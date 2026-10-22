// Writes the license files of the package into the staging folder (scripts\win-package.cmd runs it on Windows):
//
//   LICENSE.txt                the license of ImapKit Desktop, also shown by the installer, pointing to the notices
//   THIRD-PARTY-NOTICES.txt    the license texts of everything that ships with it: Node.js (with the libraries built
//                              into node.exe), nlohmann/json and the WebView2 loader compiled into the exe, and every
//                              npm package of the sidecar
//
//   node scripts/notices.mjs <staging folder> <Node.js version, e.g. v24.22.1> <CMake build folder>

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const [staging, nodeVersion, buildDir] = process.argv.slice(2);
if (!staging || !nodeVersion || !buildDir) {
    console.error('usage: node scripts/notices.mjs <staging folder> <Node.js version> <build folder>');
    process.exit(1);
}

const LICENSE_FILE = /^(licen[cs]e|copying|notice)(\.|-|$)/i;

// standard texts for packages that name their license in package.json without shipping the file
const TEMPLATES = {
    MIT: holder =>
        `Copyright (c) ${holder}\n\nPermission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:\n\nThe above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.\n\nTHE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.`,
    ISC: holder =>
        `Copyright (c) ${holder}\n\nPermission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted, provided that the above copyright notice and this permission notice appear in all copies.\n\nTHE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.`
};

const read = file => fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n').trim();

function section(title, text) {
    return `${'='.repeat(100)}\n${title}\n${'='.repeat(100)}\n\n${text}\n\n`;
}

// the license of Node.js at the bundled version, it covers V8, OpenSSL, ICU, libuv and the rest of node.exe
async function nodeLicense() {
    const cache = path.join(buildDir, `node-${nodeVersion}-LICENSE.txt`);
    if (!fs.existsSync(cache)) {
        const res = await fetch(`https://raw.githubusercontent.com/nodejs/node/${nodeVersion}/LICENSE`);
        if (!res.ok) {
            throw new Error(`The Node.js ${nodeVersion} license could not be downloaded: ${res.status}`);
        }
        fs.writeFileSync(cache, await res.text());
    }
    return read(cache);
}

// every package folder under node_modules, scoped and nested ones too
function packages(dir) {
    const found = [];
    if (!fs.existsSync(dir)) {
        return found;
    }
    for (const name of fs.readdirSync(dir)) {
        const folder = path.join(dir, name);
        if (name.startsWith('@')) {
            found.push(...packages(folder));
        } else if (fs.existsSync(path.join(folder, 'package.json'))) {
            found.push(folder, ...packages(path.join(folder, 'node_modules')));
        }
    }
    return found;
}

function packageNotice(folder) {
    const pkg = JSON.parse(fs.readFileSync(path.join(folder, 'package.json'), 'utf8'));
    const license = typeof pkg.license === 'string' ? pkg.license : JSON.stringify(pkg.license || pkg.licenses || 'unknown');
    // a choice like "(MIT OR EUPL-1.1+)" is taken as its first license, the files of the others are left out
    const others = license
        .replace(/[()]/g, '')
        .split(/\s+OR\s+/)
        .slice(1)
        .map(name => name.replace(/[-+].*$/, '').toLowerCase());
    const files = fs
        .readdirSync(folder)
        .filter(name => LICENSE_FILE.test(name) && fs.statSync(path.join(folder, name)).isFile())
        .filter(name => !others.some(other => name.toLowerCase().includes(other)))
        .sort();
    let text = files.map(name => read(path.join(folder, name))).join('\n\n');
    if (!text) {
        const author = typeof pkg.author === 'string' ? pkg.author : pkg.author && pkg.author.name;
        const template = TEMPLATES[license];
        if (!template) {
            throw new Error(`${pkg.name} has no license file and no template for ${license}`);
        }
        text = template(author || `the ${pkg.name} authors`);
    }
    return { name: pkg.name, title: `${pkg.name} ${pkg.version} (${license.replace(/^\((.*)\)$/, '$1')})`, text };
}

function webview2Dir() {
    const name = fs.readdirSync(buildDir).find(entry => entry.startsWith('webview2-') && fs.statSync(path.join(buildDir, entry)).isDirectory());
    if (!name) {
        throw new Error('The WebView2 SDK is not in ' + buildDir + ', build the app first');
    }
    return path.join(buildDir, name);
}

const webview2 = webview2Dir();
const npm = packages(path.join(staging, 'sidecar', 'node_modules'))
    .map(packageNotice)
    .sort((a, b) => a.name.localeCompare(b.name));

let notices =
    'ImapKit Desktop includes the following third-party software. Their licenses are reproduced here as they require.\n\n' +
    section(`Node.js ${nodeVersion} (node\\node.exe), with the libraries built into it`, await nodeLicense()) +
    section('JSON for Modern C++ (nlohmann/json), compiled into ImapKitDesktop.exe', read(path.join(buildDir, '_deps', 'json-src', 'LICENSE.MIT'))) +
    section(
        `Microsoft Edge WebView2 SDK loader (${path.basename(webview2)}), compiled into ImapKitDesktop.exe`,
        read(path.join(webview2, 'LICENSE.txt')) + '\n\n' + read(path.join(webview2, 'NOTICE.txt'))
    );
for (const entry of npm) {
    notices += section(`${entry.title}, in sidecar\\node_modules`, entry.text);
}
fs.writeFileSync(path.join(staging, 'THIRD-PARTY-NOTICES.txt'), notices.replace(/\n/g, '\r\n'));

const license =
    read(path.join(root, 'LICENSE')) +
    '\n\nImapKit Desktop includes third-party software: Node.js, nlohmann/json, the Microsoft WebView2 loader and the npm ' +
    'packages of its sidecar, each under its own permissive license. Their license texts are in THIRD-PARTY-NOTICES.txt ' +
    'in the installation folder, and Help > Third-Party Notices opens it.\n';
fs.writeFileSync(path.join(staging, 'LICENSE.txt'), license.replace(/\n/g, '\r\n'));

console.log(`THIRD-PARTY-NOTICES.txt with Node.js, nlohmann/json, WebView2 and ${npm.length} npm packages`);
