# Changelog

## [0.3.0](https://github.com/postalsys/imapkit-desktop/compare/v0.2.0...v0.3.0) (2026-10-22)


### Features

* keep running in the notification area when the window is closed while the server listens, one copy per user ([ce9a6a9](https://github.com/postalsys/imapkit-desktop/commit/ce9a6a9b89b132035159771544daa04bbc4b3f1b))
* Limits and SMTP tabs, APPENDLIMIT, quota, message and literal limits, SMTP login, size limit and rejected recipients ([b4bd342](https://github.com/postalsys/imapkit-desktop/commit/b4bd342e6e03af74f9d70af1a003efbd5749f7f7))
* rejected recipients as a table with cells edited in place ([f64b156](https://github.com/postalsys/imapkit-desktop/commit/f64b1568ecb4591ca169aa305ddd38c1d0663a2b))
* Server ID tab, custom values for the ID response instead of ID NIL ([efe7575](https://github.com/postalsys/imapkit-desktop/commit/efe7575cacdc3fb08c4e32f5ad7ec64c88ebf4cb))
* Server page tabs regrouped into Extensions, Limits & ID, Faults, SMTP and REST API, extensions in columns ([d358c0f](https://github.com/postalsys/imapkit-desktop/commit/d358c0f9a3bacd7bccbb0ca815981b9a9ef52dcf))


### Bug Fixes

* publish the installer only as ImapKitDesktop-Setup.exe, Check for Updates downloads it directly ([c44a167](https://github.com/postalsys/imapkit-desktop/commit/c44a16745585c110ac7a8a732a1a383d08eb8784))
* ship the license texts of Node.js and the other bundled software in THIRD-PARTY-NOTICES.txt ([3af9412](https://github.com/postalsys/imapkit-desktop/commit/3af9412d2bd166e33b372534a71c2148bdeb8481))

## [0.2.0](https://github.com/postalsys/imapkit-desktop/compare/v0.1.0...v0.2.0) (2026-10-22)


### Features

* Client page, a plaintext client like telnet or openssl s_client with TLS and STARTTLS ([7a0a1f9](https://github.com/postalsys/imapkit-desktop/commit/7a0a1f9fc6228bfde4663d5b8617f379c947065d))

## 0.1.0 (2026-10-22)


### Features

* fold quoted replies and forwards, draw the paperclip without a background ([12f137e](https://github.com/postalsys/imapkit-desktop/commit/12f137e1c88b477212c7b29b2d3ae98ef9d22e38))
* keep mail that clients send with an SMTP login in an Outbox ([24d6578](https://github.com/postalsys/imapkit-desktop/commit/24d6578cb892788256639c3cc9309e8e94ecf756))
* log the changes made in the app between the protocol lines ([12b4440](https://github.com/postalsys/imapkit-desktop/commit/12b44401ec5a9ca33a906be99d9c5fb6ec636f25))
* release pipeline with release-please and a signed release script, check for updates ([073cd92](https://github.com/postalsys/imapkit-desktop/commit/073cd924a506b0389ff9018f2d4ee04b47eec4cf))
* rework the Server and Connections pages, order the folders like a mail client ([7b03af0](https://github.com/postalsys/imapkit-desktop/commit/7b03af06d817bf0b7afbfa09ba3d56ab588e3703))
* save attachments from the preview, list embedded images, envelope icons ([54d14a7](https://github.com/postalsys/imapkit-desktop/commit/54d14a7287496d9d22a4abcec81743cd3219fdd0))
* show blocked pictures on request, and paint the message header strip directly ([f8d4a88](https://github.com/postalsys/imapkit-desktop/commit/f8d4a88498063fd3b43da22f1d4d04e4d7c4854c))


### Bug Fixes

* draw the paperclip in the classic outline shape, antialiased with GDI+ ([1e53ca0](https://github.com/postalsys/imapkit-desktop/commit/1e53ca01f933038209a915c566932b125fb278da))
* match the staged files of the installer also with makensis on Windows ([674d492](https://github.com/postalsys/imapkit-desktop/commit/674d492d5c44702addb17e9aa1ee63645f981fbb))
* paint the Server page under its group boxes, they kept pixels of the page shown before ([bcfa767](https://github.com/postalsys/imapkit-desktop/commit/bcfa767f682450cd9cfd0e6c3f58015c0ec20e00))
* show the Outbox bold only for unread mail, keep the selection right on reloads ([9af2257](https://github.com/postalsys/imapkit-desktop/commit/9af2257f67acba161df55b53172ee32ccb2db33a))
* size the paperclip to its cell, it did not fit the rows ([3a0e1ea](https://github.com/postalsys/imapkit-desktop/commit/3a0e1eaf0455856ba569f12e66c763a30ba389e2))
* stretch a column of each list to the full width, the rows and their highlight ended halfway ([dc79016](https://github.com/postalsys/imapkit-desktop/commit/dc79016a637ca40d70c7dee0101826d71e2b7e7e))
