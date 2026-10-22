// The preview documents: the Content-Security-Policy must come before anything of the message

import { describe, it } from 'node:test';
import assert from 'node:assert';
import { previewDocument } from '../src/messages.js';

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
});
