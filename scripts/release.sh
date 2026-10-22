#!/usr/bin/env bash
# Builds, signs and publishes a release of ImapKit Desktop from the release Mac, like emailengine-api/upload.sh:
#
#   scripts/release.sh            the release of the version in package.json
#   BUILD_ONLY=yes scripts/release.sh   builds and signs, uploads nothing
#
# release-please drafts the release and pushes the tag when its release PR is merged (.github/workflows/release.yaml).
# This builds the installer from that tag on the Windows build machine (scripts/build-installer.sh), signs the exe,
# the uninstaller and the installer with Azure Artifact Signing, writes hashes.txt signed with the release GPG key,
# uploads both to the draft release and publishes it.
#
# Needs: SIGN_WIN_ACCOUNT and SIGN_WIN_PROFILE (and the keychain item of scripts/sign-win.sh), gpg with the release
# key, gh, makensis, and the Windows build machine of scripts/win-env.sh
set -euo pipefail
cd "$(dirname "$0")/.."

REPO="postalsys/imapkit-desktop"
# "Postalsys Releases <releases@postalsys.com>", the key EmailEngine releases are signed with
GPG_KEY="B6F07B59064F9CCA"
BUILD_ONLY="${BUILD_ONLY:-no}"

VERSION=$(node -p 'require("./package.json").version')
TAG="v${VERSION}"
SETUP="ImapKitDesktop-Setup-${VERSION}.exe"

fail() {
    echo "$*" >&2
    exit 1
}

# everything that can stop the release is checked before the build, which takes minutes
if [ -z "${SIGN_WIN_ACCOUNT:-}" ] || [ -z "${SIGN_WIN_PROFILE:-}" ]; then
    fail "SIGN_WIN_ACCOUNT and SIGN_WIN_PROFILE are not set, a release is always signed"
fi
if [ -n "$(git status --porcelain)" ]; then
    fail "The working tree has changes, the release is built from the files on disk and must match the tag"
fi
if [ "$BUILD_ONLY" != "yes" ]; then
    gh release view "$TAG" --repo "$REPO" --json isDraft >/dev/null 2>&1 || fail "No release ${TAG}, merge the release PR of release-please first"
    git fetch --quiet --tags origin
    git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null || fail "No tag ${TAG}"
    [ "$(git rev-parse HEAD)" = "$(git rev-parse "${TAG}^{commit}")" ] || fail "HEAD is not ${TAG}, check out the tag (git checkout ${TAG}) and run again"
    CI=$(gh run list --repo "$REPO" --workflow CI --commit "$(git rev-parse HEAD)" --json conclusion --jq '.[0].conclusion')
    [ "$CI" = "success" ] || fail "CI of ${TAG} is '${CI:-not run}', not success"
fi
# the GPG passphrase is asked now, not after the build
gpg --clearsign --default-key "$GPG_KEY" --output /dev/null package.json

./scripts/build-installer.sh

cd dist
{
    echo "ImapKit Desktop ${VERSION} (https://github.com/${REPO})"
    echo ""
    echo "Signed with ${GPG_KEY}"
    echo "https://api.github.com/users/andris9/gpg_keys"
    echo ""
    echo "Built from $(git rev-parse --short HEAD)"
    echo ""
    openssl sha256 "$SETUP"
} >hashes.txt
gpg --clearsign --default-key "$GPG_KEY" hashes.txt
mv hashes.txt.asc hashes.txt
# hashes.txt is what users verify, check that it carries a good signature of the release key in particular
gpg --verify --status-fd 1 hashes.txt 2>/dev/null | grep -q "^\[GNUPG:\] VALIDSIG [0-9A-F]*${GPG_KEY} " || fail "hashes.txt is not signed with ${GPG_KEY}"

if [ "$BUILD_ONLY" = "yes" ]; then
    echo "Built dist/${SETUP} and dist/hashes.txt, nothing uploaded"
    exit 0
fi

gh release upload "$TAG" "$SETUP" hashes.txt --repo "$REPO" --clobber
# the draft becomes the latest release now that its installer is there
gh release edit "$TAG" --repo "$REPO" --draft=false --latest
echo "Published ${TAG}: https://github.com/${REPO}/releases/tag/${TAG}"
