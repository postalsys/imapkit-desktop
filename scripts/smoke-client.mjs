// Exercises a running ImapKit Desktop like a real mail client would: sends mail over SMTP, then reads and flags it
// over IMAP with STARTTLS. Needs nodemailer and imapflow: npm install nodemailer imapflow
//
//   node smoke-client.mjs [host] [imapPort] [smtpPort]

import nodemailer from 'nodemailer';
import { ImapFlow } from 'imapflow';

const [host = '127.0.0.1', imapPort = '1143', smtpPort = '1025'] = process.argv.slice(2);

// a 1x1 red PNG for the inline image
const pixel = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg==', 'base64');

const transport = nodemailer.createTransport({ host, port: Number(smtpPort), secure: false, tls: { rejectUnauthorized: false } });
await transport.sendMail({
    from: '"Alice Example" <alice@example.com>',
    to: '"Test User" <testuser@imapkit.local>',
    subject: 'Plain text greeting',
    text: 'Hi!\n\nThis one is plain text.\n\nAlice'
});
await transport.sendMail({
    from: '"Bob Builder" <bob@example.com>',
    to: '"Test User" <testuser@imapkit.local>',
    cc: 'carol@example.com',
    subject: 'Quarterly report with an attachment',
    html:
        '<h2 style="color:#036">Quarterly report</h2><p>Numbers are <b>up</b>. <img src="cid:pixel" width="40" height="40"> ' +
        '<img src="https://example.com/tracker.png" alt="remote image (blocked)"></p><p><a href="https://imapkit.com">imapkit.com</a></p>',
    attachments: [
        { filename: 'pixel.png', content: pixel, cid: 'pixel' },
        { filename: 'report.csv', content: 'quarter,revenue\nQ3,100\n' }
    ]
});
console.log('sent 2 messages over SMTP');

const client = new ImapFlow({
    host,
    port: Number(imapPort),
    secure: false,
    tls: { rejectUnauthorized: false },
    auth: { user: 'testuser', pass: 'testpass' },
    logger: false
});
await client.connect();
const lock = await client.getMailboxLock('INBOX');
try {
    for await (const message of client.fetch('1:*', { envelope: true, flags: true })) {
        console.log(message.uid, [...message.flags].join(' ') || '-', message.envelope.subject);
    }
    // flag the newest message, the GUI shows it with "!"
    await client.messageFlagsAdd('*', ['\\Flagged']);
} finally {
    lock.release();
}
await client.logout();
console.log('IMAP session done');
