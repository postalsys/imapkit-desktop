# ImapKit Desktop

A desktop mock mail server for testing email clients. It runs [ImapKit](https://imapkit.com) for IMAP and an SMTP server that delivers into the same mailboxes, and shows what clients do in a native GUI:

- **Server**: listener ports (IMAP with STARTTLS, IMAPS, SMTP), test accounts, the ImapKit extensions to advertise, quirk presets that make it behave like a known server, script rules for scripted faults, the ImapKit REST API, and export and import of the mailboxes.
- **Connections**: IMAP and SMTP sessions with the protocol log of each one (plaintext also over TLS and COMPRESS). Log lines can be filtered, copied (all of a line or the protocol data only) and saved; sessions can be disconnected and IMAP sessions sent raw data, like an `* OK [ALERT]`.
- **Messages**: an Outlook Express style mail view: folders, message list, headers and a sandboxed preview. Changes that clients make show up right away. Messages can be marked, flagged, moved, deleted, saved as `.eml`, imported from `.eml` files (also by drag and drop) and written with a New Mail form; attachments, embedded images included, can be saved one by one or all at once from the Attach line of the preview, quoted reply history and forwarded messages are folded behind a Show quoted text button (found with [@postalsys/email-content](https://github.com/postalsys/email-content)), pictures from the internet are blocked until the yellow bar above the message is clicked, and folders can be created, renamed and deleted.

The mailboxes are kept in memory by ImapKit and saved to `%APPDATA%\ImapKitDesktop`, so they survive restarts.

## Layout

| Path        | What                                                                                                                   |
| ----------- | ---------------------------------------------------------------------------------------------------------------------- |
| `sidecar/`  | Node.js process that runs ImapKit and the SMTP server, controlled over stdio, see [docs/protocol.md](docs/protocol.md) |
| `host-win/` | the Win32 GUI (C++, common controls without visual styles for the classic look, WebView2 for message bodies)           |
| `scripts/`  | build, run and screenshot helpers for the Windows build machine, and a smoke test client                               |

A macOS GUI can reuse the sidecar and its protocol as it is.

## Installing

Run `ImapKitDesktop-Setup-<version>.exe`. It installs for the current user, no administrator rights needed, to `%LOCALAPPDATA%\Programs\ImapKit Desktop`, with a Start menu shortcut and an entry in Installed apps. The setup, the app and the uninstaller are signed by Postal Systems OÜ.

Running the setup of a newer version updates the installed one in place: a running copy is closed first (it saves its mailboxes), and the mailboxes and settings in `%APPDATA%\ImapKitDesktop` are kept. An older version asks before it replaces a newer one. `/S` installs silently; uninstalling asks whether to remove the mailboxes too (a silent uninstall keeps them).

## Building

On Windows with Visual Studio 2022 Build Tools (C++ workload) and Node.js 20 or newer:

```
scripts\win-build.cmd      :: npm install for the sidecar, then CMake + Ninja, gives build\ImapKitDesktop.exe
scripts\win-package.cmd    :: dist\ImapKitDesktop with the exe, the sidecar and a pinned, checksum-verified node.exe
```

The installer is built from the Mac with `scripts/build-installer.sh`: it builds and stages the app on the Windows build machine (`WIN_HOST` and `WIN_DIR`, see `scripts/win-env.sh`), copies the staging folder back, signs the exe with Azure Artifact Signing (`scripts/sign-win.sh`, the same setup as EmailEngine releases, enabled by `SIGN_WIN_ACCOUNT` and `SIGN_WIN_PROFILE`), and builds `dist/ImapKitDesktop-Setup-<version>.exe` with NSIS (`brew install makensis`), signing the uninstaller and the setup on the way. The version comes from `package.json`; `sidecar/package.json` has to match it.

`scripts/win-run.sh` starts a development build on the desktop of the build machine and `scripts/win-screenshot.sh out.png` takes a screenshot of it (`PAGE=0|1|2` picks the page).

CI builds the app, a portable zip and an unsigned installer on every push to master, they are the `ImapKitDesktop-win-x64` artifact of the CI run.

## Testing

```
cd sidecar && npm install && npm test
```

The tests run the sidecar over stdio and talk to it with Nodemailer and ImapFlow. `scripts/smoke-client.mjs` does the same against a running app.

## License

MIT
