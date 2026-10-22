#!/usr/bin/env bash
# Copies the sources to the Windows build machine and builds them there with MSVC
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/win-env.sh
ssh "$WIN_HOST" "if not exist $WIN_DIR mkdir $WIN_DIR"
COPYFILE_DISABLE=1 tar -czf - --exclude node_modules --exclude build --exclude .git --exclude dist . | ssh "$WIN_HOST" "tar -xzf - -C $WIN_DIR_FWD"
ssh "$WIN_HOST" "$WIN_DIR\\scripts\\win-build.cmd"
