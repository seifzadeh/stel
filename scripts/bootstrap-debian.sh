#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
mkdir -p "$STEL_PROJECT_DIR/.deps/debs/partial" "$STEL_DEPS_PREFIX"
apt-get --download-only --yes -o Debug::NoLocking=1 \
  -o "Dir::Cache::archives=$STEL_PROJECT_DIR/.deps/debs" install \
  cmake ninja-build ripgrep qtbase5-dev qtbase5-dev-tools liblinphone-dev \
  libmediastreamer-dev libbctoolbox-dev libbelr-dev libortp-dev libbellesip-dev \
  libaom-dev libdav1d-dev libavcodec-dev libavutil-dev libswscale-dev qtkeychain-qt5-dev zlib1g-dev patchelf
for package in "$STEL_PROJECT_DIR"/.deps/debs/*.deb; do
  dpkg-deb -x "$package" "$STEL_PROJECT_DIR/.deps/root"
done
# apt downloads only missing runtime packages. Connect existing host runtimes to
# their extracted development symlinks; this directory is never shipped.
python3 "$STEL_PROJECT_DIR/scripts/fix-deps.py" "$STEL_PROJECT_DIR/.deps/root"
echo "Dependencies ready. Run scripts/build-linux.sh"
