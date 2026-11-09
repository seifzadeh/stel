# MS2 5.3.105 legacy video filters adapted for FFmpeg 7

Derived from upstream Mediastreamer2 5.3.105 `src/videofilters/videoenc.c`,
`videodec.c`, and `src/voip/rfc2429.h`. AGPL-3.0-or-later; original copyright
headers and LICENSE.txt are retained. Source archive and checksum are recorded
in ../ms2-av1/README.md. Local changes by Stel contributors, 2026-11-09:

- Allocate/free AVCodecContext through FFmpeg APIs, preserving required defaults.
- Use avcodec_send_frame/receive_packet and send_packet/receive_frame; remove
  obsolete compatibility APIs and global FFmpeg initialization/log overrides.
- Supply AVFrame format/dimensions/strides, timestamps and owned packets correctly.
- Allocate and pad decoder extradata; pad incoming bitstreams for FFmpeg readers.
- Validate legacy RTP header lengths and cap incomplete frame accumulation.
- Respect H.263 standard frame dimensions, scaling camera frames when necessary.
- Prefix filter descriptor symbols to avoid replacing SDK symbols globally.
- Register the patched H.263 (RFC2190/RFC4629) and MPEG-4 filters only for builds
  explicitly using STEL_LEGACY_VIDEO_ADDON=ON. No binary files are included here.

Requires matching MS2 headers/library and FFmpeg 7 development libraries.
scripts/build-linux.sh enables this for the tested Debian 13 environment.
Other SDKs should use their own supported filters and leave this addon OFF.
