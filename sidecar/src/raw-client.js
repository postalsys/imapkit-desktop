// The plaintext client of the Client page, like telnet or openssl s_client: one TCP or TLS connection to any host and
// port, the lines typed in the GUI go out with CRLF and what the server sends comes back as `client` events. TLS
// accepts every certificate, self-signed and expired ones too, and describes it the way s_client does

import net from 'node:net';
import tls from 'node:tls';
import { StringDecoder } from 'node:string_decoder';

const CONNECT_TIMEOUT = 15000;

// control characters other than tab and line ends would garble the console, they show as dots
const CONTROL_CHARACTERS = /[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/g;

// "CN=localhost, O=Example" from the subject or issuer object of a certificate
function distinguishedName(name) {
    return Object.entries(name || {})
        .map(([key, value]) => key + '=' + (Array.isArray(value) ? value.join('+') : value))
        .join(', ');
}

/**
 * The TLS details of a connection as lines of text, like the output of openssl s_client: the certificate chain, the
 * verification result (shown, never enforced), the protocol and the cipher
 *
 * @param {tls.TLSSocket} socket A connected TLS socket
 * @return {String[]} lines
 */
export function describeTls(socket) {
    const lines = [];
    let certificate = socket.getPeerCertificate(true);
    if (!certificate || !Object.keys(certificate).length) {
        lines.push('The server sent no certificate');
    } else {
        lines.push('Certificate chain');
        const seen = new Set();
        for (let depth = 0; certificate && Object.keys(certificate).length && !seen.has(certificate.fingerprint256); depth++) {
            // a self-signed certificate is its own issuer
            seen.add(certificate.fingerprint256);
            lines.push(` ${depth} s:${distinguishedName(certificate.subject)}`);
            lines.push(`   i:${distinguishedName(certificate.issuer)}`);
            lines.push(`   v:NotBefore: ${certificate.valid_from}; NotAfter: ${certificate.valid_to}`);
            if (depth === 0) {
                if (certificate.subjectaltname) {
                    lines.push(`   Names: ${certificate.subjectaltname}`);
                }
                lines.push(`   SHA-256: ${certificate.fingerprint256}`);
            }
            certificate = certificate.issuerCertificate;
        }
    }
    lines.push(
        socket.authorized
            ? 'Verification: OK'
            : `Verification error: ${socket.authorizationError || 'unknown'} (accepted anyway, this client trusts every certificate)`
    );
    const cipher = socket.getCipher();
    lines.push(`Protocol: ${socket.getProtocol()}, cipher: ${cipher ? cipher.name : 'unknown'}`);
    return lines;
}

// every certificate is accepted, SNI is for host names only
function tlsOptions(host, options) {
    return Object.assign({ rejectUnauthorized: false, servername: net.isIP(host) ? undefined : host }, options);
}

/**
 * The "This app" presets of the Client page: the listeners of the saved settings, at an address a client on this
 * computer can connect to
 *
 * @param {Object} config The settings of the server
 * @return {Object[]} `[{ name, host, port, tls }]`
 */
export function clientPresets(config) {
    // a server on every address is reachable on the loopback address
    const host = !config.host || config.host === '0.0.0.0' || config.host === '::' ? '127.0.0.1' : config.host;
    return [
        { name: 'This app: IMAP', host, port: config.imapPort, tls: false },
        { name: 'This app: IMAPS', host, port: config.imapsPort, tls: true },
        { name: 'This app: SMTP', host, port: config.smtpPort, tls: false }
    ].filter(preset => preset.port);
}

export class RawClient {
    /**
     * @param {Function} emit Gets the `client` events: `{ type: 'info' | 'error' | 'data' | 'sent' | 'state', ... }`,
     * returns false when the GUI falls behind, the socket then pauses until `waitForGui` calls back
     * @param {Function} waitForGui Calls its argument once the GUI has caught up
     */
    constructor(emit, waitForGui) {
        this.emit = emit;
        this.waitForGui = waitForGui;
        this.socket = null;
    }

    log(text, type = 'info') {
        this.emit({ type, text });
    }

    // `connecting` until the connection (and its TLS handshake) is up, then `connected`
    state() {
        const connected = !!this.socket && this.ready;
        this.emit({ type: 'state', connecting: !!this.socket && !this.ready, connected, secure: this.secure, host: this.host, port: this.port });
    }

    /**
     * Starts connecting to host:port, with TLS from the start when `tls` is set. Throws for bad input only, how the
     * connection goes (connected, the TLS details, or why it failed) comes as events
     */
    connect({ host, port, tls: secure }) {
        if (this.socket) {
            throw new Error('Already connected, disconnect first');
        }
        host = String(host || '').trim();
        port = Number(port);
        if (!host) {
            throw new Error('Enter a host name or address');
        }
        if (!Number.isInteger(port) || port < 1 || port > 65535) {
            throw new Error('The port must be a number from 1 to 65535');
        }
        Object.assign(this, { host, port, ready: false, secure: false, closing: false, error: null, startTls: null });
        this.log(`Connecting to ${host}:${port}${secure ? ' with TLS' : ''}...`);

        const socket = secure ? tls.connect(tlsOptions(host, { host, port })) : net.connect({ host, port });
        this.attach(socket);
        this.state();
        socket.setTimeout(CONNECT_TIMEOUT, () => socket.destroy(new Error(`No connection after ${CONNECT_TIMEOUT / 1000} seconds`)));
        socket.once(secure ? 'secureConnect' : 'connect', () => {
            socket.setTimeout(0);
            this.ready = true;
            this.log(`Connected to ${socket.remoteAddress}:${socket.remotePort}`);
            if (secure) {
                this.tlsReady(socket);
            }
            this.state();
        });
    }

    // listens to a socket, the plain one first and after STARTTLS the TLS socket on top of it
    attach(socket) {
        this.socket = socket;
        this.decoder = new StringDecoder('utf8');
        socket.on('data', chunk => this.received(socket, chunk));
        socket.on('error', err => {
            if (socket === this.socket) {
                this.error = err.message;
            }
        });
        socket.on('close', () => {
            // the plain socket of an upgraded connection closes with the TLS socket, that one reports it
            if (socket !== this.socket) {
                return;
            }
            if (this.closing) {
                this.log('Disconnected');
            } else if (this.error) {
                this.log('Connection failed: ' + this.error, 'error');
            } else {
                this.log('Connection closed by the server');
            }
            this.socket = null;
            this.ready = false;
            this.secure = false;
            this.state();
        });
    }

    tlsReady(socket) {
        this.secure = true;
        for (const line of describeTls(socket)) {
            this.log(line);
        }
    }

    received(socket, chunk) {
        if (socket !== this.socket) {
            return;
        }
        const text = this.decoder.write(chunk);
        if (text && this.emit({ type: 'data', text: text.replace(CONTROL_CHARACTERS, '.') }) === false) {
            // a big response faster than the GUI shows it: read on once the GUI caught up, not into memory
            socket.pause();
            this.waitForGui(() => socket.resume());
        }
        if (this.startTls) {
            this.checkStartTls(text);
        }
    }

    send({ data }) {
        if (!this.socket || !this.ready) {
            throw new Error('Not connected');
        }
        const line = String(data ?? '');
        this.socket.write(line + '\r\n');
        this.emit({ type: 'sent', text: line + '\r\n' });
        this.expectStartTls(line);
    }

    // STARTTLS typed in a plaintext session: IMAP has a tag before it ("a STARTTLS"), SMTP not
    expectStartTls(line) {
        const match = !this.secure && /^(?:(\S+)\s+)?STARTTLS\s*$/i.exec(line);
        this.startTls = match ? { tag: match[1] || null, pending: '' } : null;
    }

    // upgrades the connection when the server agrees to STARTTLS: the tagged OK of IMAP, 220 of SMTP
    checkStartTls(text) {
        const state = this.startTls;
        state.pending += text;
        let end;
        while (this.startTls === state && (end = state.pending.indexOf('\n')) >= 0) {
            const line = state.pending.slice(0, end).replace(/\r$/, '');
            state.pending = state.pending.slice(end + 1);
            if (state.tag !== null) {
                if (line.startsWith(state.tag + ' ')) {
                    this.startTls = null;
                    if (/^\S+\s+OK\b/i.test(line)) {
                        this.upgrade();
                    }
                }
            } else if (/^\d{3} /.test(line)) {
                // the last line of an SMTP reply, the ones before it have "220-"
                this.startTls = null;
                if (line.startsWith('220')) {
                    this.upgrade();
                }
            }
        }
    }

    upgrade() {
        this.log('Starting TLS...');
        const socket = tls.connect(tlsOptions(this.host, { socket: this.socket }));
        this.attach(socket);
        socket.once('secureConnect', () => {
            this.tlsReady(socket);
            this.state();
        });
    }

    disconnect() {
        if (this.socket) {
            this.closing = true;
            this.socket.destroy();
        }
    }
}
