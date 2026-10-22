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
import { summarize, parseMessage, sourceDocument, readAttachment, composeMessage } from './messages.js';
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

const server = new MailServer(Object.assign({}, DEFAULT_CONFIG, readJson(configFile, {})), readJson(storageFile, null) || defaultStorage());

function send(message) {
    process.stdout.write(JSON.stringify(message) + '\n');
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
    } catch (err) {
        send({ event: 'log', data: { level: 'error', message: 'Saving the mailboxes failed: ' + err.message } });
    }
}

for (const name of ['status', 'session', 'trace', 'store', 'log']) {
    server.on(name, data => send({ event: name, data }));
}
server.on('store', saveSoon);

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
    hello: () => ({ version: VERSION, dataDir, config: server.config, plugins: PLUGINS, quirks: QUIRKS, status: server.status() }),

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

    mailboxes: () => server.control.listMailboxes().map(folderInfo),

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

    messages: params => server.control.listMessages(params.path, { raw: true }).map(summarize),

    message: async params => {
        const message = server.control.getMessage(params.path, params.uid);
        if (params.markSeen && !message.flags.includes('\\Seen')) {
            server.control.setFlags(params.path, [params.uid], ['\\Seen'], 'add');
            message.flags = message.flags.concat('\\Seen');
        }
        return parseMessage(message);
    },

    messageSource: params => ({ sourceHtml: sourceDocument(server.control.getMessage(params.path, params.uid).raw) }),

    setFlags: params => server.control.setFlags(params.path, params.uids, params.flags, params.mode || 'set'),

    moveMessages: params => server.control.moveMessages(params.path, params.uids, params.target),

    deleteMessages: params => {
        const trash = trashPath();
        if (trash && trash !== params.path) {
            return server.control.moveMessages(params.path, params.uids, trash);
        }
        return server.control.expungeMessages(params.path, params.uids);
    },

    addMessage: params => server.control.addMessage(params.path, { raw: Buffer.from(params.raw, params.encoding === 'base64' ? 'base64' : 'utf8') }),

    // adds .eml files to a mailbox, returns the new UIDs
    importMessages: params => params.files.map(file => server.control.addMessage(params.path, { raw: fs.readFileSync(file) }).uid),

    composeMessage: async params => server.control.addMessage(params.path, { raw: await composeMessage(params), flags: params.flags || [] }),

    saveMessage: params => {
        fs.writeFileSync(params.file, server.control.getMessage(params.path, params.uid).raw);
        return true;
    },

    saveAttachment: async params => {
        fs.writeFileSync(params.file, await readAttachment(server.control.getMessage(params.path, params.uid), params.index));
        return true;
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

    reset: () => replaceStorage(defaultStorage())
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
        send({ id: request.id, result: (await method(request.params || {})) ?? null });
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
    await server.stop().catch(() => false);
    saveNow();
    process.exit(0);
});

send({ event: 'ready', data: { version: VERSION, dataDir } });

// a mock server is useful right away, a failure (a port in use) is reported with the status event
if (!args['no-autostart']) {
    server.start(server.config).catch(() => false);
}
