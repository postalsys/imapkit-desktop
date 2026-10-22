// Folders, moving and saving messages, attachments, the New Message form, mailbox export and import, script rules,
// quirks, the REST API and injected output

import { describe, it, before, after } from 'node:test';
import assert from 'node:assert';
import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import MailComposer from 'nodemailer/lib/mail-composer/index.js';
import { freePort, startSidecar } from './helpers.js';

/**
 * Opens a plaintext IMAP connection and resolves with `{ socket, read(pattern) }`, read waits for output matching
 */
function connectImap(port) {
    return new Promise((resolve, reject) => {
        const socket = net.connect(port, '127.0.0.1');
        let buffer = '';
        let waiting = null;
        socket.setEncoding('utf8');
        socket.on('data', chunk => {
            buffer += chunk;
            if (waiting && waiting.pattern.test(buffer)) {
                const done = waiting.resolve;
                waiting = null;
                done(buffer);
            }
        });
        socket.on('error', reject);
        const read = pattern =>
            new Promise(done => {
                if (pattern.test(buffer)) {
                    return done(buffer);
                }
                waiting = { pattern, resolve: done };
            });
        read(/^\* OK/m).then(() => resolve({ socket, read }));
    });
}

describe('sidecar features', () => {
    let dataDir;
    let sidecar;
    let config;

    before(async () => {
        dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'imapkit-desktop-'));
        sidecar = startSidecar(dataDir);
        config = { host: '127.0.0.1', imapPort: await freePort(), imapsPort: 0, smtpPort: 0 };
        await sidecar.call('start', { config });
    });

    after(() => {
        sidecar.child.stdin.end();
        fs.rmSync(dataDir, { recursive: true, force: true });
    });

    it('lists the quirk presets of imapkit', async () => {
        const hello = await sidecar.call('hello');
        assert.ok(hello.quirks.some(quirk => quirk.name === 'm365-throttle' && quirk.description));
    });

    it('lists Inbox first, then the special folders, then the rest by name', async () => {
        await sidecar.call('createMailbox', { name: 'zeta' });
        await sidecar.call('createMailbox', { name: 'Alpha' });
        await sidecar.call('createMailbox', { parent: 'Alpha', name: 'Child' });
        const paths = (await sidecar.call('mailboxes')).map(folder => folder.path);
        assert.deepStrictEqual(paths, ['INBOX', 'Drafts', 'Sent', 'Junk', 'Trash', 'Alpha', 'Alpha/Child', 'zeta']);
        for (const path of ['Alpha/Child', 'Alpha', 'zeta']) {
            await sidecar.call('deleteMailbox', { path });
        }
    });

    it('creates, renames and deletes folders', async () => {
        await sidecar.call('createMailbox', { parent: 'INBOX', name: 'Projects' });
        await sidecar.call('createMailbox', { parent: 'Projects', name: 'Old' });
        assert.deepStrictEqual(await sidecar.call('renameMailbox', { path: 'Projects/Old', name: 'Archive' }), { path: 'Projects/Archive' });
        const folders = await sidecar.call('mailboxes');
        const archive = folders.find(folder => folder.path === 'Projects/Archive');
        assert.deepStrictEqual([archive.name, archive.parent, archive.canRename], ['Archive', 'Projects', true]);
        const inbox = folders.find(folder => folder.path === 'INBOX');
        assert.deepStrictEqual([inbox.name, inbox.canRename, inbox.canDelete], ['Inbox', false, false]);
        await sidecar.call('deleteMailbox', { path: 'Projects/Archive' });
        await sidecar.call('deleteMailbox', { path: 'Projects' });
        const paths = (await sidecar.call('mailboxes')).map(mailbox => mailbox.path);
        assert.ok(!paths.includes('Projects'));
        await assert.rejects(sidecar.call('deleteMailbox', { path: 'Nope' }), /does not exist/);
    });

    it('imports .eml files and summarizes them for the message list', async () => {
        const file = path.join(dataDir, 'imported.eml');
        fs.writeFileSync(
            file,
            'From: "Bob, Jr." <bob@example.com>\r\nSubject: =?UTF-8?Q?Tere_=C3=B5htust?=\r\nDate: Thu, 22 Oct 2026 11:05:00 +0000\r\n\r\nHi\r\n'
        );
        const [uid] = await sidecar.call('importMessages', { path: 'Junk', files: [file] });
        const [summary] = (await sidecar.call('messages', { path: 'Junk' })).filter(message => message.uid === uid);
        assert.strictEqual(summary.fromName, 'Bob, Jr.');
        assert.strictEqual(summary.subject, 'Tere õhtust');
        assert.strictEqual(summary.received, '2026-10-22T11:05:00.000Z');
        const { sourceHtml } = await sidecar.call('messageSource', { path: 'Junk', uid });
        assert.match(sourceHtml, /&lt;bob@example.com&gt;/);
    });

    it('reports flag changes with the messages', async () => {
        const [first] = await sidecar.call('messages', { path: 'Junk' });
        await sidecar.call('setFlags', { path: 'Junk', uids: [first.uid], flags: ['\\Flagged'], mode: 'add' });
        const event = await sidecar.waitFor(
            message => message.event === 'store' && message.data.type === 'flags' && message.data.path === 'Junk' && message.data.messages
        );
        assert.deepStrictEqual(event.data.messages, [{ uid: first.uid, flags: ['\\Flagged'] }]);
    });

    it('composes a message, moves it and saves it as .eml', async () => {
        const { uid } = await sidecar.call('composeMessage', {
            path: 'Drafts',
            from: 'tester@example.com',
            to: 'someone@example.com',
            subject: 'Composed',
            text: 'Body text'
        });
        const parsed = await sidecar.call('message', { path: 'Drafts', uid });
        assert.strictEqual(parsed.subject, 'Composed');
        assert.strictEqual(parsed.text.trim(), 'Body text');

        const moved = await sidecar.call('moveMessages', { path: 'Drafts', uids: [uid], target: 'Sent' });
        const file = path.join(dataDir, 'saved.eml');
        await sidecar.call('saveMessage', { path: 'Sent', uid: moved.uids[0].targetUid, file });
        assert.match(fs.readFileSync(file, 'utf8'), /^Subject: Composed$/m);
    });

    it('saves an attachment', async () => {
        const raw = await new MailComposer({
            subject: 'With files',
            text: 'see attached',
            attachments: [{ filename: 'notes.txt', content: 'attached text' }]
        })
            .compile()
            .build();
        const { uid } = await sidecar.call('addMessage', { path: 'INBOX', raw: raw.toString('base64'), encoding: 'base64' });
        const parsed = await sidecar.call('message', { path: 'INBOX', uid });
        assert.deepStrictEqual(
            parsed.attachments.map(attachment => attachment.filename),
            ['notes.txt']
        );
        const file = path.join(dataDir, 'notes.txt');
        await sidecar.call('saveAttachment', { path: 'INBOX', uid, index: 0, file });
        assert.strictEqual(fs.readFileSync(file, 'utf8'), 'attached text');

        // every attachment into a folder, without replacing what is there
        const dir = fs.mkdtempSync(path.join(dataDir, 'all-'));
        fs.writeFileSync(path.join(dir, 'notes.txt'), 'existing');
        assert.deepStrictEqual(await sidecar.call('saveAttachments', { path: 'INBOX', uid, dir }), ['notes (2).txt']);
        assert.strictEqual(fs.readFileSync(path.join(dir, 'notes.txt'), 'utf8'), 'existing');
    });

    it('exports and imports the mailboxes', async () => {
        const file = path.join(dataDir, 'export.json');
        await sidecar.call('exportStorage', { file });
        await sidecar.call('createMailbox', { path: 'Temporary' });
        await sidecar.call('importStorage', { file });
        assert.ok(!(await sidecar.call('mailboxes')).some(mailbox => mailbox.path === 'Temporary'));
        // a broken file leaves the mailboxes alone
        fs.writeFileSync(file, JSON.stringify({ INBOX: { messages: 'nope' } }));
        await assert.rejects(sidecar.call('importStorage', { file }), /messages/);
        assert.ok((await sidecar.call('mailboxes')).some(mailbox => mailbox.path === 'Sent'));
    });

    it('reads and checks script rules', async () => {
        const file = path.join(dataDir, 'rules.json');
        fs.writeFileSync(file, JSON.stringify({ on: 'command', command: 'NOOP', send: '$TAG NO [ALERT] scripted\r\n' }));
        const rules = await sidecar.call('readScript', { file });
        assert.strictEqual(rules.length, 1);
        await sidecar.call('start', { config: Object.assign({}, config, { script: rules }) });
        const { socket, read } = await connectImap(config.imapPort);
        socket.write('A1 NOOP\r\n');
        assert.match(await read(/^A1 /m), /^A1 NO \[ALERT\] scripted/m);
        socket.end();

        fs.writeFileSync(file, JSON.stringify({ on: 'nonsense' }));
        await assert.rejects(sidecar.call('readScript', { file }));
    });

    it('injects output into an IMAP session', async () => {
        const { socket, read } = await connectImap(config.imapPort);
        // the newest IMAP session is the one just opened
        const sessions = (await sidecar.call('sessions')).filter(session => session.kind === 'imap');
        const id = sessions[sessions.length - 1].id;
        await sidecar.call('inject', { id, data: '* OK [ALERT] Maintenance soon' });
        assert.match(await read(/ALERT/), /^\* OK \[ALERT\] Maintenance soon\r$/m);
        socket.end();
    });

    it('starts the REST API when enabled', async () => {
        const restPort = await freePort();
        const status = await sidecar.call('start', { config: Object.assign({}, config, { rest: { enabled: true, port: restPort } }) });
        assert.strictEqual(status.restPort, restPort);
        const res = await fetch('http://127.0.0.1:' + restPort + '/v1/mailboxes');
        assert.ok((await res.json()).some(mailbox => mailbox.path === 'INBOX'));
    });
});
