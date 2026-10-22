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
        // unread, like new mail in any folder: the reader sees that a client sent something
        const message = { uid: this.nextId++, raw, envelope, user, internaldate: new Date().toISOString(), flags: [] };
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
     * Changes flags of messages, as control.setFlags() does for a mailbox
     *
     * @param {Array} uids Message ids
     * @param {Array} flags Flags
     * @param {String} mode `set`, `add` or `remove`
     * @return {Array} `[{ uid, flags }]` of the messages that changed
     */
    setFlags(uids, flags, mode) {
        const changed = [];
        for (const message of this.messages.filter(entry => uids.includes(entry.uid))) {
            const before = message.flags.join(' ');
            if (mode === 'add') {
                message.flags = [...new Set(message.flags.concat(flags))];
            } else if (mode === 'remove') {
                message.flags = message.flags.filter(flag => !flags.includes(flag));
            } else {
                message.flags = flags.slice();
            }
            if (message.flags.join(' ') !== before) {
                changed.push({ uid: message.uid, flags: message.flags.slice() });
            }
        }
        if (changed.length) {
            this.emit('change', changed);
        }
        return changed;
    }

    /**
     * @return {Number} the number of unread messages
     */
    unseen() {
        return this.messages.filter(message => !message.flags.includes('\\Seen')).length;
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
