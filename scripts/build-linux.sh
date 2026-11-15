#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
export STEL_FAX_SDK="$STEL_PROJECT_DIR/.deps/fax-sdk"
python3 "$STEL_PROJECT_DIR/scripts/build-fax-sdk.py" --output "$STEL_FAX_SDK"
export STEL_PDF_SDK="$STEL_PROJECT_DIR/.deps/pdf-sdk"
python3 "$STEL_PROJECT_DIR/scripts/build-pdf-engine.py" --output "$STEL_PDF_SDK"
export LD_LIBRARY_PATH="$STEL_FAX_SDK/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake -S "$STEL_PROJECT_DIR" -B "$STEL_PROJECT_DIR/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSTEL_QT_MAJOR=5 -DSTEL_DISTRO_LINPHONE=ON \
  -DSTEL_LINPHONE_ROOT="$STEL_FAX_SDK" -DSTEL_PDF_ROOT="$STEL_PDF_SDK" \
  -DSTEL_AV1_ADDON=ON -DSTEL_LEGACY_VIDEO_ADDON=ON "$@"
cmake --build "$STEL_PROJECT_DIR/build" --parallel 4
ctest --test-dir "$STEL_PROJECT_DIR/build" --output-on-failure
