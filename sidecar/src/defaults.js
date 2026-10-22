// Default settings and the mailboxes a fresh server starts with

export const DEFAULT_PLUGINS = [
    'ENABLE',
    'ID',
    'IDLE',
    'NAMESPACE',
    'UNSELECT',
    'UIDPLUS',
    'MOVE',
    'CONDSTORE',
    'SPECIAL-USE',
    'LITERALPLUS',
    'SASL-IR',
    'AUTH-PLAIN',
    'STARTTLS'
];

export const DEFAULT_CONFIG = {
    host: '127.0.0.1',
    imapPort: 1143,
    // implicit TLS, 0 turns it off
    imapsPort: 1993,
    // 0 turns SMTP off
    smtpPort: 1025,
    users: [{ username: 'testuser', password: 'testpass' }],
    plugins: DEFAULT_PLUGINS,
    // quirk presets of imapkit, e.g. "m365-throttle"
    quirks: [],
    // script rules of imapkit in their JSON form
    script: [],
    // the ID response (RFC 2971): custom field values when enabled, ID NIL otherwise
    id: { enabled: false, values: {} },
    // limits for imapkit, null is no limit (see options.js)
    limits: { appendLimit: null, quotaStorage: null, quotaMessages: null, quotaSoft: false, messageLimit: null, maxLiteralSize: null },
    // the SMTP server: a required login, the largest message in bytes (null for any) and the rejected recipients
    smtp: { requireAuth: false, maxSize: null, rejectRecipients: [] },
    rest: { enabled: false, port: 8143, host: '127.0.0.1', token: '' }
};

const welcome = [
    'From: ImapKit Desktop <mock@imapkit.local>',
    'To: Test User <testuser@imapkit.local>',
    'Subject: Welcome to ImapKit Desktop',
    'Date: ' + new Date().toUTCString().replace('GMT', '+0000'),
    'Message-ID: <welcome@imapkit.local>',
    'MIME-Version: 1.0',
    'Content-Type: text/html; charset=utf-8',
    '',
    '<p>This mailbox lives in the memory of <b>ImapKit Desktop</b>.</p>',
    '<p>Point an IMAP client to the IMAP port, or send mail to the SMTP port, and watch the sessions in the Connections view.</p>',
    ''
].join('\r\n');

/**
 * The storage of a fresh server, in the shape of the imapkit storage option
 *
 * @return {Object} storage
 */
export function defaultStorage() {
    return {
        INBOX: { messages: [{ raw: welcome, flags: [] }] },
        '': {
            folders: {
                Drafts: { 'special-use': '\\Drafts' },
                Sent: { 'special-use': '\\Sent' },
                Trash: { 'special-use': '\\Trash' },
                Junk: { 'special-use': '\\Junk' }
            }
        }
    };
}
