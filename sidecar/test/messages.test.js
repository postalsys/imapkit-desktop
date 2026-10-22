// The preview documents: the Content-Security-Policy must come before anything of the message

import { describe, it } from 'node:test';
import assert from 'node:assert';
import MailComposer from 'nodemailer/lib/mail-composer/index.js';
import { previewDocument, parseMessage, safeFilename, summarize, findHistory } from '../src/messages.js';

describe('previewDocument', () => {
    // markup that could fool a search for the <head> of the message
    const tricks = [
        '<!--<head>--><html><head></head><body><img src="https://example.com/x.png"></body></html>',
        '<html><head title=">"><meta http-equiv="refresh" content="0;url=https://example.com"></head></html>',
        '<title><head></title><p>text</p>',
        '<header>Newsletter</header><p>text</p>'
    ];

    for (const html of tricks) {
        it('puts the policy before the message: ' + html.slice(0, 30), () => {
            const document = previewDocument({ html });
            const policy = document.indexOf('Content-Security-Policy');
            assert.ok(policy > 0 && policy < document.indexOf(html), 'the policy comes before the message');
            assert.ok(document.startsWith('<!doctype html><html><head><meta'), 'the policy is in the first head');
        });
    }

    it('escapes plain text messages', () => {
        assert.match(previewDocument({ textAsHtml: '<p>a &amp; b</p>' }), /<body><p>a &amp; b<\/p><\/body>/);
    });

    it('allows images from the network only when asked for', () => {
        const html = '<img src="https://example.com/x.png">';
        assert.match(previewDocument({ html }), /img-src data: cid:;/);
        assert.match(previewDocument({ html }, true), /img-src data: cid: http: https:;/);
    });
});

describe('parseMessage', () => {
    const message = html => ({ uid: 1, flags: [], size: 1, raw: Buffer.from('Content-Type: text/html\r\n\r\n' + html + '\r\n') });

    it('tells when the HTML refers to images on the network', async () => {
        assert.strictEqual((await parseMessage(message('<img src="https://example.com/x.png">'))).remoteImages, true);
        assert.strictEqual((await parseMessage(message('<td style="background:url(//example.com/bg.png)">'))).remoteImages, true);
        assert.strictEqual((await parseMessage(message('<a href="https://example.com">link</a>'))).remoteImages, false);
    });
});

describe('attachments', () => {
    const pixel = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg==', 'base64');
    const build = options => new MailComposer(Object.assign({ subject: 'test' }, options)).compile().build();

    it('lists embedded images after the attached files, marked as inline', async () => {
        const raw = await build({
            html: '<img src="cid:logo">',
            attachments: [
                { filename: 'logo.png', content: pixel, cid: 'logo' },
                { filename: 'report.csv', content: 'a,b\n' }
            ]
        });
        const parsed = await parseMessage({ uid: 1, flags: [], size: raw.length, raw });
        assert.deepStrictEqual(
            parsed.attachments.map(attachment => [attachment.filename, attachment.inline]),
            [
                ['report.csv', false],
                ['logo.png', true]
            ]
        );
    });

    it('marks messages with attached files for the list, not those with embedded images only', async () => {
        const files = await build({ text: 'x', attachments: [{ filename: 'a.txt', content: 'a' }] });
        const images = await build({ html: '<img src="cid:logo">', attachments: [{ filename: 'logo.png', content: pixel, cid: 'logo' }] });
        assert.strictEqual(summarize({ uid: 1, flags: [], size: 1, raw: files }).hasAttachments, true);
        assert.strictEqual(summarize({ uid: 2, flags: [], size: 1, raw: images }).hasAttachments, false);
    });

    it('keeps file names inside the folder they are saved to', () => {
        assert.strictEqual(safeFilename('..\\..\\Windows\\evil.exe', 0), 'evil.exe');
        assert.strictEqual(safeFilename('../../etc/passwd', 0), 'passwd');
        assert.strictEqual(safeFilename('a:b*c?.txt', 0), 'a_b_c_.txt');
        assert.strictEqual(safeFilename('..', 2), 'attachment-3');
        assert.strictEqual(safeFilename(undefined, 0), 'attachment-1');
    });
});

describe('reply history', () => {
    it('folds the quote of a Gmail reply, in its original markup', async () => {
        const html =
            '<div dir="ltr">See you Monday.</div><div class="gmail_quote"><div class="gmail_attr">On Thu, Oct 22, 2026 Bob &lt;bob@example.com&gt; wrote:<br></div>' +
            '<blockquote class="gmail_quote" style="border-left:1px solid #ccc">Can we meet?</blockquote></div>';
        const fold = await findHistory({ html, subject: 'Re: Meeting' });
        assert.strictEqual(fold.visible, '<div dir="ltr">See you Monday.</div>');
        assert.match(fold.hidden, /^<div class="gmail_quote">.*<blockquote class="gmail_quote" style="border-left:1px solid #ccc">/);
        assert.strictEqual(fold.label, 'Show quoted text');

        const document = previewDocument({ html }, false, fold);
        assert.ok(document.indexOf('Content-Security-Policy') < document.indexOf('See you Monday'), 'the policy still comes first');
        assert.match(
            document,
            /See you Monday\.<\/div><details class="imapkit-desktop-history"><summary>&#183;&#183;&#183; Show quoted text<\/summary><div class="gmail_quote">/
        );
    });

    it('folds a forwarded message and plain text history', async () => {
        const forward = await findHistory({
            text: 'FYI\n\n---------- Forwarded message ---------\nFrom: Carol <c@example.com>\nDate: Mon\nSubject: Plan\nTo: me\n\nThe plan.\n'
        });
        assert.deepStrictEqual([forward.visible, forward.label], ['FYI\n\n', 'Show forwarded message']);
        const reply = await findHistory({ text: 'Thanks!\n\n-----Original Message-----\nFrom: Bob\nSent: Monday\nSubject: x\n\nOld text\n' });
        assert.strictEqual(reply.visible, 'Thanks!\n\n');
        assert.match(previewDocument({ text: 'x' }, false, reply), /<pre[^>]*>Thanks!\n\n<\/pre><details/);
    });

    it('leaves messages without history alone', async () => {
        assert.strictEqual(await findHistory({ text: 'Just a message.\nNothing quoted.\n' }), null);
        assert.strictEqual(await findHistory({ html: '<p>Hello</p>' }), null);
    });
});
