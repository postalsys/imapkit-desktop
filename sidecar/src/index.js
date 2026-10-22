#!/usr/bin/env node
// The sidecar of ImapKit Desktop. The native GUI starts it and talks to it with JSON lines over stdio:
//
//   GUI -> sidecar   {"id": 1, "method": "start", "params": {...}}
//   sidecar -> GUI   {"id": 1, "result": {...}}  or  {"id": 1, "error": {"message": "..."}}
//   sidecar -> GUI   {"event": "session", "data": {...}}   events are not numbered
//
// Methods and events are listed in docs/protocol.md. The settings and the mailboxes are saved in the data
// directory (--data-dir, by default %APPDATA%\ImapKitDesktop or ~/.imapkit-desktop), so they survive a restart.
// The servers start with the saved settings right away, unless --no-autostart is given.

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import readline from 'node:readline';
import { createRequire } from 'node:module';
import { parseArgs } from 'node:util';
import imapkit, { quirks, validateStorage } from 'imapkit';
import { MailServer } from './mail-server.js';
import { Outbox, OUTBOX } from './outbox.js';
import { RawClient, clientPresets } from './raw-client.js';
import { releaseUpdate } from './update.js';
import addressparser from 'nodemailer/lib/addressparser/index.js';
import { summarize, parseMessage, sourceDocument, readAttachment, readAttachments, composeMessage } from './messages.js';
import { DEFAULT_CONFIG, defaultStorage } from './defaults.js';

const require = createRequire(import.meta.url);
// kept in step with the version of the app (package.json at the root of the repository)
const VERSION = require('../package.json').version;

// mailbox changes are written at most this often, every write serializes all mailboxes
const SAVE_INTERVAL = 3000;

// the plugins ImapKit has, with their --help text, for the extension list of the GUI
const pluginHelp = readJson(path.join(path.dirname(require.resolve('imapkit/package.json')), 'dist', 'plugin-help.json'), {});
const PLUGINS = Object.keys(pluginHelp).map(name => ({ name, help: pluginHelp[name].join(' ') }));
const QUIRKS = Object.keys(quirks).map(name => ({ name, description: quirks[name].description }));

const { values: args } = parseArgs({ options: { 'data-dir': { type: 'string' }, 'no-autostart': { type: 'boolean' } } });
const dataDir = args['data-dir'] || (process.env.APPDATA ? path.join(process.env.APPDATA, 'ImapKitDesktop') : path.join(os.homedir(), '.imapkit-desktop'));
const configFile = path.join(dataDir, 'config.json');
const storageFile = path.join(dataDir, 'storage.json');
const outboxFile = path.join(dataDir, 'outbox.json');

function readJson(file, fallback) {
    try {
        return JSON.parse(fs.readFileSync(file, 'utf8'));
    } catch {
        return fallback;
    }
}

function writeJson(file, value) {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    // write and rename, a crash never leaves half a file behind
    fs.writeFileSync(file + '.tmp', JSON.stringify(value));
    fs.renameSync(file + '.tmp', file);
}

const outbox = new Outbox(readJson(outboxFile, []));
const server = new MailServer(Object.assign({}, DEFAULT_CONFIG, readJson(configFile, {})), readJson(storageFile, null) || defaultStorage(), outbox);
// the connection of the Client page
const rawClient = new RawClient(
    data => send({ event: 'client', data }),
    resume => process.stdout.once('drain', resume)
);

// false when the pipe to the GUI is full
function send(message) {
    return process.stdout.write(JSON.stringify(message) + '\n');
}

// mailbox changes are saved a moment later, a burst of changes is written once
let saveTimer = null;
function saveSoon() {
    if (!saveTimer) {
        saveTimer = setTimeout(saveNow, SAVE_INTERVAL);
    }
}
function saveNow() {
    clearTimeout(saveTimer);
    saveTimer = null;
    try {
        writeJson(storageFile, server.snapshot());
        writeJson(outboxFile, outbox);
    } catch (err) {
        send({ event: 'log', data: { level: 'error', message: 'Saving the mailboxes failed: ' + err.message } });
    }
}

for (const name of ['status', 'session', 'trace', 'store', 'log']) {
    server.on(name, data => send({ event: name, data }));
}
server.on('store', saveSoon);
outbox.on('change', changed => {
    // a flag change comes with the messages, the GUI updates its rows without a reload
    send({ event: 'store', data: changed ? { type: 'flags', path: OUTBOX, messages: changed } : { type: 'outbox', path: OUTBOX } });
    saveSoon();
});

/**
 * A message of a mailbox, or of the Outbox
 *
 * @param {String} mailbox Mailbox path, or OUTBOX
 * @param {Number} uid UID, or the id of an Outbox message
 * @return {Object} the message in the shape of control.getMessage()
 */
function messageOf(mailbox, uid) {
    return mailbox === OUTBOX ? outbox.get(uid) : server.control.getMessage(mailbox, uid);
}

/**
 * Replaces every mailbox, restarting the servers when they run (their sessions see a new server)
 *
 * @param {Object} storage Mailboxes in the shape of the imapkit storage option
 * @return {Promise<Object>} status
 */
async function replaceStorage(storage) {
    const wasRunning = server.running;
    await server.stop();
    server.setStorage(storage);
    saveNow();
    if (wasRunning) {
        await server.start(server.config);
    }
    send({ event: 'store', data: { type: 'reset', path: null } });
    return server.status();
}

/**
 * Finds the Trash mailbox, deleted messages go there first like in Outlook Express
 *
 * @return {String|null} path
 */
function trashPath() {
    const trash = server.control.listMailboxes().find(mailbox => (mailbox.specialUse || []).includes('\\Trash'));
    return trash ? trash.path : null;
}

/**
 * A mailbox as the folder tree shows it
 *
 * @param {Object} mailbox A mailbox of control.listMailboxes()
 * @return {Object} folder
 */
function folderInfo(mailbox) {
    const cut = mailbox.path.lastIndexOf(mailbox.delimiter);
    return {
        path: mailbox.path,
        // INBOX is the one name the server keeps in upper case, a mail client shows it as Inbox
        name: mailbox.path === 'INBOX' ? 'Inbox' : mailbox.path.slice(cut + 1),
        parent: cut < 0 ? null : mailbox.path.slice(0, cut),
        delimiter: mailbox.delimiter,
        messages: mailbox.messages,
        unseen: mailbox.unseen,
        specialUse: (mailbox.specialUse || [])[0] || null,
        selectable: mailbox.selectable,
        // the GUI keeps INBOX in place, DELETE of INBOX is refused and RENAME of it moves its messages
        canRename: mailbox.path !== 'INBOX',
        canDelete: mailbox.path !== 'INBOX'
    };
}

// the order of the special folders, the way mail clients list them: Inbox first, then the folders of the mail
// workflow, the rest alphabetically after them
const FOLDER_ORDER = ['\\Drafts', '\\Templates', '\\Sent', '\\Archive', '\\Junk', '\\Trash', '\\All', '\\Flagged', '\\Important'];

/**
 * Orders folders for the folder tree: at every level Inbox, then the special folders, then the rest by name.
 * Subfolders come right after their parent
 *
 * @param {Array} folders Folders of folderInfo()
 * @return {Array} folders
 */
function sortFolders(folders) {
    const rank = folder => {
        if (folder.path === 'INBOX') {
            return -1;
        }
        const index = FOLDER_ORDER.indexOf(folder.specialUse);
        return index < 0 ? FOLDER_ORDER.length : index;
    };
    const byParent = new Map();
    for (const folder of folders) {
        const siblings = byParent.get(folder.parent) || [];
        siblings.push(folder);
        byParent.set(folder.parent, siblings);
    }
    const sorted = [];
    const visit = parent => {
        const siblings = (byParent.get(parent) || []).sort(
            (a, b) => rank(a) - rank(b) || a.name.localeCompare(b.name, undefined, { sensitivity: 'base', numeric: true })
        );
        for (const folder of siblings) {
            sorted.push(folder);
            visit(folder.path);
        }
    };
    visit(null);
    // folders whose parent is not listed (a \\Noselect level that LIST left out) go at the end, nothing is lost
    return sorted.concat(folders.filter(folder => !sorted.includes(folder)));
}

/**
 * The path of a new folder: top level without a parent, and next to INBOX for INBOX, which holds no folders here
 *
 * @param {String|null} parent Path of the parent folder
 * @param {String} name Name of the new folder
 * @return {String} path
 */
function childPath(parent, name) {
    if (!parent || parent === 'INBOX') {
        return name;
    }
    return parent + server.control.getMailbox(parent).delimiter + name;
}

const methods = {
    hello: () => ({ version: VERSION, dataDir, config: server.config, defaults: DEFAULT_CONFIG, plugins: PLUGINS, quirks: QUIRKS, status: server.status() }),

    getConfig: () => server.config,

    start: async params => {
        const config = params.config ? Object.assign({}, DEFAULT_CONFIG, params.config) : server.config;
        // saved also when the start fails (a port in use), the settings are what the user entered
        writeJson(configFile, config);
        return server.start(config);
    },

    stop: async () => {
        const status = await server.stop();
        saveNow();
        return status;
    },

    status: () => server.status(),

    sessions: () => server.sessions(),

    disconnect: params => {
        server.disconnect(params.id);
        return true;
    },

    inject: params => {
        server.inject(params.id, params.data);
        return true;
    },

    mailboxes: () => sortFolders(server.control.listMailboxes().map(folderInfo)),

    createMailbox: params => server.control.createMailbox(params.path || childPath(params.parent, params.name), { subscribed: true }),

    renameMailbox: params => {
        let newPath = params.newPath;
        if (!newPath) {
            const cut = params.path.lastIndexOf(server.control.getMailbox(params.path).delimiter);
            newPath = params.path.slice(0, cut + 1) + params.name;
        }
        server.control.renameMailbox(params.path, newPath);
        return { path: newPath };
    },

    deleteMailbox: params => server.control.deleteMailbox(params.path),

    messages: params => (params.path === OUTBOX ? outbox.list() : server.control.listMessages(params.path, { raw: true })).map(summarize),

    // the number of messages in the Outbox, for the folder tree
    outboxStatus: () => ({ messages: outbox.messages.length, unseen: outbox.unseen() }),

    emptyOutbox: () => outbox.remove(outbox.messages.map(message => message.uid)),

    message: async params => {
        if (params.path === OUTBOX) {
            // with the SMTP envelope: the recipients of a Bcc are only there
            if (params.markSeen && outbox.setFlags([params.uid], ['\\Seen'], 'add').length) {
                logAction('Opened message ' + params.uid + ' of the Outbox, marked as read');
            }
            const message = outbox.get(params.uid);
            return Object.assign(await parseMessage(message, !!params.remoteImages), { envelope: message.envelope, user: message.user });
        }
        const message = server.control.getMessage(params.path, params.uid);
        if (params.markSeen && !message.flags.includes('\\Seen')) {
            server.control.setFlags(params.path, [params.uid], ['\\Seen'], 'add');
            message.flags = message.flags.concat('\\Seen');
            logAction('Opened UID ' + params.uid + ' in ' + params.path + ', marked as read');
        }
        return parseMessage(message, !!params.remoteImages);
    },

    messageSource: params => ({ sourceHtml: sourceDocument(messageOf(params.path, params.uid).raw) }),

    setFlags: params =>
        params.path === OUTBOX
            ? outbox.setFlags(params.uids, params.flags, params.mode || 'set')
            : server.control.setFlags(params.path, params.uids, params.flags, params.mode || 'set'),

    moveMessages: params => {
        if (params.path !== OUTBOX) {
            return server.control.moveMessages(params.path, params.uids, params.target);
        }
        // out of the Outbox: delivered into the mailbox like an incoming message
        const uids = params.uids.map(uid => ({ uid, targetUid: server.control.addMessage(params.target, { raw: outbox.get(uid).raw }).uid }));
        outbox.remove(params.uids);
        return { uids };
    },

    deleteMessages: params => {
        if (params.path === OUTBOX) {
            return outbox.remove(params.uids);
        }
        const trash = trashPath();
        if (trash && trash !== params.path) {
            return server.control.moveMessages(params.path, params.uids, trash);
        }
        return server.control.expungeMessages(params.path, params.uids);
    },

    addMessage: params => server.control.addMessage(params.path, { raw: Buffer.from(params.raw, params.encoding === 'base64' ? 'base64' : 'utf8') }),

    // adds .eml files to a mailbox, returns the new UIDs
    importMessages: params =>
        params.files.map(file =>
            params.path === OUTBOX
                ? outbox.add(fs.readFileSync(file), { from: '', to: [] }, null).uid
                : server.control.addMessage(params.path, { raw: fs.readFileSync(file) }).uid
        ),

    composeMessage: async params => {
        const raw = await composeMessage(params);
        if (params.path === OUTBOX) {
            // as if a client sent it: the envelope has every recipient
            const addresses = text => addressparser(text || '', { flatten: true }).map(address => address.address);
            return outbox.add(raw, { from: addresses(params.from)[0] || '', to: addresses(params.to).concat(addresses(params.cc)) }, null);
        }
        return server.control.addMessage(params.path, { raw, flags: params.flags || [] });
    },

    saveMessage: params => {
        fs.writeFileSync(params.file, messageOf(params.path, params.uid).raw);
        return true;
    },

    saveAttachment: async params => {
        fs.writeFileSync(params.file, await readAttachment(messageOf(params.path, params.uid), params.index));
        return true;
    },

    // writes every attachment into a folder, existing files are not replaced. Returns the names written
    saveAttachments: async params => {
        const written = [];
        for (const { filename, content } of await readAttachments(messageOf(params.path, params.uid))) {
            let name = filename;
            for (let n = 2; fs.existsSync(path.join(params.dir, name)); n++) {
                const dot = filename.lastIndexOf('.');
                name = dot > 0 ? filename.slice(0, dot) + ' (' + n + ')' + filename.slice(dot) : filename + ' (' + n + ')';
            }
            fs.writeFileSync(path.join(params.dir, name), content);
            written.push(name);
        }
        return written;
    },

    exportStorage: params => {
        fs.writeFileSync(params.file, JSON.stringify(server.snapshot(), null, 2));
        return true;
    },

    importStorage: params => {
        const storage = JSON.parse(fs.readFileSync(params.file, 'utf8'));
        // a broken file fails here with the path of the problem, before the current mailboxes are replaced
        validateStorage(storage);
        return replaceStorage(storage);
    },

    // reads script rules from a JSON file (a rule or a list of rules), the GUI keeps them in the settings
    readScript: params => {
        const rules = [].concat(JSON.parse(fs.readFileSync(params.file, 'utf8')));
        // imapkit checks the rules when a server is built with them
        imapkit({ script: rules });
        return rules;
    },

    reset: () => replaceStorage(defaultStorage()),

    // the latest published release on GitHub, asked for from Help > Check for Updates only
    checkUpdate: async () => {
        const res = await fetch('https://api.github.com/repos/postalsys/imapkit-desktop/releases/latest', {
            headers: { Accept: 'application/vnd.github+json', 'User-Agent': 'ImapKit-Desktop/' + VERSION },
            signal: AbortSignal.timeout(15000)
        });
        if (res.status === 404) {
            return releaseUpdate(null, VERSION);
        }
        if (!res.ok) {
            throw new Error('GitHub answered ' + res.status);
        }
        return releaseUpdate(await res.json(), VERSION);
    },

    clientPresets: () => clientPresets(server.config),

    clientConnect: params => rawClient.connect(params),

    clientSend: params => rawClient.send(params),

    clientDisconnect: () => rawClient.disconnect()
};

/**
 * Writes a change made in the GUI to the protocol log, as the line of a session named "app", so the log shows what
 * changed the mailboxes between the commands of the clients
 *
 * @param {String} text What was done
 */
function logAction(text) {
    send({ event: 'trace', data: { id: 'app', direction: 'ui', text: text + '\r\n', time: new Date().toISOString() } });
}

const where = mailbox => (mailbox === OUTBOX ? 'the Outbox' : mailbox);
// "UID 5", "UIDs 5, 6" or "12 messages"
const which = (mailbox, uids) => {
    const label = mailbox === OUTBOX ? 'message' : 'UID';
    if (uids.length > 5) {
        return uids.length + ' messages';
    }
    return label + (uids.length === 1 ? ' ' : 's ') + uids.join(', ');
};
const FLAG_NAMES = { '\\Seen': ['read', 'unread'], '\\Flagged': ['flagged', 'not flagged'] };

// the log line of a change made with a method, from its parameters and result, null for nothing to log
const ACTIONS = {
    start: () => 'Started the server',
    stop: () => 'Stopped the server',
    disconnect: params => 'Disconnected ' + params.id,
    inject: params => 'Sent to ' + params.id + ': ' + String(params.data).trim(),
    createMailbox: (params, result) => 'Created the folder ' + ((result && result.path) || params.path || childPath(params.parent, params.name)),
    renameMailbox: (params, result) => 'Renamed the folder ' + params.path + ' to ' + result.path,
    deleteMailbox: params => 'Deleted the folder ' + params.path,
    setFlags: (params, result) => {
        const changed = (Array.isArray(result) ? result : []).map(message => message.uid);
        if (!changed.length) {
            return null;
        }
        const names = params.flags.length === 1 && FLAG_NAMES[params.flags[0]];
        if (names && (params.mode === 'add' || params.mode === 'remove')) {
            return 'Marked ' + which(params.path, changed) + ' in ' + where(params.path) + ' as ' + names[params.mode === 'add' ? 0 : 1];
        }
        return 'Set the flags of ' + which(params.path, changed) + ' in ' + where(params.path) + ' (' + params.mode + ' ' + params.flags.join(' ') + ')';
    },
    moveMessages: params =>
        (params.path === OUTBOX ? 'Delivered ' : 'Moved ') + which(params.path, params.uids) + ' from ' + where(params.path) + ' to ' + params.target,
    deleteMessages: (params, result) =>
        result && result.uidvalidity
            ? 'Moved ' + which(params.path, params.uids) + ' from ' + where(params.path) + ' to the Trash'
            : 'Deleted ' + which(params.path, params.uids) + ' from ' + where(params.path),
    addMessage: (params, result) => 'Added UID ' + result.uid + ' to ' + params.path,
    importMessages: (params, result) => 'Imported ' + result.length + ' message' + (result.length === 1 ? '' : 's') + ' into ' + where(params.path),
    composeMessage: params => 'Wrote a new message into ' + where(params.path),
    importStorage: () => 'Replaced the mailboxes with an imported file',
    reset: () => 'Reset the mailboxes to the defaults'
};

async function handle(line) {
    let request;
    try {
        request = JSON.parse(line);
    } catch {
        return send({ event: 'log', data: { level: 'error', message: 'Invalid request: ' + line.slice(0, 200) } });
    }
    const method = Object.hasOwn(methods, request.method) ? methods[request.method] : null;
    try {
        if (!method) {
            throw new Error('Unknown method ' + request.method);
        }
        const params = request.params || {};
        const count = request.method === 'emptyOutbox' ? outbox.messages.length : 0;
        const result = (await method(params)) ?? null;
        const describe = Object.hasOwn(ACTIONS, request.method) ? ACTIONS[request.method] : null;
        const text = request.method === 'emptyOutbox' ? (count ? 'Emptied the Outbox (' + count + ' messages)' : null) : describe && describe(params, result);
        if (text) {
            logAction(text);
        }
        send({ id: request.id, result });
    } catch (err) {
        send({ id: request.id, error: { message: err.message, code: err.code || null } });
    }
}

const input = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
input.on('line', line => {
    if (line.trim()) {
        handle(line);
    }
});

// the GUI closed the pipe: it exited or crashed, save and go
input.on('close', async () => {
    rawClient.disconnect();
    await server.stop().catch(() => false);
    saveNow();
    process.exit(0);
});

send({ event: 'ready', data: { version: VERSION, dataDir } });

// a mock server is useful right away, a failure (a port in use) is reported with the status event
if (!args['no-autostart']) {
    server.start(server.config).catch(() => false);
}
