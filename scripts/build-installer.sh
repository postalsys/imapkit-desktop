#!/usr/bin/env bash
# Builds the signed installer dist/ImapKitDesktop-Setup.exe, from the Mac:
#
#   1. builds the app and stages it (exe, sidecar, pinned node.exe) on the Windows build machine
#   2. copies the staging folder here
#   3. signs ImapKitDesktop.exe, then makensis builds the installer and signs the uninstaller and the installer
#
# Signing uses scripts/sign-win.sh, it needs SIGN_WIN_ACCOUNT and SIGN_WIN_PROFILE (unset: an unsigned build).
# node.exe is left as nodejs.org signed it. Needs makensis 3.08 or newer (brew install makensis)
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/win-env.sh

VERSION=$(node -p 'require("./package.json").version')
if [ "$(node -p 'require("./sidecar/package.json").version')" != "$VERSION" ]; then
    echo "sidecar/package.json and package.json have different versions" >&2
    exit 1
fi

echo "Building ImapKit Desktop ${VERSION} on ${WIN_HOST}"
./scripts/win-build.sh
ssh "$WIN_HOST" "$WIN_DIR\\scripts\\win-package.cmd"

rm -rf dist
mkdir -p dist
ssh "$WIN_HOST" "tar -czf - -C $WIN_DIR_FWD/dist ImapKitDesktop" | tar -xzf - -C dist

./scripts/sign-win.sh dist/ImapKitDesktop/ImapKitDesktop.exe

SIGN_FLAG=()
if [ -n "${SIGN_WIN_ACCOUNT:-}" ] && [ -n "${SIGN_WIN_PROFILE:-}" ]; then
    SIGN_FLAG=(-DSIGN)
fi
# no version in the name, so releases/latest/download/ImapKitDesktop-Setup.exe always gets the latest release
OUTFILE="dist/ImapKitDesktop-Setup.exe"
makensis -V2 -NOCD -DVERSION="$VERSION" -DSTAGING=dist/ImapKitDesktop -DOUTFILE="$OUTFILE" "${SIGN_FLAG[@]}" installer/imapkit-desktop.nsi

shasum -a 256 "$OUTFILE"
