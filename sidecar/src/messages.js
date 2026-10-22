// Message summaries for the message list, parsed messages and documents for the preview pane

import libmime from 'libmime';
import { simpleParser } from 'mailparser';
import addressparser from 'nodemailer/lib/addressparser/index.js';
import MailComposer from 'nodemailer/lib/mail-composer/index.js';

// the preview loads nothing from the network and runs no scripts, embedded images are data: URLs. Remote images
// load only when the user asks for them, a sender could tell from them when and where the message was read
const policy = remoteImages =>
    "default-src 'none'; img-src data: cid:" + (remoteImages ? ' http: https:' : '') + "; style-src 'unsafe-inline' data:; font-src data:";
// email HTML is written for a white page, also when Windows uses dark mode
const meta = remoteImages =>
    '<meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="' +
    policy(remoteImages) +
    '"><meta name="color-scheme" content="light"><style>:root{color-scheme:light}html{background:#fff;color:#000}</style>';
const META = meta(false);
// an image, a background or a CSS url() from the network
const REMOTE_IMAGE = /\b(?:src|background|srcset)\s*=\s*["']?\s*(?:https?:)?\/\/|url\(\s*["']?\s*(?:https?:)?\/\//i;
const BASE_STYLE = '<style>body{font:10pt Arial,sans-serif;margin:8px;}pre{white-space:pre-wrap;word-wrap:break-word;}</style>';
const HTML_ESCAPES = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' };

const escapeHtml = text => text.replace(/[&<>"]/g, char => HTML_ESCAPES[char]);

const document = body => '<!doctype html><html><head>' + META + BASE_STYLE + '</head><body>' + body + '</body></html>';

/**
 * A complete HTML document for the preview pane. The Content-Security-Policy goes first, before anything of the
 * message: a browser keeps the policy of the first meta tag it parses, and a message can not move or hide a tag
 * that comes before it (looking for the <head> of the message could be fooled by comments, attributes or <header>)
 *
 * @param {Object} parsed A message of mailparser
 * @param {Boolean} [remoteImages] true lets images from the network load
 * @return {String} document
 */
export function previewDocument(parsed, remoteImages = false) {
    if (typeof parsed.html === 'string' && parsed.html) {
        return '<!doctype html><html><head>' + meta(remoteImages) + '</head><body>' + parsed.html + '</body></html>';
    }
    // mailparser escapes the text and turns links into anchors
    return document(parsed.textAsHtml || '');
}

/**
 * The message source as an HTML document
 *
 * @param {Buffer} raw Message source
 * @return {String} document
 */
export function sourceDocument(raw) {
    return document('<pre style="font:9pt Courier New,monospace">' + escapeHtml(raw.toString('utf8')) + '</pre>');
}

/**
 * Reads the header block of a message source, without looking at the body
 *
 * @param {Buffer} raw Message source
 * @return {Object} the decoded From, To, Subject, Date and Content-Type headers (the first of repeated headers)
 */
function readHeaders(raw) {
    let end = raw.indexOf('\r\n\r\n');
    if (end < 0) {
        end = raw.indexOf('\n\n');
    }
    // 8-bit headers are UTF-8 in practice
    const headers = libmime.decodeHeaders(raw.subarray(0, end < 0 ? raw.length : end).toString('utf8'));
    const value = key => (headers[key] ? libmime.decodeWords(headers[key][0]) : '');
    return { from: value('from'), to: value('to'), subject: value('subject'), date: value('date'), contentType: value('content-type') };
}

/**
 * @param {String|Date} value A date header, an IMAP internal date or a Date
 * @return {String|null} ISO date, null when it does not parse
 */
function isoDate(value) {
    const date = value ? new Date(value) : null;
    return date && !isNaN(date.getTime()) ? date.toISOString() : null;
}

/**
 * Short form of a message for the message list
 *
 * @param {Object} message A message of control.listMessages() with `raw`
 * @return {Object} summary
 */
export function summarize(message) {
    const headers = readHeaders(message.raw);
    const [sender] = addressparser(headers.from);
    return {
        uid: message.uid,
        flags: message.flags,
        size: message.size,
        from: headers.from,
        // the name of the sender, or the address when there is no name, as a mail list shows it
        fromName: (sender && (sender.name || sender.address)) || headers.from,
        to: headers.to,
        subject: headers.subject,
        // the date of the message, or when it arrived
        received: isoDate(headers.date) || isoDate(message.internaldate),
        // files are attached in multipart/mixed, the images an HTML message shows are in multipart/related. Read from
        // the headers alone, the list does not parse every message
        hasAttachments: /^\s*multipart\/mixed\b/i.test(headers.contentType)
    };
}

const addressText = value =>
    value
        ? []
              .concat(value)
              .map(address => address.text)
              .join(', ')
        : '';

/**
 * @param {Object} attachment An attachment of mailparser
 * @return {Boolean} true for an image that the HTML of the message shows, rather than an attached file
 */
const isEmbedded = attachment => !!(attachment.related || (attachment.contentDisposition === 'inline' && attachment.cid));

/**
 * The parts the preview lists, the attached files first and then the embedded images
 *
 * @param {Object} parsed A message of mailparser
 * @return {Array} attachments of mailparser
 */
function listAttachments(parsed) {
    const attachments = parsed.attachments || [];
    return attachments.filter(attachment => !isEmbedded(attachment)).concat(attachments.filter(isEmbedded));
}

/**
 * A file name of an attachment that is safe to write into a folder: no path, no characters Windows refuses
 *
 * @param {String} name File name the sender gave
 * @param {Number} index Position of the attachment, for a name when there is none
 * @return {String} file name
 */
export function safeFilename(name, index) {
    const clean = String(name || '')
        .replace(/^.*[\\/]/, '')
        .replace(/[<>:"|?*\x00-\x1f]/g, '_')
        .replace(/^[.\s]+|[.\s]+$/g, '');
    return clean || 'attachment-' + (index + 1);
}

/**
 * Parses a message for the preview pane
 *
 * @param {Object} message A message of control.getMessage()
 * @param {Boolean} [remoteImages] true lets images from the network load in the preview
 * @return {Promise<Object>} headers, text, attachment list and the preview document. `remoteImages` tells whether the
 *   HTML refers to images on the network, which the preview blocks unless they were asked for
 */
export async function parseMessage(message, remoteImages = false) {
    const parsed = await simpleParser(message.raw, { skipImageLinks: false });
    return {
        uid: message.uid,
        flags: message.flags,
        size: message.size,
        from: addressText(parsed.from),
        to: addressText(parsed.to),
        cc: addressText(parsed.cc),
        subject: parsed.subject || '',
        date: parsed.date ? parsed.date.toISOString() : null,
        text: parsed.text || '',
        attachments: listAttachments(parsed).map((attachment, index) => ({
            filename: safeFilename(attachment.filename, index),
            contentType: attachment.contentType,
            size: attachment.size,
            inline: isEmbedded(attachment)
        })),
        remoteImages: typeof parsed.html === 'string' && REMOTE_IMAGE.test(parsed.html),
        previewHtml: previewDocument(parsed, remoteImages)
    };
}

/**
 * Reads an attachment of a message
 *
 * @param {Object} message A message of control.getMessage()
 * @param {Number} index Position in the attachment list of parseMessage()
 * @return {Promise<Buffer>} the content
 */
export async function readAttachment(message, index) {
    const attachment = listAttachments(await simpleParser(message.raw))[index];
    if (!attachment) {
        throw new Error('The message has no attachment ' + index);
    }
    return attachment.content;
}

/**
 * Reads every attachment of a message, the embedded images too
 *
 * @param {Object} message A message of control.getMessage()
 * @return {Promise<Array>} `[{ filename, content }]` with safe and unique file names
 */
export async function readAttachments(message) {
    const used = new Set();
    return listAttachments(await simpleParser(message.raw)).map((attachment, index) => {
        let filename = safeFilename(attachment.filename, index);
        const dot = filename.lastIndexOf('.');
        const [base, ext] = dot > 0 ? [filename.slice(0, dot), filename.slice(dot)] : [filename, ''];
        for (let n = 2; used.has(filename.toLowerCase()); n++) {
            filename = base + ' (' + n + ')' + ext;
        }
        used.add(filename.toLowerCase());
        return { filename, content: attachment.content };
    });
}

/**
 * Builds a message source from the fields of the New Message form
 *
 * @param {Object} fields `{ from, to, cc, subject, text }`
 * @return {Promise<Buffer>} message source
 */
export function composeMessage(fields) {
    const mail = new MailComposer({
        from: fields.from,
        to: fields.to,
        cc: fields.cc || undefined,
        subject: fields.subject,
        text: fields.text,
        date: new Date()
    });
    return mail.compile().build();
}
