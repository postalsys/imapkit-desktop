// The Outbox: mail that clients sent through the SMTP server after logging in (AUTH), the way a mail client sends
// through its submission server. It is kept here with its SMTP envelope instead of being delivered anywhere, so a
// test can see what a client sent and to whom. Mail without AUTH is incoming mail and goes to INBOX instead

import { EventEmitter } from 'node:events';

// the path the GUI uses for the Outbox in the methods that take a mailbox path; not a valid IMAP mailbox name
export const OUTBOX = '\\Outbox';

export class Outbox extends EventEmitter {
    /**
     * @param {Array} [saved] Messages of toJSON(), to continue where the last run stopped
     */
    constructor(saved) {
        super();
        this.messages = (saved || []).map(message => Object.assign({}, message, { raw: Buffer.from(message.raw, 'base64') }));
        this.nextId = this.messages.reduce((max, message) => Math.max(max, message.uid), 0) + 1;
    }

    /**
     * Keeps a sent message
     *
     * @param {Buffer} raw Message source
     * @param {Object} envelope `{ from, to: [] }` of the SMTP transaction
     * @param {String|null} user The user name the client logged in with
     * @return {Object} the message
     */
    add(raw, envelope, user) {
        const message = { uid: this.nextId++, raw, envelope, user, internaldate: new Date().toISOString(), flags: ['\\Seen'] };
        this.messages.push(message);
        this.emit('change');
        return message;
    }

    /**
     * @param {Number} uid Message id
     * @return {Object} the message in the shape of control.getMessage(), with its envelope
     */
    get(uid) {
        const message = this.messages.find(entry => entry.uid === uid);
        if (!message) {
            throw new Error('The Outbox has no message ' + uid);
        }
        return Object.assign({ size: message.raw.length }, message);
    }

    /**
     * @return {Array} every message in the shape of control.listMessages() with `raw`
     */
    list() {
        return this.messages.map(message => Object.assign({ size: message.raw.length }, message));
    }

    /**
     * Removes messages
     *
     * @param {Array} uids Message ids
     * @return {Object} `{ uids }` of the removed messages
     */
    remove(uids) {
        const removed = this.messages.filter(message => uids.includes(message.uid)).map(message => message.uid);
        this.messages = this.messages.filter(message => !uids.includes(message.uid));
        if (removed.length) {
            this.emit('change');
        }
        return { uids: removed };
    }

    toJSON() {
        return this.messages.map(message => Object.assign({}, message, { raw: message.raw.toString('base64') }));
    }
}
