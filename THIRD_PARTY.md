# Licensing and dependencies

Stel's original source and artwork are GPL-3.0-or-later; see LICENSE and NOTICE.md.
Upstream components retain their original licenses, including AGPL-3.0-or-later
for liblinphone and the vendored Mediastreamer2 filters; see LICENSES/AGPL-3.0.txt.
The combined distribution also carries the AGPL conditions (GPLv3 section 13). This starter uses
the open-source licensing path of liblinphone. A proprietary product requires
review of the SDK/Qt/dependency licences and, where applicable, commercial SDK
licensing. Static linking does not remove licence or corresponding-source duties.

- liblinphone 5.3.105, belle-sip, mediastreamer2, oRTP, belr, bctoolbox and related
  SDK components: https://www.linphone.org/en/liblinphone-voip-sdk/
- SDK source (pin a release and retain its exact source/submodule revisions):
  https://gitlab.linphone.org/BC/public/linphone-sdk
- Qt 5.15: https://www.qt.io/licensing/ and https://code.qt.io/
- Debian source packages, including Debian patches and build metadata:
  https://sources.debian.org/ and https://snapshot.debian.org/
- OpenSSL: https://openssl-library.org/source/
- QtKeychain 0.15.0: BSD-3-Clause, https://github.com/frankosterfeld/qtkeychain
- zlib (Linux native container decompression): zlib licence, https://zlib.net/

The native portable launcher is part of Stel (GPL-3.0-or-later). Linux links its
small launcher statically with zlib; the actual Qt/SDK runtime stays bundled in
the payload. The Windows launcher uses the static C++ runtime and an uncompressed
payload, with no Qt/zlib dependency before extraction. Both run the embedded native
application directly and remove its private temporary directory after it exits.

The Linux bundle contains a `share/licenses/` directory with available Debian
copyright notices and exact installed binary/source package versions. Retain the
matching sources/build instructions when redistributing. This local development
bundle is not a completed third-party source-distribution package.

The automated release workflow additionally downloads the exact Debian source
packages listed in `share/licenses/debian-sources.json`. The Windows recipe builds
the pinned SDK/submodules and QtKeychain in `packaging/dependencies.json`; its
source archive retains those trees, submodule revision records and matching
official Qt module sources. These dependency-source archives accompany the
application source archive in every automated release. Linux distro dependency
versions are recorded per build; the distro is Debian 13. Windows uses Qt 5.15.2.
See `docs/BUILDING.md` for the complete recipes.

Codec availability follows the installed SDK. Available and enabled codecs are
shown separately in Settings. The Linux build adds matching upstream AV1 filters
from `third_party/ms2-av1` (AGPL-3.0-or-later), AOM/libaom (BSD-2-Clause and
Alliance for Open Media Patent License), and dav1d (BSD-2-Clause). G.729 uses
the SDK's existing bcg729 backend (GPL-3.0-or-later/commercial dual licence).
The Debian integration uses adapted MS2 H.263/MPEG-4 encoder/decoder filters in
`third_party/ms2-legacy`, updated to FFmpeg 7's allocation and send/receive APIs
(AGPL-3.0-or-later); FFmpeg codec/utility/scaler libraries follow their bundled
Debian build's licences. Retain the documented local source modifications.
See the source origin/checksum in `third_party/ms2-av1/README.md`. Review codec
patents and binary codec redistribution terms for your distribution jurisdiction.

The packaged CA bundle is a copy of the host's public trust store at packaging
time. It contains no private key; update it with application releases. Custom
PBX trust roots and client private keys belong in the user's private settings,
not in a generally distributed binary package.
