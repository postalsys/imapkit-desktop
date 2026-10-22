# ImapKit Desktop

A desktop mock mail server for testing email clients. It runs [ImapKit](https://imapkit.com) for IMAP and an SMTP server that delivers into the same mailboxes, and shows what clients do in a native GUI:

- **Server**: a status banner with Start/Stop and Apply Changes, where the server listens (IMAP with STARTTLS, IMAPS, SMTP), the test accounts, and the settings to copy into an email client. The advanced tools are on five tabs: **Extensions** (the IMAP capabilities to advertise), **Limits & ID** (largest message, quota, messages per command, largest literal, and the server's answer to ID instead of `ID NIL`), **Faults** (quirk presets that make it behave like a known server, and script rules for scripted faults), **SMTP** (require a login, a size limit, rejected recipients with the reply code to answer, in a table edited in place) and **REST API**. The File menu imports, exports and resets the mailboxes.
- **Connections**: IMAP and SMTP sessions, closed ones grayed out, and the protocol log of the selected session or of all of them (plaintext also over TLS and COMPRESS). Log lines can be filtered, copied (all of a line or the protocol data only) and saved; sessions can be disconnected and IMAP sessions sent raw data, like an `* OK [ALERT]`.
- **Messages**: an Outlook Express style mail view with the mailboxes under Local Folders and an Outbox. Mail that a client sends through the SMTP server after logging in lands in the Outbox, with its SMTP envelope (Bcc recipients included), and is delivered nowhere; mail without a login is incoming mail and goes to Inbox. The view has a folder tree, message list, headers and a sandboxed preview. Changes that clients make show up right away. Messages can be marked, flagged, moved, deleted, saved as `.eml`, imported from `.eml` files (also by drag and drop) and written with a New Mail form; attachments, embedded images included, can be saved one by one or all at once from the Attach line of the preview, quoted reply history and forwarded messages are folded behind a Show quoted text button (found with [@postalsys/email-content](https://github.com/postalsys/email-content)), pictures from the internet are blocked until the yellow bar above the message is clicked, and folders can be created, renamed and deleted.
- **Client**: a plaintext client like telnet or `openssl s_client`, for typing IMAP or SMTP commands by hand. It connects to any host and port, this app's servers are presets, with TLS from the start (any certificate is accepted, and the chain, verification result, protocol and cipher are shown) or in plaintext, where `STARTTLS` upgrades the connection once the server agrees. Typed lines are blue, server output black; Up and Down recall earlier lines and the session can be saved.

While the server listens, closing the window keeps ImapKit Desktop running in the notification area, like a service: click its icon to open the window, right-click it to start or stop the server or exit. File > Exit quits. Starting the app again opens the running copy.

The mailboxes are kept in memory by ImapKit and saved to `%APPDATA%\ImapKitDesktop`, so they survive restarts.

## Layout

| Path        | What                                                                                                                   |
| ----------- | ---------------------------------------------------------------------------------------------------------------------- |
| `sidecar/`  | Node.js process that runs ImapKit and the SMTP server, controlled over stdio, see [docs/protocol.md](docs/protocol.md) |
| `host-win/` | the Win32 GUI (C++, common controls without visual styles for the classic look, WebView2 for message bodies)           |
| `scripts/`  | build, run and screenshot helpers for the Windows build machine, and a smoke test client                               |

A macOS GUI can reuse the sidecar and its protocol as it is.

## Installing

Run `ImapKitDesktop-Setup.exe` from the [latest release](https://github.com/postalsys/imapkit-desktop/releases/latest/download/ImapKitDesktop-Setup.exe). It installs for the current user, no administrator rights needed, to `%LOCALAPPDATA%\Programs\ImapKit Desktop`, with a Start menu shortcut and an entry in Installed apps. The setup, the app and the uninstaller are signed by Postal Systems OÜ.

Running the setup of a newer version updates the installed one in place: a running copy is closed first (it saves its mailboxes), and the mailboxes and settings in `%APPDATA%\ImapKitDesktop` are kept. An older version asks before it replaces a newer one. `/S` installs silently; uninstalling asks whether to remove the mailboxes too (a silent uninstall keeps them).

## Building

On Windows with Visual Studio 2022 Build Tools (C++ workload) and Node.js 20 or newer:

```
scripts\win-build.cmd      :: npm install for the sidecar, then CMake + Ninja, gives build\ImapKitDesktop.exe
scripts\win-package.cmd    :: dist\ImapKitDesktop with the exe, the sidecar and a pinned, checksum-verified node.exe
```

The installer is built from the Mac with `scripts/build-installer.sh`: it builds and stages the app on the Windows build machine (`WIN_HOST` and `WIN_DIR`, see `scripts/win-env.sh`), copies the staging folder back, signs the exe with Azure Artifact Signing (`scripts/sign-win.sh`, the same setup as EmailEngine releases, enabled by `SIGN_WIN_ACCOUNT` and `SIGN_WIN_PROFILE`), and builds `dist/ImapKitDesktop-Setup.exe` with NSIS (`brew install makensis`), signing the uninstaller and the setup on the way. The version comes from `package.json`; `sidecar/package.json` has to match it.

`scripts/win-run.sh` starts a development build on the desktop of the build machine and `scripts/win-screenshot.sh out.png` takes a screenshot of it (`PAGE=0|1|2` picks the page).

CI builds the app, a portable zip and an unsigned installer on every push to master, they are the `ImapKitDesktop-win-x64` artifact of the CI run.

## Releasing

Releases follow EmailEngine's flow. Commits use Conventional Commit messages (`feat:`, `fix:` ...), and release-please keeps a release PR up to date on master (`release-please-config.json`, `.github/workflows/release.yaml`). Merging the PR bumps the version in `package.json` and `sidecar/package.json`, writes the changelog, pushes the `v<version>` tag and drafts the release.

Then, on the release Mac, check out the tag and run `scripts/release.sh`. It checks that CI passed for the tag, builds the installer on the Windows build machine, signs the exe, the uninstaller and the setup with Azure Artifact Signing, writes `hashes.txt` signed with the release GPG key, uploads both to the draft and publishes it. The setup has no version in its name, so https://github.com/postalsys/imapkit-desktop/releases/latest/download/ImapKitDesktop-Setup.exe always gets the latest release. `BUILD_ONLY=yes` builds and signs without uploading.

Help > Check for Updates in the app looks up the latest published release, and the installer of a newer version updates an installed one in place.

## Testing

```
cd sidecar && npm install && npm test
```

The tests run the sidecar over stdio and talk to it with Nodemailer and ImapFlow. `scripts/smoke-client.mjs` does the same against a running app.

## License

MIT
