// The plaintext client of the Client page: connecting with and without TLS, STARTTLS on request, failures

import { describe, it, before, after } from 'node:test';
import assert from 'node:assert';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { freePort, startSidecar } from './helpers.js';
import { clientPresets } from '../src/raw-client.js';

describe('client page connection', () => {
    let dataDir;
    let sidecar;
    let config;
    // the client events since the last mark()
    let from = 0;
    const mark = () => (from = sidecar.events.length);
    const clientEvent = predicate => sidecar.waitFor((event, index) => index >= from && event.event === 'client' && predicate(event.data));
    const received = pattern => clientEvent(data => data.type === 'data' && pattern.test(data.text));
    const connected = secure => clientEvent(data => data.type === 'state' && data.connected && data.secure === secure);
    const disconnected = () => clientEvent(data => data.type === 'state' && !data.connected);

    before(async () => {
        dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'imapkit-desktop-'));
        sidecar = startSidecar(dataDir);
        config = { host: '127.0.0.1', imapPort: await freePort(), imapsPort: await freePort(), smtpPort: await freePort() };
        await sidecar.call('start', { config });
    });

    after(() => {
        sidecar.child.stdin.end();
        fs.rmSync(dataDir, { recursive: true, force: true });
    });

    it('talks IMAP in plaintext and upgrades on STARTTLS', async () => {
        mark();
        await sidecar.call('clientConnect', { host: '127.0.0.1', port: config.imapPort, tls: false });
        await connected(false);
        await received(/^\* OK/);

        mark();
        await sidecar.call('clientSend', { data: 'a STARTTLS' });
        await clientEvent(data => data.type === 'sent' && data.text === 'a STARTTLS\r\n');
        await clientEvent(data => data.type === 'info' && data.text === 'Certificate chain');
        await clientEvent(data => data.type === 'state' && data.connected && data.secure);

        // the session goes on over TLS
        mark();
        await sidecar.call('clientSend', { data: 'b LOGIN testuser testpass' });
        await received(/^b OK/m);

        mark();
        await sidecar.call('clientDisconnect');
        await disconnected();
        await clientEvent(data => data.type === 'info' && data.text === 'Disconnected');
    });

    it('connects with TLS and describes the certificate', async () => {
        mark();
        await sidecar.call('clientConnect', { host: 'localhost', port: config.imapsPort, tls: true });
        await connected(true);
        await clientEvent(data => data.type === 'info' && /^ 0 s:/.test(data.text));
        await clientEvent(data => data.type === 'info' && /^Verification error: .*accepted anyway/.test(data.text));
        await clientEvent(data => data.type === 'info' && /^Protocol: TLSv1\.[23]/.test(data.text));
        await received(/^\* OK/);

        mark();
        await sidecar.call('clientSend', { data: 'a LOGOUT' });
        await received(/^a OK/m);
        await clientEvent(data => data.type === 'info' && data.text === 'Connection closed by the server');
        await disconnected();
    });

    it('stays in plaintext when the server refuses STARTTLS', async () => {
        mark();
        await sidecar.call('clientConnect', { host: '127.0.0.1', port: config.imapPort, tls: false });
        await received(/^\* OK/);
        mark();
        await sidecar.call('clientSend', { data: 'a LOGIN testuser testpass' });
        await received(/^a OK/m);

        // STARTTLS is for the Not Authenticated state only
        mark();
        await sidecar.call('clientSend', { data: 'b STARTTLS' });
        await received(/^b (BAD|NO)/m);
        await sidecar.call('clientSend', { data: 'c NOOP' });
        await received(/^c OK/m);
        assert.ok(!sidecar.events.slice(from).some(event => event.event === 'client' && event.data.secure));

        await sidecar.call('clientDisconnect');
        await disconnected();
    });

    it('upgrades SMTP on STARTTLS', async () => {
        mark();
        await sidecar.call('clientConnect', { host: '127.0.0.1', port: config.smtpPort, tls: false });
        await received(/^220 /m);

        mark();
        await sidecar.call('clientSend', { data: 'EHLO test' });
        await received(/^250 /m);
        mark();
        await sidecar.call('clientSend', { data: 'STARTTLS' });
        await clientEvent(data => data.type === 'state' && data.secure);

        mark();
        await sidecar.call('clientSend', { data: 'EHLO test' });
        await received(/^250 /m);
        await sidecar.call('clientDisconnect');
        await disconnected();
    });

    it('reports a refused connection and checks the input', async () => {
        const closed = await freePort();
        mark();
        // the outcome comes as events only
        await sidecar.call('clientConnect', { host: '127.0.0.1', port: closed, tls: false });
        await clientEvent(data => data.type === 'error' && /^Connection failed: .*ECONNREFUSED/.test(data.text));
        await disconnected();
        assert.ok(sidecar.events.slice(from).some(event => event.event === 'client' && event.data.type === 'state' && event.data.connecting));

        await assert.rejects(sidecar.call('clientConnect', { host: '127.0.0.1', port: 70000 }), /port must be a number/);
        await assert.rejects(sidecar.call('clientConnect', { host: ' ', port: 143 }), /host name/);
        await assert.rejects(sidecar.call('clientSend', { data: 'x' }), /Not connected/);
    });

    it('offers the listeners of the settings as presets', async () => {
        assert.deepStrictEqual(await sidecar.call('clientPresets'), [
            { name: 'This app: IMAP', host: '127.0.0.1', port: config.imapPort, tls: false },
            { name: 'This app: IMAPS', host: '127.0.0.1', port: config.imapsPort, tls: true },
            { name: 'This app: SMTP', host: '127.0.0.1', port: config.smtpPort, tls: false }
        ]);
    });
});

describe('client presets', () => {
    it('connect to the loopback address of a server on every address, and leave out listeners that are off', () => {
        assert.deepStrictEqual(clientPresets({ host: '0.0.0.0', imapPort: 143, imapsPort: 0, smtpPort: 25 }), [
            { name: 'This app: IMAP', host: '127.0.0.1', port: 143, tls: false },
            { name: 'This app: SMTP', host: '127.0.0.1', port: 25, tls: false }
        ]);
        assert.strictEqual(clientPresets({ host: '192.168.1.5', imapPort: 143, imapsPort: 993, smtpPort: 0 })[1].host, '192.168.1.5');
    });
});
