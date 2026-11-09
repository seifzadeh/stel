#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
cmake -S "$STEL_PROJECT_DIR" -B "$STEL_PROJECT_DIR/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSTEL_QT_MAJOR=5 -DSTEL_DISTRO_LINPHONE=ON \
  -DSTEL_LINPHONE_ROOT="$STEL_DEPS_PREFIX" -DSTEL_AV1_ADDON=ON -DSTEL_LEGACY_VIDEO_ADDON=ON "$@"
cmake --build "$STEL_PROJECT_DIR/build" --parallel 4
ctest --test-dir "$STEL_PROJECT_DIR/build" --output-on-failure
