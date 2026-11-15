#!/usr/bin/env bash
# Capture production widgets without opening the developer's settings/keyring.
set -euo pipefail
STEL_CAPTURE_PROJECT="$(cd -- "$(dirname -- "$0")/.." && pwd)"
STEL_CAPTURE_BUILD="$(realpath "${1:-$STEL_CAPTURE_PROJECT/build}")"
STEL_CAPTURE_OUTPUT="$(realpath -m "${2:-$STEL_CAPTURE_PROJECT/build/screenshots}")"
cmake --build "$STEL_CAPTURE_BUILD" --target stel_screenshots --parallel 4
STEL_CAPTURE_STATE="$(mktemp -d -t stel-screenshots-XXXXXXXX)"
trap 'rm -rf -- "$STEL_CAPTURE_STATE"' EXIT
export XDG_RUNTIME_DIR="$STEL_CAPTURE_STATE/runtime" XDG_DATA_HOME="$STEL_CAPTURE_STATE/data"
export XDG_CONFIG_HOME="$STEL_CAPTURE_STATE/config" XDG_CACHE_HOME="$STEL_CAPTURE_STATE/cache"
mkdir -m 700 "$XDG_RUNTIME_DIR" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME"
unset GNOME_KEYRING_CONTROL GNOME_KEYRING_PID
export QT_QPA_PLATFORM=offscreen
export STEL_SCREENSHOT_REVISION="$(git -C "$STEL_CAPTURE_PROJECT" rev-parse HEAD)"
dbus-run-session -- bash -euo pipefail -s -- "$STEL_CAPTURE_BUILD/stel_screenshots" "$STEL_CAPTURE_OUTPUT" <<'SESSION'
trap 'gnome-keyring-daemon --quit --control-directory "$XDG_RUNTIME_DIR/keyring" >/dev/null 2>&1 || true' EXIT
printf '%s' 'stel-screenshot-disposable-keyring' | gnome-keyring-daemon --unlock --components=secrets --control-directory "$XDG_RUNTIME_DIR/keyring"
"$1" "$2"
SESSION
