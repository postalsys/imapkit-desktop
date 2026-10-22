#!/usr/bin/env bash
# Takes the screenshots of the ImapKit Desktop page of imapkit.com, the same way on every run:
#
#   scripts/docs-screenshots.sh [output folder, default ../imapkit/docs/static/img/desktop]
#
# It builds the app on the Windows machine (scripts/win-env.sh), closes a running copy there (it saves its data), and
# starts the app with --data-dir on a fresh copy of the demo data in scripts/screenshots/demo (fixed messages, dates
# and settings; scripts/screenshots/make-demo.mjs makes it). The steps below select messages, drive the Client page
# and switch tabs with scripts/ui-steps.ps1, every screenshot has the same window size and crop. Only the clock times
# of the protocol logs differ between runs. The demo copy is closed and its data removed at the end, the data of the
# user (%APPDATA%\ImapKitDesktop) is never touched. Needs a logged in, unlocked desktop session on the machine.
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/win-env.sh

OUT=${1:-../imapkit/docs/static/img/desktop}
SIZE=1120x760
TASK=ImapKitDesktopDocs
DATA="$WIN_DIR\\screenshot-data"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# runs a command in the desktop session of the logged in user, where the window is; waits for it unless told not to
desktop() {
    ssh "$WIN_HOST" "schtasks /create /tn $TASK /tr \"$1\" /sc once /st 23:59 /it /f >nul && schtasks /run /tn $TASK >nul"
    [ "${2:-wait}" = "wait" ] || return 0
    for _ in $(seq 1 120); do
        sleep 1
        ssh "$WIN_HOST" "schtasks /query /tn $TASK /fo csv /nh" | grep -q Running || return 0
    done
    echo "$1 did not finish" >&2
    return 1
}

# the steps of scripts/ui-steps.ps1, one per argument
steps() {
    printf '%s\r\n' "$@" >"$TMP/steps.txt"
    scp -q "$TMP/steps.txt" "$WIN_HOST:$WIN_DIR_FWD/screenshot-steps.txt"
    desktop "powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File $WIN_DIR\\scripts\\ui-steps.ps1 $WIN_DIR\\screenshot-steps.txt"
}

# closes the app like the installer does (WM_CLOSE, it saves), then for sure
close_app() {
    desktop "taskkill /im ImapKitDesktop.exe"
    sleep 3
    ssh "$WIN_HOST" "taskkill /f /im ImapKitDesktop.exe >nul 2>&1 & exit /b 0"
}

# a screenshot of the window at the fixed size, without the desktop around its frame (the resize border on the
# sides and at the bottom, a line at the top). The variables of scripts/win-screenshot.sh go before the name
shot() {
    local name=$1
    shift
    env "$@" RESIZE=$SIZE scripts/win-screenshot.sh "$TMP/$name-full.png" >/dev/null
    local width=${SIZE%x*} height=${SIZE#*x}
    sips -c $((height - 9)) $((width - 16)) --cropOffset 1 8 "$TMP/$name-full.png" --out "$TMP/$name.png" >/dev/null
    echo "$name.png"
}

# window messages and screen captures need a desktop that is shown: a disconnected RDP session renders nothing
if ! ssh "$WIN_HOST" "query user" 2>/dev/null | grep -q " Active "; then
    echo "No active desktop session on $WIN_HOST, log in (or reconnect the remote desktop) and keep it unlocked" >&2
    exit 1
fi

scripts/win-build.sh >/dev/null
close_app
ssh "$WIN_HOST" "if exist $DATA rmdir /s /q $DATA & mkdir $DATA"
scp -q scripts/screenshots/demo/*.json "$WIN_HOST:$WIN_DIR_FWD/screenshot-data/"
desktop "$WIN_DIR\\build\\ImapKitDesktop.exe --data-dir $DATA" nowait
sleep 6

shot server PAGE=0
# the SMTP tab, the fourth of the tab control (214) on the Server page
steps "page 0" "keys 214 39,39,39"
shot server-smtp
steps "page 0" "keys 214 37,37,37"

# End and Up select the Northwind order, Home the reply of Maria Lopez (see make-demo.mjs)
shot messages PAGE=2 KEYS=35,38
shot quoted PAGE=2 KEYS=36

# a session on the Client page: clear the hint, connect to the IMAP preset, STARTTLS, log in, select INBOX, and a
# command typed but not sent
steps "page 3" "click 610" "select 601 0" "click 607" "wait 1200" \
    "text 613 a CAPABILITY" "click 614" "wait 500" \
    "text 613 b STARTTLS" "click 614" "wait 1500" \
    "text 613 c LOGIN testuser testpass" "click 614" "wait 500" \
    "text 613 d EXAMINE INBOX" "click 614" "wait 800" \
    "text 613 e FETCH 1:* (FLAGS ENVELOPE)"
shot client PAGE=3
# the same session from the server's side
shot connections PAGE=1

close_app
ssh "$WIN_HOST" "rmdir /s /q $DATA & del $WIN_DIR\\screenshot-steps.txt & schtasks /delete /tn $TASK /f >nul"

mkdir -p "$OUT"
for name in server server-smtp messages quoted client connections; do
    cp "$TMP/$name.png" "$OUT/$name.png"
done
echo "Screenshots in $OUT"
