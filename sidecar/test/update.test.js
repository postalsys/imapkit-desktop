// The answer of Help > Check for Updates

import { describe, it } from 'node:test';
import assert from 'node:assert';
import { compareVersions, releaseUpdate } from '../src/update.js';

describe('check for updates', () => {
    const release = {
        tag_name: 'v0.3.0',
        html_url: 'https://github.com/postalsys/imapkit-desktop/releases/tag/v0.3.0',
        assets: [
            { name: 'hashes.txt', browser_download_url: 'https://example.com/hashes.txt' },
            { name: 'ImapKitDesktop-Setup.exe', browser_download_url: 'https://example.com/ImapKitDesktop-Setup.exe' }
        ]
    };

    it('offers the installer of a newer release for download', () => {
        assert.deepStrictEqual(releaseUpdate(release, '0.2.0'), {
            current: '0.2.0',
            latest: '0.3.0',
            newer: true,
            url: release.html_url,
            download: 'https://example.com/ImapKitDesktop-Setup.exe'
        });
        assert.strictEqual(releaseUpdate(release, '0.3.0').newer, false);
    });

    it('falls back to the release page without an installer, and handles no release at all', () => {
        assert.strictEqual(releaseUpdate({ ...release, assets: [] }, '0.2.0').download, release.html_url);
        assert.deepStrictEqual(releaseUpdate(null, '0.2.0'), { current: '0.2.0', latest: null, newer: false, url: null, download: null });
    });

    it('compares versions by number', () => {
        assert.ok(compareVersions('0.10.0', '0.9.1') > 0);
        assert.strictEqual(compareVersions('1.0', '1.0.0'), 0);
    });
});
