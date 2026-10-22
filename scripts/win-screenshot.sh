#!/usr/bin/env bash
# Takes a screenshot of the app window on the Windows machine and copies it here: scripts/win-screenshot.sh out.png
# DESKTOP=1 takes the whole desktop instead, PAGE=0|1|2|3 switches to the Server, Connections, Messages or Client page first,
# KEYS=40,40 sends these virtual keys to the list view of that page (40 is Down), RESIZE=800x600,1120x760 resizes the
# window in turn, COMMAND=405 sends that control id as WM_COMMAND to the visible page
set -euo pipefail
. "$(dirname "$0")/win-env.sh"
OUT=${1:-screenshot.png}
FLAG="${DESKTOP:+ -Desktop}${PAGE:+ -Page $PAGE}${KEYS:+ -ListKeys $KEYS}${RESIZE:+ -Resize $RESIZE}${COMMAND:+ -PageCommand $COMMAND}"
ssh "$WIN_HOST" "del $WIN_DIR\\shot.png >nul 2>&1 & schtasks /create /tn ImapKitDesktopShot /tr \"powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File $WIN_DIR\\scripts\\screenshot.ps1 $WIN_DIR\\shot.png$FLAG\" /sc once /st 23:59 /it /f >nul && schtasks /run /tn ImapKitDesktopShot >nul"
for _ in $(seq 1 20); do
    if scp -q "$WIN_HOST:$WIN_DIR_FWD/shot.png" "$OUT" 2>/dev/null; then
        echo "$OUT"
        exit 0
    fi
    sleep 1
done
echo "no screenshot, is a desktop session logged in and not locked?" >&2
exit 1
