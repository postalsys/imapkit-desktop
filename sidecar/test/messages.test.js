// The preview documents: the Content-Security-Policy must come before anything of the message

import { describe, it } from 'node:test';
import assert from 'node:assert';
import { previewDocument, parseMessage } from '../src/messages.js';

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
