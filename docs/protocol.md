# Sidecar protocol

The native GUI starts `node sidecar/src/index.js [--data-dir <dir>] [--no-autostart]` and talks to it with newline-delimited JSON over stdio. Every GUI (the Win32 host now, a macOS host later) speaks this protocol, so the mail server logic lives only in the sidecar.

```
GUI -> sidecar   {"id": 1, "method": "start", "params": {...}}
sidecar -> GUI   {"id": 1, "result": {...}}
sidecar -> GUI   {"id": 1, "error": {"message": "...", "code": "NONEXISTENT"}}
sidecar -> GUI   {"event": "session", "data": {...}}
```

The sidecar starts the servers with the saved settings right away, `--no-autostart` leaves that to a `start` call. Closing the sidecar's stdin stops the servers, saves the mailboxes and ends the process. Settings are kept in `config.json` and the mailboxes in `storage.json` of the data directory (`%APPDATA%\ImapKitDesktop`, or `~/.imapkit-desktop` elsewhere).

## Methods

| Method           | Params                                           | Result                                                                                                                                                                         |
| ---------------- | ------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `hello`          |                                                  | `{ version, dataDir, config, plugins: [{ name, help }], quirks: [{ name, description }], status }`                                                                             |
| `getConfig`      |                                                  | the settings                                                                                                                                                                   |
| `start`          | `{ config? }`                                    | status. A given config is saved, also when the start fails. Restarting keeps the mailboxes, new plugins and users take effect. At least one user is required                   |
| `stop`           |                                                  | status                                                                                                                                                                         |
| `status`         |                                                  | `{ running, host, imapPort, imapsPort, smtpPort, restPort, restUrl? }`                                                                                                         |
| `sessions`       |                                                  | connected sessions, as in the `session` event                                                                                                                                  |
| `disconnect`     | `{ id }`                                         | closes a session (IMAP with BYE, SMTP with 421)                                                                                                                                |
| `inject`         | `{ id, data }`                                   | writes data to an IMAP session as it is (CRLF added), e.g. `* OK [ALERT] ...`                                                                                                  |
| `mailboxes`      |                                                  | `[{ path, name, parent, delimiter, messages, unseen, specialUse, selectable, canRename, canDelete }]`, also while stopped. `name` is the display name (Inbox for INBOX)        |
| `messages`       | `{ path }`                                       | `[{ uid, flags, size, from, fromName, to, subject, received }]`, `fromName` is the display name of the sender, `received` the ISO date of the Date header or the internal date |
| `message`        | `{ path, uid, markSeen? }`                       | `{ uid, flags, from, to, cc, subject, date, text, attachments, previewHtml }`                                                                                                  |
| `setFlags`       | `{ path, uids, flags, mode }`                    | `mode` is `set`, `add` or `remove`                                                                                                                                             |
| `deleteMessages` | `{ path, uids }`                                 | moves to the `\Trash` mailbox, or expunges when they are in Trash already                                                                                                      |
| `addMessage`     | `{ path, raw, encoding? }`                       | `{ uid, uidvalidity }`, `encoding: "base64"` for octets                                                                                                                        |
| `moveMessages`   | `{ path, uids, target }`                         | `{ uidvalidity, uids: [{ uid, targetUid }] }`                                                                                                                                  |
| `importMessages` | `{ path, files }`                                | adds `.eml` files to a mailbox, the new UIDs                                                                                                                                   |
| `composeMessage` | `{ path, from, to, cc?, subject, text, flags? }` | builds a message and adds it, `{ uid, uidvalidity }`                                                                                                                           |
| `messageSource`  | `{ path, uid }`                                  | `{ sourceHtml }`, the message source as a document for the preview                                                                                                             |
| `saveMessage`    | `{ path, uid, file }`                            | writes the message source to a file                                                                                                                                            |
| `saveAttachment` | `{ path, uid, index, file }`                     | writes an attachment (its position in `message().attachments`) to a file                                                                                                       |
| `createMailbox`  | `{ parent, name }` or `{ path }`                 | creates a mailbox (and its missing parents), subscribed. A folder in INBOX goes to the top level                                                                               |
| `renameMailbox`  | `{ path, name }` or `{ path, newPath }`          | `{ path }`, the new path                                                                                                                                                       |
| `deleteMailbox`  | `{ path }`                                       |                                                                                                                                                                                |
| `exportStorage`  | `{ file }`                                       | writes the mailboxes as JSON in the shape of the imapkit `storage` option                                                                                                      |
| `importStorage`  | `{ file }`                                       | checks the file with imapkit's `validateStorage()` and replaces every mailbox with it                                                                                          |
| `readScript`     | `{ file }`                                       | reads and checks imapkit script rules (a rule or a list), returns them for `config.script`                                                                                     |
| `reset`          |                                                  | replaces the mailboxes with the default ones                                                                                                                                   |

The settings are `{ host, imapPort, imapsPort, smtpPort, users: [{ username, password }], plugins: [names], quirks: [names], script: [rules], rest: { enabled, port, host, token } }`. `imapsPort` or `smtpPort` 0 turns that listener off. `quirks` are imapkit quirk presets, `script` imapkit script rules in their JSON form, and `rest` the imapkit REST API, which listens on 127.0.0.1 unless `rest.host` says otherwise (imapkit then requires a token).

`previewHtml` and `sourceHtml` are complete documents with a Content-Security-Policy that allows only embedded (`data:`) images and styles, so the GUI can show them in a web view without network access.

## Events

| Event     | Data                                                                                                                                                                                                                                                                        |
| --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ready`   | `{ version, dataDir }`, once at startup                                                                                                                                                                                                                                     |
| `status`  | as the `status` method, with `error` when starting failed                                                                                                                                                                                                                   |
| `session` | `{ type, id, number, kind, user, state, mailbox, secure, remoteAddress, started }`, `type` is `open`, `login`, `select`, `update`, `close`... `id` (`imap-<n>` or `smtp-<n>`) addresses the session in `disconnect` and `inject`, `number` is the session number            |
| `trace`   | `{ id, direction, text, time }`: protocol traffic of a session, `in` from the client. Long chunks are cut at 8 KB                                                                                                                                                           |
| `store`   | `{ type, path, oldPath?, messages? }` when mailboxes or messages change (`append`, `expunge`, `flags`, `mailbox`, `reset`), to refresh views. `oldPath` is the old name of a renamed mailbox, `messages` (`[{ uid, flags }]`) are the new flags of a flag change when known |
| `log`     | `{ level, message }`                                                                                                                                                                                                                                                        |
