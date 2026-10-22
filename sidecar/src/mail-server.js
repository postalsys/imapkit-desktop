// The mail servers the GUI controls: ImapKit for IMAP (plaintext with STARTTLS, and implicit TLS) and an SMTP server
// that delivers into the same storage. Everything the GUI shows comes from the events of this class.

import { EventEmitter } from 'node:events';
import imapkit from 'imapkit';
import { SMTPServer } from 'smtp-server';

// longer chunks of protocol traffic are cut in the log, a FETCH of a large message would flood it
const TRACE_LIMIT = 8 * 1024;

/**
 * Turns protocol traffic into log text, cut at TRACE_LIMIT octets
 *
 * @param {Buffer|String} data Octets, or text from the SMTP log
 * @return {String} text
 */
function traceText(data) {
    if (typeof data === 'string') {
        return data.length <= TRACE_LIMIT ? data : data.slice(0, TRACE_LIMIT) + '\r\n[... ' + (data.length - TRACE_LIMIT) + ' more characters]\r\n';
    }
    if (data.length <= TRACE_LIMIT) {
        return data.toString('utf8');
    }
    return data.subarray(0, TRACE_LIMIT).toString('utf8') + '\r\n[... ' + (data.length - TRACE_LIMIT) + ' more bytes]\r\n';
}

/**
 * Waits until a net.Server listens, or fails to
 *
 * @param {net.Server} server Server
 * @param {Number} port Port
 * @param {String} host Address
 * @return {Promise<Number>} the port
 */
function listen(server, port, host) {
    return new Promise((resolve, reject) => {
        const onError = err => reject(err);
        server.once('error', onError);
        server.listen(port, host, () => {
            server.removeListener('error', onError);
            resolve(server.address().port);
        });
    });
}

export class MailServer extends EventEmitter {
    /**
     * @param {Object} config Settings, see DEFAULT_CONFIG
     * @param {Object} storage The mailboxes to start with, in the shape of the imapkit storage option
     * @param {Outbox} outbox Keeps the mail that clients send after logging in
     */
    constructor(config, storage, outbox) {
        super();
        this.config = config;
        this.storage = storage;
        // where authenticated SMTP mail goes, see outbox.js
        this.outbox = outbox;
        this.imap = null;
        this.smtp = null;
        this.running = false;
        this.ports = {};
        // session id to what the GUI knows about it, the close event of imapkit has no remote address any more
        this.sessionInfo = new Map();
        // smtp-server connection id to session id, the SMTP log names connections by theirs
        this.smtpSessions = new Map();
        this.smtpCounter = 0;
    }

    /**
     * Builds the ImapKit server for the configuration, from the current mailboxes
     */
    build() {
        const config = this.config;
        const users = Object.create(null);
        for (const user of config.users) {
            users[user.username] = { password: user.password };
        }
        const rest = config.rest || {};
        const server = imapkit({
            storage: this.snapshot(),
            users,
            plugins: config.plugins,
            quirks: config.quirks || [],
            script: config.script || [],
            tls: config.imapsPort ? { port: config.imapsPort, host: config.host } : undefined,
            // the REST API controls everything, imapkit refuses a non-loopback address without a token
            rest: rest.enabled ? { port: rest.port, host: rest.host || '127.0.0.1', token: rest.token || undefined } : undefined
        });

        server.on('session', event => this.onImapSession(event));
        server.on('trace', ({ session, direction, data }) => this.emitTrace('imap-' + session, direction, data));
        server.on('mailbox', event => this.emit('store', { type: 'mailbox', path: event.path, oldPath: event.oldPath || null }));
        server.on('append', mailbox => this.emit('store', { type: 'append', path: mailbox.path }));
        server.on('expunge', mailbox => this.emit('store', { type: 'expunge', path: mailbox.path }));
        // flag changes of the control API come with the messages, the GUI updates its rows without a reload
        server.on('flags', (mailbox, messages) =>
            this.emit('store', { type: 'flags', path: mailbox.path, messages: messages.map(message => ({ uid: message.uid, flags: message.flags.slice() })) })
        );
        // STORE of a session has no flags event, the command event tells that flags may have changed
        server.on('command', event => {
            if (/STORE$/.test(event.command) && event.status === 'OK') {
                const info = this.sessionInfo.get('imap-' + event.session);
                this.emit('store', { type: 'flags', path: info ? info.mailbox : null });
            }
        });

        this.imap = server;
    }

    /**
     * Starts listening with a configuration. Changing plugins or users needs a new ImapKit server, the mailboxes are
     * carried over
     *
     * @param {Object} config Settings
     * @return {Promise<Object>} status
     */
    async start(config) {
        if (!config.users || !config.users.length) {
            throw new Error('At least one test account is needed');
        }
        await this.stop();
        this.config = config;
        this.build();
        try {
            const imapPort = await this.imap.start(config.imapPort, config.host);
            this.ports = {
                imapPort,
                imapsPort: this.imap.tlsServer ? this.imap.tlsServer.address().port : 0,
                restPort: this.imap.restServer ? this.imap.restServer.address().port : 0,
                smtpPort: 0
            };
            if (config.smtpPort) {
                this.smtp = this.createSmtpServer();
                this.ports.smtpPort = await listen(this.smtp.server, config.smtpPort, config.host);
            }
        } catch (err) {
            await this.stop();
            this.emitStatus(err.message);
            throw err;
        }
        this.running = true;
        this.emitStatus();
        return this.status();
    }

    /**
     * Stops listening and closes every session. The mailboxes stay, so they can still be browsed
     *
     * @return {Promise<Object>} status
     */
    async stop() {
        if (this.smtp) {
            const smtp = this.smtp;
            this.smtp = null;
            await new Promise(resolve => smtp.close(resolve));
        }
        if (this.imap && (this.imap.server.listening || this.imap.tlsServer)) {
            await this.imap.stop();
        }
        const wasRunning = this.running;
        this.running = false;
        this.ports = {};
        if (wasRunning) {
            this.emitStatus();
        }
        return this.status();
    }

    /**
     * @return {Object} `{ running, host, imapPort, imapsPort, smtpPort, restPort, restUrl }`
     */
    status() {
        const status = Object.assign({ running: this.running, host: this.config.host }, this.ports);
        if (this.ports.restPort) {
            status.restUrl = 'http://' + ((this.config.rest && this.config.rest.host) || '127.0.0.1') + ':' + this.ports.restPort + '/v1/';
        }
        return status;
    }

    emitStatus(error) {
        this.emit('status', Object.assign(this.status(), error ? { error } : {}));
    }

    /**
     * The control API of the current ImapKit server, a stopped server can still be browsed
     *
     * @return {Object} control API
     */
    get control() {
        if (!this.imap) {
            this.build();
        }
        return this.imap.control;
    }

    /**
     * @return {Object} the mailboxes in the shape of the imapkit storage option
     */
    snapshot() {
        return this.imap ? this.imap.control.snapshot() : this.storage;
    }

    /**
     * Replaces every mailbox. The next start, or browsing, builds a server from them
     *
     * @param {Object} storage Mailboxes in the shape of the imapkit storage option
     */
    setStorage(storage) {
        this.imap = null;
        this.storage = storage;
    }

    /**
     * Connected IMAP and SMTP sessions
     *
     * @return {Array} sessions
     */
    sessions() {
        return [...this.sessionInfo.values()];
    }

    /**
     * @param {String} id Session id
     * @return {Object} what is known about the session
     */
    session(id) {
        const info = this.sessionInfo.get(id);
        if (!info) {
            throw new Error('No session ' + id);
        }
        return info;
    }

    /**
     * Closes a session, IMAP with BYE and SMTP with 421
     *
     * @param {String} id Session id
     */
    disconnect(id) {
        const info = this.session(id);
        if (info.kind === 'imap' && this.imap) {
            this.imap.control.disconnect(info.number, { text: 'Disconnected by the administrator' });
        } else if (info.kind === 'smtp' && this.smtp) {
            for (const connection of this.smtp.connections) {
                if (connection.id === info.cid) {
                    connection.send(421, 'Disconnected by the administrator');
                    connection.close();
                }
            }
        }
    }

    /**
     * Writes data to an IMAP session as it is, like an unsolicited response
     *
     * @param {String} id Session id
     * @param {String} data Output, CRLF is added when missing
     */
    inject(id, data) {
        const info = this.session(id);
        if (info.kind !== 'imap' || !this.imap) {
            throw new Error('Only IMAP sessions take injected data');
        }
        this.imap.control.inject(info.number, /\r\n$/.test(data) ? data : data + '\r\n');
    }

    onImapSession(event) {
        const number = event.session.session;
        const id = 'imap-' + number;
        const known = this.sessionInfo.get(id);
        const info = {
            id,
            number,
            kind: 'imap',
            user: event.session.user,
            state: event.session.state,
            mailbox: event.session.mailbox,
            secure: event.session.secure,
            remoteAddress: event.session.remoteAddress || (known ? known.remoteAddress : null),
            started: known ? known.started : new Date().toISOString()
        };
        if (event.type === 'close') {
            this.sessionInfo.delete(id);
        } else {
            this.sessionInfo.set(id, info);
        }
        this.emit('session', Object.assign({ type: event.type }, info));
    }

    emitTrace(id, direction, data) {
        this.emit('trace', { id, direction, text: traceText(data), time: new Date().toISOString() });
    }

    /**
     * Updates an SMTP session and tells the GUI
     *
     * @param {String} cid smtp-server connection id
     * @param {String} type Event type
     * @param {Object} [changes] Fields that changed
     */
    updateSmtpSession(cid, type, changes) {
        const info = this.sessionInfo.get(this.smtpSessions.get(cid));
        if (info) {
            Object.assign(info, changes);
            this.emit('session', Object.assign({ type }, info));
        }
    }

    /**
     * An SMTP server that accepts any sender, recipient and login. Mail of a session that logged in goes to the Outbox,
     * other mail is delivered to INBOX. Its protocol log comes from the logger of smtp-server, which redacts AUTH
     * credentials
     *
     * @return {SMTPServer} server
     */
    createSmtpServer() {
        const credentials = this.imap.getCredentials();
        const noop = () => false;
        const logger = {
            // smtp-server logs every command as (meta, 'C:', line) and every response as (meta, 'S:', line)
            debug: (meta, ...args) => {
                const id = meta && this.smtpSessions.get(meta.cid);
                if (id && (meta.tnx === 'command' || meta.tnx === 'send')) {
                    this.emitTrace(id, meta.tnx === 'send' ? 'out' : 'in', args.slice(1).join(' ') + '\r\n');
                }
            },
            info: noop,
            warn: noop,
            error: noop,
            trace: noop,
            fatal: noop
        };

        const smtp = new SMTPServer({
            banner: 'ImapKit Desktop',
            key: credentials.key,
            cert: credentials.cert,
            authOptional: true,
            logger,
            onConnect: (session, done) => {
                const number = ++this.smtpCounter;
                const info = {
                    id: 'smtp-' + number,
                    number,
                    kind: 'smtp',
                    cid: session.id,
                    user: null,
                    state: 'Connected',
                    mailbox: null,
                    secure: !!session.secure,
                    remoteAddress: session.remoteAddress,
                    started: new Date().toISOString()
                };
                this.sessionInfo.set(info.id, info);
                this.smtpSessions.set(session.id, info.id);
                this.emit('session', Object.assign({ type: 'open' }, info));
                done();
            },
            onAuth: (auth, session, done) => {
                this.updateSmtpSession(session.id, 'login', { user: auth.username });
                done(null, { user: auth.username });
            },
            onMailFrom: (address, session, done) => {
                // STARTTLS has upgraded the session by now, if the client used it
                this.updateSmtpSession(session.id, 'update', { secure: !!session.secure });
                done();
            },
            onData: (stream, session, done) => {
                const chunks = [];
                stream.on('data', chunk => chunks.push(chunk));
                stream.on('error', done);
                stream.on('end', () => {
                    const raw = Buffer.concat(chunks);
                    const id = this.smtpSessions.get(session.id);
                    if (id) {
                        this.emitTrace(id, 'in', '[message of ' + raw.length + ' bytes]\r\n');
                    }
                    try {
                        if (session.user) {
                            // a client that logged in submits mail it sends, like to the submission server of a provider
                            const envelope = {
                                from: session.envelope.mailFrom ? session.envelope.mailFrom.address : '',
                                to: session.envelope.rcptTo.map(recipient => recipient.address)
                            };
                            const { uid } = this.outbox.add(raw, envelope, (typeof session.user === 'string' ? session.user : session.user.user) || null);
                            done(null, 'Message kept in the Outbox as message ' + uid + ', not delivered');
                        } else {
                            // without a login it is incoming mail from another server
                            const { uid } = this.imap.control.addMessage('INBOX', { raw });
                            done(null, 'Message delivered to INBOX as UID ' + uid);
                        }
                    } catch (err) {
                        done(err);
                    }
                });
            },
            onClose: session => {
                const id = this.smtpSessions.get(session.id);
                const info = id && this.sessionInfo.get(id);
                this.smtpSessions.delete(session.id);
                if (info) {
                    this.sessionInfo.delete(id);
                    this.emit('session', Object.assign({ type: 'close' }, info));
                }
            }
        });
        smtp.on('error', err => this.emit('log', { level: 'error', message: 'SMTP: ' + err.message }));
        return smtp;
    }
}
