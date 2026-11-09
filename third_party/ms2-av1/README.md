# Matching Mediastreamer2 5.3.105 AV1 filters

Unmodified upstream AV1/AOM encoder, AV1/dav1d decoder, OBU RTP helpers,
and their C++ filter interfaces/wrappers from Mediastreamer2 5.3.105.
Copyright Belledonne Communications; AGPL-3.0-or-later, see LICENSE.txt and
individual source headers. Integration lives in the application's CMakeLists.txt
and src/sipengine.cpp. No generated binaries are included here.

Source: https://deb.debian.org/debian/pool/main/m/mediastreamer2/mediastreamer2_5.3.105+dfsg.orig.tar.xz

SHA-256: d5837caf52089d427b15c881b42ffb87889101d94b80fe14bc69d5f95df8989c

Build with `-DSTEL_AV1_ADDON=ON` and matching MS2 5.3 headers/library plus
libaom/dav1d development packages. This is enabled by scripts/build-linux.sh
for the Debian development environment. When building a complete Linphone SDK,
enable AV1 there and leave the app addon OFF to use the SDK's own filters.
Filters are registered only when that core's factory lacks the corresponding
AV1 encoder/decoder. The portable packager includes the AOM/dav1d runtime
dependency closure and their distribution notices.
