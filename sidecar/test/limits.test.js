// The Limits and SMTP settings: the options they give imapkit, the recipient rules, and both servers enforcing them

import { describe, it, before, after } from 'node:test';
import assert from 'node:assert';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import nodemailer from 'nodemailer';
import { freePort, startSidecar } from './helpers.js';
import { limitOptions, recipientRules } from '../src/options.js';
import { DEFAULT_CONFIG } from '../src/defaults.js';

describe('limit options', () => {
    it('passes on the limits that are set', () => {
        assert.deepStrictEqual(limitOptions({ limits: DEFAULT_CONFIG.limits }), {});
        assert.deepStrictEqual(
            limitOptions({ limits: { appendLimit: 0, quotaStorage: 10, quotaMessages: null, quotaSoft: true, messageLimit: 5, maxLiteralSize: 2048 } }),
            { appendLimit: 0, messageLimit: 5, maxLiteralSize: 2048, quota: { STORAGE: 10, soft: true } }
        );
    });
});

describe('rejected recipients', () => {
    it('makes rules of the rows, with wildcards, codes and texts', () => {
        const rules = recipientRules([
            { address: '', code: 550, message: 'skipped' },
            { address: 'blocked@example.com', code: null, message: '' },
            { address: '*@full.test', code: 452, message: 'Mailbox full, try later' },
            { address: 'a+b@x.test', code: '551', message: 'Nope' }
        ]);
        assert.strictEqual(rules.length, 3);
        assert.ok(rules[0].pattern.test('Blocked@Example.com'));
        assert.ok(!rules[0].pattern.test('blockedXexample.com'));
        assert.deepStrictEqual([rules[0].code, rules[0].message], [550, 'Recipient rejected']);
        assert.ok(rules[1].pattern.test('anyone@full.test'));
        assert.deepStrictEqual([rules[1].code, rules[1].message], [452, 'Mailbox full, try later']);
        assert.ok(rules[2].pattern.test('a+b@x.test'));
        assert.deepStrictEqual([rules[2].code, rules[2].message], [551, 'Nope']);
    });

    it('refuses a code that is not 4xx or 5xx', () => {
        assert.throws(() => recipientRules([{ address: 'ok@x.test', code: 250 }]), /row 1: the reply code 250/);
    });
});

describe('limits and SMTP rules on the servers', () => {
    let dataDir;
    let sidecar;
    let config;

    before(async () => {
        dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'imapkit-desktop-'));
        sidecar = startSidecar(dataDir);
        config = {
            host: '127.0.0.1',
            imapPort: await freePort(),
            imapsPort: 0,
            smtpPort: await freePort(),
            plugins: DEFAULT_CONFIG.plugins.concat('APPENDLIMIT'),
            limits: Object.assign({}, DEFAULT_CONFIG.limits, { appendLimit: 100 }),
            smtp: { requireAuth: false, maxSize: 2000, rejectRecipients: [{ address: '*@blocked.test', code: 551, message: 'Gone away' }] }
        };
        await sidecar.call('start', { config });
    });

    after(() => {
        sidecar.child.stdin.end();
        fs.rmSync(dataDir, { recursive: true, force: true });
    });

    const transport = auth => nodemailer.createTransport({ host: '127.0.0.1', port: config.smtpPort, secure: false, auth, tls: { rejectUnauthorized: false } });

    it('advertises and enforces APPENDLIMIT', async () => {
        await sidecar.call('clientConnect', { host: '127.0.0.1', port: config.imapPort });
        await sidecar.waitFor(event => event.event === 'client' && event.data.type === 'data' && /^\* OK/.test(event.data.text));
        await sidecar.call('clientSend', { data: 'a LOGIN testuser testpass' });
        await sidecar.call('clientSend', { data: 'c CAPABILITY' });
        await sidecar.waitFor(event => event.event === 'client' && /APPENDLIMIT=100/.test(event.data.text || ''));
        // 198 octets and the CRLF of the line are the literal, the empty line ends the command
        await sidecar.call('clientSend', { data: 'b APPEND INBOX {200+}' });
        await sidecar.call('clientSend', { data: 'x'.repeat(198) });
        await sidecar.call('clientSend', { data: '' });
        await sidecar.waitFor(event => event.event === 'client' && /^b NO \[TOOBIG\]/m.test(event.data.text || ''));
        await sidecar.call('clientDisconnect');
    });

    it('refuses rejected recipients and messages over the size limit', async () => {
        await assert.rejects(transport().sendMail({ from: 'a@x.test', to: 'someone@blocked.test', text: 'hi' }), err => err.responseCode === 551);
        await assert.rejects(transport().sendMail({ from: 'a@x.test', to: 'b@x.test', text: 'y'.repeat(5000) }), err => err.responseCode === 552);
        const info = await transport().sendMail({ from: 'a@x.test', to: 'b@x.test', text: 'small' });
        assert.match(info.response, /^250 .*INBOX/);
    });

    it('requires a login when told to', async () => {
        await sidecar.call('start', { config: Object.assign({}, config, { smtp: { requireAuth: true, maxSize: null, rejectRecipients: [] } }) });
        await assert.rejects(transport().sendMail({ from: 'a@x.test', to: 'b@x.test', text: 'hi' }), err => err.responseCode === 530);
        const info = await transport({ user: 'any', pass: 'thing' }).sendMail({ from: 'a@x.test', to: 'b@x.test', text: 'hi' });
        assert.match(info.response, /Outbox/);
    });

    it('fails the start for a broken rule', async () => {
        await assert.rejects(
            sidecar.call('start', {
                config: Object.assign({}, config, {
                    smtp: { requireAuth: false, maxSize: null, rejectRecipients: [{ address: 'x@y.test', code: 999, message: '' }] }
                })
            }),
            /reply code 999/
        );
    });
});
