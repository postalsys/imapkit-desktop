// Runs the sidecar as the GUI does, over stdio, and talks to its servers with real IMAP and SMTP clients

import { describe, it, before, after } from 'node:test';
import assert from 'node:assert';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import nodemailer from 'nodemailer';
import { ImapFlow } from 'imapflow';
import { freePort, startSidecar } from './helpers.js';

describe('sidecar', () => {
    let dataDir;
    let sidecar;
    let ports;

    before(async () => {
        dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'imapkit-desktop-'));
        sidecar = startSidecar(dataDir);
        ports = { imapPort: await freePort(), imapsPort: await freePort(), smtpPort: await freePort() };
    });

    after(() => {
        sidecar.child.stdin.end();
        fs.rmSync(dataDir, { recursive: true, force: true });
    });

    it('starts IMAP, IMAPS and SMTP', async () => {
        const hello = await sidecar.call('hello');
        assert.strictEqual(hello.status.running, false);
        const status = await sidecar.call('start', { config: Object.assign({ host: '127.0.0.1' }, ports) });
        assert.deepStrictEqual(status, Object.assign({ running: true, host: '127.0.0.1', restPort: 0 }, ports));
        await sidecar.waitFor(event => event.event === 'status' && event.data.running);
    });

    it('delivers SMTP mail to INBOX and logs the SMTP session', async () => {
        const transport = nodemailer.createTransport({ host: '127.0.0.1', port: ports.smtpPort, secure: false, tls: { rejectUnauthorized: false } });
        await transport.sendMail({ from: 'alice@example.com', to: 'bob@example.com', subject: 'Hello over SMTP', text: 'Hi Bob!' });
        await sidecar.waitFor(event => event.event === 'store' && event.data.type === 'append');
        const smtpOpen = await sidecar.waitFor(event => event.event === 'session' && event.data.kind === 'smtp' && event.data.type === 'open');
        await sidecar.waitFor(event => event.event === 'trace' && event.data.id === smtpOpen.data.id && /^MAIL FROM/.test(event.data.text));
        // the client upgraded with STARTTLS
        await sidecar.waitFor(event => event.event === 'session' && event.data.id === smtpOpen.data.id && event.data.type === 'update' && event.data.secure);

        const messages = await sidecar.call('messages', { path: 'INBOX' });
        const sent = messages.find(message => message.subject === 'Hello over SMTP');
        assert.ok(sent);
        assert.match(sent.from, /alice@example\.com/);
        const parsed = await sidecar.call('message', { path: 'INBOX', uid: sent.uid, markSeen: true });
        assert.strictEqual(parsed.text.trim(), 'Hi Bob!');
        assert.ok(parsed.flags.includes('\\Seen'));
    });

    it('serves the same mailboxes over IMAPS and traces the plaintext', async () => {
        const client = new ImapFlow({
            host: '127.0.0.1',
            port: ports.imapsPort,
            secure: true,
            tls: { rejectUnauthorized: false },
            auth: { user: 'testuser', pass: 'testpass' },
            logger: false
        });
        await client.connect();
        const status = await client.status('INBOX', { messages: true });
        assert.strictEqual(status.messages, 2);
        await client.logout();

        const login = await sidecar.waitFor(event => event.event === 'session' && event.data.kind === 'imap' && event.data.type === 'login');
        assert.strictEqual(login.data.secure, true);
        assert.strictEqual(login.data.user, 'testuser');
        await sidecar.waitFor(event => event.event === 'trace' && event.data.id === login.data.id && /STATUS INBOX/.test(event.data.text));
        await sidecar.waitFor(event => event.event === 'session' && event.data.id === login.data.id && event.data.type === 'close');
    });

    it('moves deleted messages to Trash, then expunges them there', async () => {
        const [first] = await sidecar.call('messages', { path: 'INBOX' });
        await sidecar.call('deleteMessages', { path: 'INBOX', uids: [first.uid] });
        const trash = await sidecar.call('messages', { path: 'Trash' });
        assert.strictEqual(trash.length, 1);
        await sidecar.call('deleteMessages', { path: 'Trash', uids: [trash[0].uid] });
        assert.strictEqual((await sidecar.call('messages', { path: 'Trash' })).length, 0);
    });

    it('keeps the mailboxes on disk after stop', async () => {
        await sidecar.call('stop');
        const saved = JSON.parse(fs.readFileSync(path.join(dataDir, 'storage.json'), 'utf8'));
        assert.strictEqual(saved.INBOX.messages.length, 1);
        // a stopped server can still be browsed
        assert.strictEqual((await sidecar.call('mailboxes')).length, 5);
        await assert.rejects(sidecar.call('nope'), /Unknown method nope/);
    });
});
