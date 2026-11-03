// The OAuth provider of the settings: a sign-in without a browser, logins over IMAP and SMTP with its tokens, the
// queued outcome of the next sign-in, expiring and revoking tokens, and the protocol log

import { describe, it, before, after } from 'node:test';
import assert from 'node:assert';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { ImapFlow } from 'imapflow';
import nodemailer from 'nodemailer';
import { freePort, startSidecar } from './helpers.js';
import { oauthOptions, pluginList, oauthLogText } from '../src/options.js';

const REDIRECT = 'http://127.0.0.1:5000/cb';

/** Runs the sign-in of a public client with PKCE: authorize without following the redirect, then the token request */
async function signIn(issuer, client = 'imapkit', user = 'alice@example.com') {
    const verifier = crypto.randomBytes(32).toString('base64url');
    const challenge = crypto.createHash('sha256').update(verifier).digest('base64url');
    const query = new URLSearchParams({
        response_type: 'code',
        client_id: client,
        redirect_uri: REDIRECT,
        scope: 'openid email mail',
        state: 's',
        code_challenge: challenge,
        code_challenge_method: 'S256',
        login_hint: user
    });
    const res = await fetch(issuer + '/authorize?' + query, { redirect: 'manual' });
    const redirect = new URL(res.headers.get('location'));
    const code = redirect.searchParams.get('code');
    if (!code) {
        return { error: redirect.searchParams.get('error') };
    }
    const tokens = await fetch(issuer + '/token', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ grant_type: 'authorization_code', code, redirect_uri: REDIRECT, client_id: client, code_verifier: verifier })
    });
    return tokens.json();
}

describe('OAuth settings', () => {
    it('turns the settings into the imapkit option', () => {
        assert.strictEqual(oauthOptions({ oauth: { enabled: false } }), undefined);
        const options = oauthOptions({
            host: '127.0.0.1',
            oauth: {
                enabled: true,
                port: 8085,
                consent: 'approve',
                tokenLifetime: 60,
                alwaysRefresh: true,
                clients: [
                    { id: 'app', name: 'My App', redirectUris: 'http://127.0.0.1/cb, com.example:/oauth', consent: 'deny' },
                    { id: ' ', name: 'skipped' },
                    { id: 'plain', name: '', redirectUris: '', consent: 'default' }
                ]
            }
        });
        assert.deepStrictEqual(options, {
            port: 8085,
            host: '127.0.0.1',
            consent: 'approve',
            clients: { app: { name: 'My App', redirectUris: ['http://127.0.0.1/cb', 'com.example:/oauth'], consent: 'deny' }, plain: {} },
            accessTokenTtl: 60,
            refreshTokens: 'always'
        });
        assert.strictEqual(oauthOptions({ oauth: { enabled: true, clients: [] } }).clients, undefined);
        assert.deepStrictEqual(pluginList({ plugins: ['IDLE', 'SASL-IR'], oauth: { enabled: true } }), ['IDLE', 'SASL-IR', 'XOAUTH2', 'OAUTHBEARER']);
        assert.deepStrictEqual(pluginList({ plugins: ['IDLE'] }), ['IDLE']);
    });

    it('describes the events of the provider for the log', () => {
        assert.strictEqual(
            oauthLogText({ type: 'authorize', client: 'imapkit', user: 'a@x', decision: 'approve', source: 'server default' }),
            'Sign-in request from imapkit for a@x: approve (server default)'
        );
        assert.strictEqual(
            oauthLogText({ type: 'authorize', client: null, result: 'error page', description: 'Unknown client_id "x"' }),
            'Sign-in request refused: Unknown client_id "x"'
        );
        assert.strictEqual(
            oauthLogText({ type: 'access', via: 'smtp', user: 'a@x', result: 'invalid_token', reason: 'the access token is expired' }),
            'SMTP login with an access token for a@x: refused, the access token is expired'
        );
        assert.strictEqual(oauthLogText({ type: 'revoke', tokens: 1, reason: 'control API' }), 'Revoked 1 token: control API');
        assert.strictEqual(oauthLogText({ type: 'other' }), null);
    });
});

describe('sidecar OAuth provider', () => {
    let dataDir;
    let sidecar;
    let status;

    before(async () => {
        dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'imapkit-desktop-'));
        sidecar = startSidecar(dataDir);
        const config = {
            host: '127.0.0.1',
            imapPort: await freePort(),
            imapsPort: 0,
            smtpPort: await freePort(),
            users: [{ username: 'alice@example.com', password: 'pw' }],
            plugins: ['IDLE', 'STARTTLS'],
            oauth: { enabled: true, port: await freePort(), consent: 'approve', tokenLifetime: 3600, alwaysRefresh: true, clients: [] }
        };
        status = await sidecar.call('start', { config });
    });

    after(() => {
        sidecar.child.stdin.end();
        fs.rmSync(dataDir, { recursive: true, force: true });
    });

    it('reports the issuer and the provider state', async () => {
        assert.match(status.oauthIssuer, /^http:\/\/127\.0\.0\.1:\d+$/);
        const state = await sidecar.call('oauthState');
        assert.strictEqual(state.running, true);
        assert.strictEqual(state.issuer, status.oauthIssuer);
        assert.deepStrictEqual(
            state.clients.map(client => client.id),
            ['imapkit', 'imapkit-approve', 'imapkit-deny']
        );
    });

    it('signs in and logs in over IMAP and SMTP with the access token', async () => {
        const tokens = await signIn(status.oauthIssuer);
        assert.ok(tokens.access_token);
        assert.ok(tokens.refresh_token, 'alwaysRefresh issues one without offline_access');

        const client = new ImapFlow({
            host: '127.0.0.1',
            port: status.imapPort,
            secure: false,
            tls: { rejectUnauthorized: false },
            auth: { user: 'alice@example.com', accessToken: tokens.access_token },
            logger: false
        });
        await client.connect();
        assert.ok(client.authenticated);
        await client.logout();

        const transport = nodemailer.createTransport({
            host: '127.0.0.1',
            port: status.smtpPort,
            secure: false,
            tls: { rejectUnauthorized: false },
            auth: { type: 'OAuth2', user: 'alice@example.com', accessToken: tokens.access_token }
        });
        const info = await transport.sendMail({ from: 'alice@example.com', to: 'bob@example.com', subject: 'OAuth', text: 'Hi' });
        assert.match(info.response, /^250 /);

        const wrong = nodemailer.createTransport({
            host: '127.0.0.1',
            port: status.smtpPort,
            secure: false,
            tls: { rejectUnauthorized: false },
            auth: { type: 'OAuth2', user: 'alice@example.com', accessToken: 'not-a-token' }
        });
        await assert.rejects(wrong.sendMail({ from: 'alice@example.com', to: 'bob@example.com', subject: 'x', text: 'x' }), /535|Invalid login/);

        await sidecar.waitFor(
            event =>
                event.event === 'trace' && event.data.id === 'oauth' && /SMTP login with an access token for alice@example.com: accepted/.test(event.data.text)
        );
        await sidecar.waitFor(event => event.event === 'oauth' && event.data.type === 'access' && event.data.via === 'imap');
    });

    it('queues the outcome of the next sign-in', async () => {
        await sidecar.call('oauthQueue', { decision: { error: 'temporarily_unavailable' } });
        assert.strictEqual((await sidecar.call('oauthState')).decisions.length, 1);
        assert.deepStrictEqual(await signIn(status.oauthIssuer), { error: 'temporarily_unavailable' });
        assert.ok((await signIn(status.oauthIssuer)).access_token);
        await sidecar.call('oauthQueue', { decision: 'deny' });
        await sidecar.call('oauthClearQueue');
        assert.deepStrictEqual((await sidecar.call('oauthState')).decisions, []);
        await assert.rejects(sidecar.call('oauthQueue', { decision: 'maybe' }), /decision must be/);
        await sidecar.waitFor(
            event => event.event === 'trace' && /Queued the outcome of the next OAuth sign-in: error temporarily_unavailable/.test(event.data.text)
        );
    });

    it('expires and revokes tokens, the next login fails', async () => {
        const tokens = await signIn(status.oauthIssuer);
        const login = () =>
            new ImapFlow({
                host: '127.0.0.1',
                port: status.imapPort,
                secure: false,
                tls: { rejectUnauthorized: false },
                auth: { user: 'alice@example.com', accessToken: tokens.access_token },
                logger: false
            }).connect();
        const expired = await sidecar.call('oauthExpire', { token: tokens.access_token });
        assert.deepStrictEqual(expired, { expired: 1 });
        await assert.rejects(login());
        const revoked = await sidecar.call('oauthRevoke', {});
        assert.ok(revoked.revoked >= 2);
        const listed = (await sidecar.call('oauthState')).tokens;
        assert.ok(listed.every(item => item.revoked));
    });

    it('answers the OAuth methods with an error when the provider is off', async () => {
        await sidecar.call('stop');
        assert.deepStrictEqual(await sidecar.call('oauthState'), { running: false, clients: [], decisions: [], tokens: [] });
        await assert.rejects(sidecar.call('oauthQueue', { decision: 'deny' }), /not running/);
    });
});
