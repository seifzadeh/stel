#!/usr/bin/env bash
# SDK source builds are intentionally separate from the application build.
set -euo pipefail
STEL_SDK_SOURCE="${1:?Usage: build-sdk.sh /path/to/linphone-sdk [install-prefix]}"
STEL_SDK_PREFIX="${2:-$PWD/vendor/linphone-static}"
STEL_SDK_SOURCE="$(cd "$STEL_SDK_SOURCE" && pwd)"
cmake -S "$STEL_SDK_SOURCE" -B "$STEL_SDK_SOURCE/build-stel-static" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$STEL_SDK_PREFIX" \
  -DLINPHONESDK_PLATFORM=Desktop -DBUILD_SHARED_LIBS=OFF \
  -DENABLE_VIDEO=ON -DENABLE_AV1=ON -DENABLE_G729=ON -DENABLE_SRTP=ON -DENABLE_ZRTP=ON \
  -DENABLE_ADVANCED_IM=OFF -DENABLE_DB_STORAGE=OFF -DENABLE_LDAP=OFF \
  -DENABLE_CXX_WRAPPER=OFF -DENABLE_UNIT_TESTS=OFF
cmake --build "$STEL_SDK_SOURCE/build-stel-static" --parallel "${STEL_BUILD_JOBS:-4}"
echo "SDK build finished. Inspect the generated SDK prefix/install tree before setting STEL_LINPHONE_ROOT."
