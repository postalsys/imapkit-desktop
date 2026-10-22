#!/usr/bin/env bash
# Authenticode-signs Windows executables with Azure Artifact Signing, the way emailengine-api/upload.sh does:
#
#   scripts/sign-win.sh <file.exe>...
#
# SIGN_WIN_ACCOUNT and SIGN_WIN_PROFILE name the signing account and certificate profile, SIGN_WIN_ENDPOINT its
# region. Without them the files stay unsigned and this says so. The credentials of the signing service principal
# (tenantId, clientId, clientSecret) are read from the login keychain item "azure-artifact-signing", so no secret is
# passed on a command line. Needs jsign and jq (brew install jsign jq).
set -euo pipefail

SIGN_WIN_ACCOUNT="${SIGN_WIN_ACCOUNT:-}"
SIGN_WIN_PROFILE="${SIGN_WIN_PROFILE:-}"
SIGN_WIN_ENDPOINT="${SIGN_WIN_ENDPOINT:-neu.codesigning.azure.net}"
SIGN_WIN_KEYCHAIN="azure-artifact-signing"
PRODUCT="ImapKit Desktop"
HOMEPAGE="https://github.com/postalsys/imapkit-desktop"

if [ -z "$SIGN_WIN_ACCOUNT" ] || [ -z "$SIGN_WIN_PROFILE" ]; then
    echo "NOTE: Windows signing is not configured, leaving $* unsigned" >&2
    exit 0
fi

# a short lived access token for the signing service principal, OAuth client credentials grant. The request body
# carries the secret, so it goes to curl on stdin
token() {
    local creds tenant
    creds=$(security find-generic-password -s "$SIGN_WIN_KEYCHAIN" -w 2>/dev/null) || return 1
    tenant=$(jq -er '.tenantId' <<<"$creds") || return 1
    jq -r '"grant_type=client_credentials&scope=https%3A%2F%2Fcodesigning.azure.net%2F.default&client_id=\(.clientId | @uri)&client_secret=\(.clientSecret | @uri)"' <<<"$creds" |
        curl -sSf --data @- "https://login.microsoftonline.com/${tenant}/oauth2/v2.0/token" |
        jq -er '.access_token'
}

if ! AZURE_CODESIGNING_TOKEN=$(token); then
    echo "Cannot get an Azure Artifact Signing token for ${SIGN_WIN_ACCOUNT}/${SIGN_WIN_PROFILE}." >&2
    echo "Check the '${SIGN_WIN_KEYCHAIN}' keychain item (is the screen locked?) and that its client secret has not expired." >&2
    exit 1
fi
export AZURE_CODESIGNING_TOKEN

for file in "$@"; do
    echo "Signing ${file}..."
    # jsign timestamps automatically for this store type, needed as the certificates are valid for three days
    jsign --storetype TRUSTEDSIGNING \
        --keystore "${SIGN_WIN_ENDPOINT}" \
        --storepass "env:AZURE_CODESIGNING_TOKEN" \
        --alias "${SIGN_WIN_ACCOUNT}/${SIGN_WIN_PROFILE}" \
        --name "${PRODUCT}" \
        --url "${HOMEPAGE}" \
        --replace \
        "$file"
done
