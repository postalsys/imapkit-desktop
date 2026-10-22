// Starts the sidecar the way the GUI does and talks to it over stdio

import { spawn } from 'node:child_process';
import net from 'node:net';
import path from 'node:path';
import readline from 'node:readline';
import { fileURLToPath } from 'node:url';

const entry = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'src', 'index.js');

export function freePort() {
    return new Promise(resolve => {
        const server = net.createServer().listen(0, '127.0.0.1', () => {
            const { port } = server.address();
            server.close(() => resolve(port));
        });
    });
}

/**
 * Starts the sidecar, returns `call(method, params)`, the received events and `waitFor(predicate)`
 */
export function startSidecar(dataDir) {
    const child = spawn(process.execPath, [entry, '--data-dir', dataDir, '--no-autostart'], { stdio: ['pipe', 'pipe', 'inherit'] });
    const pending = new Map();
    const events = [];
    const waiters = [];
    let nextId = 0;
    readline.createInterface({ input: child.stdout }).on('line', line => {
        const message = JSON.parse(line);
        if (message.event) {
            events.push(message);
            waiters.slice().forEach(waiter => waiter());
        } else if (pending.has(message.id)) {
            const { resolve, reject } = pending.get(message.id);
            pending.delete(message.id);
            message.error ? reject(new Error(message.error.message)) : resolve(message.result);
        }
    });
    const call = (method, params) =>
        new Promise((resolve, reject) => {
            const id = ++nextId;
            pending.set(id, { resolve, reject });
            child.stdin.write(JSON.stringify({ id, method, params }) + '\n');
        });
    const waitFor = predicate =>
        new Promise(resolve => {
            const check = () => {
                const found = events.find(predicate);
                if (found) {
                    waiters.splice(waiters.indexOf(check), 1);
                    resolve(found);
                }
            };
            waiters.push(check);
            check();
        });
    return { child, call, waitFor };
}
