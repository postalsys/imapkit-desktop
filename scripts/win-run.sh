#!/usr/bin/env bash
# Starts the built app on the desktop of the logged in user of the Windows machine (an SSH session has no desktop),
# through a scheduled task that runs interactively. Stops a running copy first
set -euo pipefail
. "$(dirname "$0")/win-env.sh"
ssh "$WIN_HOST" "taskkill /im ImapKitDesktop.exe /f >nul 2>&1 & schtasks /create /tn ImapKitDesktopDev /tr $WIN_DIR\\build\\ImapKitDesktop.exe /sc once /st 23:59 /it /f >nul && schtasks /run /tn ImapKitDesktopDev"
