// Turns the settings of the GUI into the options of ImapKit and the rules of the SMTP server

/**
 * The ID response of the server (RFC 2971) from the settings: the fields that have a value, when custom values are
 * turned on. Without any, imapkit answers ID NIL
 *
 * @param {Object} config The settings, `id: { enabled, values: { name, version, ... } }`
 * @return {Object|undefined} the `id` option of imapkit
 */
export function serverId(config) {
    const id = config.id || {};
    if (!id.enabled) {
        return undefined;
    }
    const fields = Object.entries(id.values || {}).filter(([key, value]) => key && typeof value === 'string' && value.trim());
    return fields.length ? Object.fromEntries(fields.map(([key, value]) => [key, value.trim()])) : undefined;
}

// a limit that is set: a whole number, null or missing for no limit
const isSet = value => Number.isInteger(value) && value >= 0;

/**
 * The limit options of imapkit from `config.limits`: `{ appendLimit, quotaStorage, quotaMessages, quotaSoft,
 * messageLimit, maxLiteralSize }`. Only the limits that are set become options, each takes effect when the plugin
 * that reads it is loaded (APPENDLIMIT, QUOTA, MESSAGELIMIT or SAVELIMIT; maxLiteralSize always)
 *
 * @param {Object} config The settings
 * @return {Object} options for imapkit
 */
export function limitOptions(config) {
    const limits = config.limits || {};
    const options = {};
    for (const key of ['appendLimit', 'messageLimit', 'maxLiteralSize']) {
        if (isSet(limits[key])) {
            options[key] = limits[key];
        }
    }
    // STORAGE is in units of 1024 octets, as in the QUOTA responses
    const quota = {};
    if (isSet(limits.quotaStorage)) {
        quota.STORAGE = limits.quotaStorage;
    }
    if (isSet(limits.quotaMessages)) {
        quota.MESSAGE = limits.quotaMessages;
    }
    if (Object.keys(quota).length) {
        options.quota = Object.assign(quota, { soft: !!limits.quotaSoft });
    }
    return options;
}

/**
 * Parses the rejected recipients of the SMTP settings, one rule per line: an address where `*` matches any characters,
 * then optionally the reply code (4xx or 5xx, 550 when left out) and the text of the reply
 *
 *   blocked@example.com
 *   *@full.test 452 Mailbox full, try again later
 *
 * @param {String[]} lines The rules as the GUI has them
 * @return {Object[]} `[{ pattern, code, message }]`, the pattern as a RegExp
 * @throws {Error} for a line with a code that is not 4xx or 5xx
 */
export function recipientRules(lines) {
    const rules = [];
    (lines || []).forEach((line, index) => {
        const [address, ...rest] = String(line).trim().split(/\s+/);
        if (!address || address.startsWith('#')) {
            return;
        }
        let code = 550;
        if (rest.length && /^\d+$/.test(rest[0])) {
            code = Number(rest.shift());
            if (code < 400 || code > 599) {
                throw new Error(`Rejected recipients, line ${index + 1}: the reply code ${code} is not 4xx or 5xx`);
            }
        }
        const source = address
            .split('*')
            .map(part => part.replace(/[.+?^${}()|[\]\\]/g, '\\$&'))
            .join('.*');
        rules.push({ pattern: new RegExp('^' + source + '$', 'i'), code, message: rest.join(' ') || 'Recipient rejected' });
    });
    return rules;
}
