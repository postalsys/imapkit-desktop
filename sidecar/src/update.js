// What Help > Check for Updates tells: whether the latest GitHub release is newer, and where its installer is

// the installer asset of a release, without a version in its name
export const SETUP_NAME = 'ImapKitDesktop-Setup.exe';

/**
 * Compares two x.y.z versions
 *
 * @return {Number} positive when a is newer, negative when b is newer, 0 when equal
 */
export function compareVersions(a, b) {
    const parts = version => version.split('.').map(part => parseInt(part, 10) || 0);
    const [x, y] = [parts(a), parts(b)];
    for (let i = 0; i < 3; i++) {
        if ((x[i] || 0) !== (y[i] || 0)) {
            return (x[i] || 0) - (y[i] || 0);
        }
    }
    return 0;
}

/**
 * The update answer for the latest release of the GitHub API, null when there is no release yet
 *
 * @param {Object|null} release The release object of the GitHub API
 * @param {String} current The running version
 * @return {Object} `{ current, latest, newer, url, download }`: `url` is the release page, `download` the installer
 * itself (the release page when the release has no installer)
 */
export function releaseUpdate(release, current) {
    if (!release) {
        return { current, latest: null, newer: false, url: null, download: null };
    }
    const latest = String(release.tag_name || '').replace(/^v/, '');
    const setup = (release.assets || []).find(asset => asset.name === SETUP_NAME);
    return {
        current,
        latest,
        newer: compareVersions(latest, current) > 0,
        url: release.html_url,
        download: setup ? setup.browser_download_url : release.html_url
    };
}
