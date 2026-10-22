// Writes the demo data of the documentation screenshots into scripts/screenshots/demo: config.json, storage.json (the
// mailboxes in the imapkit storage format) and outbox.json, in the files the app keeps in its data directory. Every
// message has a fixed date, UID and flags, so the screenshots look the same on every run. The output is committed,
// run this only to change the demo mail:
//
//   node scripts/screenshots/make-demo.mjs

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
import { DEFAULT_CONFIG, defaultStorage } from '../../sidecar/src/defaults.js';

// nodemailer of the sidecar, resolved through its package exports
const sidecarRequire = createRequire(new URL('../../sidecar/package.json', import.meta.url));
const { default: MailComposer } = await import(pathToFileURL(sidecarRequire.resolve('nodemailer/lib/mail-composer/index.js')));

const dir = path.join(path.dirname(fileURLToPath(import.meta.url)), 'demo');
const ME = '"Test User" <testuser@imapkit.local>';

function build(mail) {
    return new Promise((resolve, reject) => new MailComposer(mail).compile().build((err, message) => (err ? reject(err) : resolve(message.toString()))));
}

// "2026-10-19T09:12:00+03:00" to the INTERNALDATE form imapkit stores, "19-Oct-2026 09:12:00 +0300"
function internaldate(iso) {
    const [, y, m, d, time, sign, zh, zm] = /^(\d+)-(\d+)-(\d+)T([\d:]+)([+-])(\d+):(\d+)$/.exec(iso);
    const month = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'][Number(m) - 1];
    return `${d}-${month}-${y} ${time} ${sign}${zh}${zm}`;
}

const pdf = Buffer.from(
    '%PDF-1.4\n1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj 2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj 3 0 obj<</Type/Page/Parent 2 0 R/MediaBox[0 0 612 792]>>endobj\ntrailer<</Root 1 0 R>>\n%%EOF\n'
);

// oldest first; the list shows the newest first, Maria Lopez on top (the quoted text screenshot selects it with Home)
// and the Northwind order second from the bottom (the main screenshot selects it with End and Up), so selecting them
// never passes an unread message
const inbox = [
    {
        date: '2026-10-18T08:00:00+03:00',
        flags: ['\\Seen'],
        mail: {
            from: '"ImapKit Desktop" <mock@imapkit.local>',
            subject: 'Welcome to ImapKit Desktop',
            html: '<p>This mailbox lives in the memory of <b>ImapKit Desktop</b>.</p><p>Point an IMAP client to the IMAP port, or send mail to the SMTP port, and watch the sessions in the Connections view.</p>'
        }
    },
    {
        date: '2026-10-19T09:12:00+03:00',
        flags: ['\\Seen'],
        mail: {
            from: '"Northwind Store" <orders@northwind.example>',
            subject: 'Your order #48213 has shipped',
            html: `<div style="font-family:Segoe UI,Arial,sans-serif;max-width:560px">
<h2 style="color:#1f5c99;margin:0 0 12px">Good news, your order is on its way</h2>
<p>Hi Test User,</p><p>Order <b>#48213</b> left our warehouse this morning and should arrive on <b>Friday</b>.</p>
<table cellpadding="6" style="border-collapse:collapse;width:100%;font-size:14px">
<tr style="background:#eef3f8"><th align="left">Item</th><th align="right">Qty</th><th align="right">Price</th></tr>
<tr><td>Mechanical keyboard</td><td align="right">1</td><td align="right">$89.00</td></tr>
<tr><td>USB-C cable, 2 m</td><td align="right">2</td><td align="right">$18.00</td></tr>
<tr style="border-top:1px solid #ccc"><td><b>Total</b></td><td></td><td align="right"><b>$107.00</b></td></tr>
</table>
<p><a href="https://northwind.example/track/48213">Track your parcel</a></p>
<p style="color:#777;font-size:12px">Northwind Store, 1 Example Street</p></div>`
        }
    },
    {
        date: '2026-10-20T14:40:00+03:00',
        flags: ['\\Seen'],
        mail: {
            from: '"Billing" <billing@acme-hosting.example>',
            subject: 'Invoice INV-2026-0142',
            text: 'Hello,\n\nPlease find attached the invoice for October.\n\nAmount due: EUR 49.00\nDue date: 2026-11-05\n\nThank you,\nACME Hosting',
            attachments: [{ filename: 'INV-2026-0142.pdf', content: pdf, contentType: 'application/pdf' }]
        }
    },
    {
        date: '2026-10-21T08:03:00+03:00',
        flags: ['\\Seen'],
        mail: {
            from: '"CI" <ci@builds.example>',
            subject: 'Build failed: main #1287',
            text: 'Build #1287 of main failed.\n\nStep: test\nFailing test: sync > resumes after IDLE timeout\n\nhttps://builds.example/main/1287'
        }
    },
    {
        date: '2026-10-21T16:25:00+03:00',
        flags: [],
        mail: {
            from: '"Mail Weekly" <news@mailweekly.example>',
            subject: 'Mail Weekly: five IMAP extensions worth knowing',
            html: `<div style="font-family:Georgia,serif;max-width:560px">
<img src="https://mailweekly.example/banner.png" width="560" height="120" alt="Mail Weekly">
<h2>Five IMAP extensions worth knowing</h2>
<ol><li><b>IDLE</b> for push updates</li><li><b>CONDSTORE</b> and <b>QRESYNC</b> for fast resync</li>
<li><b>MOVE</b> instead of COPY and EXPUNGE</li><li><b>SPECIAL-USE</b> to find Sent and Trash</li><li><b>LITERAL+</b> to save round trips</li></ol>
<p>Read the full issue on our site.</p></div>`
        }
    },
    {
        date: '2026-10-22T09:40:00+03:00',
        flags: [],
        mail: {
            from: '"My App" <no-reply@myapp.example>',
            subject: 'Reset your password',
            html: '<p>Someone asked to reset the password of your account.</p><p><a href="https://myapp.example/reset?token=abc123">Choose a new password</a></p><p>If it was not you, ignore this message.</p>'
        }
    },
    {
        date: '2026-10-22T10:18:00+03:00',
        flags: ['\\Flagged'],
        mail: {
            from: '"Maria Lopez" <maria@contoso.example>',
            subject: 'Re: Q4 planning meeting',
            text:
                'Thursday at 14:00 works for me. I booked the small meeting room.\n\nMaria\n\n' +
                'On Tue, Oct 20, 2026 at 11:02, Test User <testuser@imapkit.local> wrote:\n' +
                '> Hi Maria,\n>\n> could we meet this week to go over the Q4 plan?\n> Thursday or Friday afternoon would suit me.\n>\n> Thanks!\n'
        }
    }
];

const storage = defaultStorage();
storage.INBOX = { uidvalidity: 1, messages: [] };
for (const [index, entry] of inbox.entries()) {
    const raw = await build(Object.assign({ to: ME, date: new Date(entry.date), messageId: `<demo-${index + 1}@imapkit.local>` }, entry.mail));
    storage.INBOX.messages.push({ uid: index + 1, raw, flags: entry.flags, internaldate: internaldate(entry.date) });
}

// a message a client sent with an SMTP login, kept in the Outbox
const sent = await build({
    from: ME,
    to: '"Maria Lopez" <maria@contoso.example>',
    subject: 'Re: Q4 planning meeting',
    date: new Date('2026-10-22T10:31:00+03:00'),
    messageId: '<demo-sent-1@imapkit.local>',
    text: 'Great, see you on Thursday!\n'
});
const outbox = [
    {
        uid: 1,
        raw: Buffer.from(sent).toString('base64'),
        envelope: { from: 'testuser@imapkit.local', to: ['maria@contoso.example', 'archive@contoso.example'] },
        user: 'testuser',
        internaldate: '2026-10-22T07:31:00.000Z',
        flags: []
    }
];

fs.mkdirSync(dir, { recursive: true });
// the default settings, with a rejected recipient for the SMTP tab
const config = Object.assign({}, DEFAULT_CONFIG, {
    smtp: Object.assign({}, DEFAULT_CONFIG.smtp, { rejectRecipients: [{ address: '*@blocked.test', code: 550, message: 'No such user' }] })
});
fs.writeFileSync(path.join(dir, 'config.json'), JSON.stringify(config, null, 2) + '\n');
fs.writeFileSync(path.join(dir, 'storage.json'), JSON.stringify(storage, null, 2) + '\n');
fs.writeFileSync(path.join(dir, 'outbox.json'), JSON.stringify(outbox, null, 2) + '\n');
console.log('Wrote ' + dir);
