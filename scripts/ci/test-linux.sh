#!/usr/bin/env bash
# Exercise the real QtKeychain backend in an isolated, unlocked Secret Service.
set -euo pipefail
STEL_TEST_BUILD="$(realpath "${1:-build}")"
STEL_TEST_STATE="$(mktemp -d -t stel-ci-keyring-XXXXXXXX)"
trap 'rm -rf -- "$STEL_TEST_STATE"' EXIT
export XDG_RUNTIME_DIR="$STEL_TEST_STATE/runtime"
export XDG_DATA_HOME="$STEL_TEST_STATE/data"
export XDG_CONFIG_HOME="$STEL_TEST_STATE/config"
export XDG_CACHE_HOME="$STEL_TEST_STATE/cache"
mkdir -m 700 "$XDG_RUNTIME_DIR" "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME"
unset GNOME_KEYRING_CONTROL GNOME_KEYRING_PID
export QT_QPA_PLATFORM=offscreen
dbus-run-session -- bash -euo pipefail -s -- "$STEL_TEST_BUILD" <<'SESSION'
trap 'gnome-keyring-daemon --quit --control-directory "$XDG_RUNTIME_DIR/keyring" >/dev/null 2>&1 || true' EXIT
# Disposable CI credentials only; this never opens the developer's real keyring.
printf '%s' 'stel-ci-disposable-keyring' | gnome-keyring-daemon --unlock --components=secrets --control-directory "$XDG_RUNTIME_DIR/keyring"
printf '%s' 'secret-service-ready' | timeout 15 secret-tool store --label='Stel CI preflight' app Stel-CI purpose preflight
test "$(timeout 15 secret-tool lookup app Stel-CI purpose preflight)" = 'secret-service-ready'
secret-tool clear app Stel-CI purpose preflight
echo 'Real Secret Service preflight passed; running all native tests.'
ctest --test-dir "$1" --output-on-failure
SESSION
