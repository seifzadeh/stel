#!/usr/bin/env bash
set -euo pipefail
# Run as root in the workflow's Debian 13 container, not on an end user's machine.
. /etc/os-release
test "$ID" = debian && test "$VERSION_ID" = 13
export DEBIAN_FRONTEND=noninteractive
sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources
apt-get update
apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build git ca-certificates python3 openssl patchelf \
  qtbase5-dev qtbase5-dev-tools libqt5svg5 qtwayland5 qtkeychain-qt5-dev \
  liblinphone-dev libmediastreamer-dev libbctoolbox-dev libbelr-dev libortp-dev \
  libbellesip-dev libaom-dev libdav1d-dev libavcodec-dev libavutil-dev libswscale-dev \
  zlib1g-dev libsecret-1-0 dbus-x11 xvfb xauth dpkg-dev
