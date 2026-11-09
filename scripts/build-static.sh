#!/usr/bin/env bash
set -euo pipefail
STEL_PROJECT_DIR="$(cd -- "$(dirname -- "$0")/.." && pwd)"
: "${QT_STATIC_PREFIX:?Set QT_STATIC_PREFIX to a static Qt 5.15 prefix}"
: "${LINPHONE_STATIC_PREFIX:?Set LINPHONE_STATIC_PREFIX to a static Linphone SDK prefix}"
cmake -S "$STEL_PROJECT_DIR" -B "$STEL_PROJECT_DIR/build-static" \
  -DCMAKE_BUILD_TYPE=Release -DSTEL_QT_MAJOR=5 -DSTEL_STATIC=ON \
  -DSTEL_LINPHONE_ROOT="$LINPHONE_STATIC_PREFIX" \
  -DCMAKE_PREFIX_PATH="$QT_STATIC_PREFIX;$LINPHONE_STATIC_PREFIX"
cmake --build "$STEL_PROJECT_DIR/build-static" --parallel "${STEL_BUILD_JOBS:-4}"
ctest --test-dir "$STEL_PROJECT_DIR/build-static" --output-on-failure
echo "Qt/SDK static linking requested and checked. Audit readelf/ldd for remaining OS libraries and deploy resources/plugins."
