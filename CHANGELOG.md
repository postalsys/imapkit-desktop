# Changelog

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
