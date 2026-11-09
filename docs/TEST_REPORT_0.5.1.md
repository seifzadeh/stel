# Stel 0.5.1 — RTP by default and PBX call stability

New accounts default to RTP. Settings → Accounts → SIP exposes Media type with
RTP, SRTP, DTLS-SRTP and ZRTP choices. The UI displays RTP rather than the internal
`none` value, and saves/loads the selected mode through stable item data. Missing
JSON media fields also default to RTP. Existing explicit saved modes are retained.
Only the two accounts the user authorized were changed to RTP on this machine.

The reported 62223 → 62224 failure was reproduced against PBX 192.168.17.12.
The server rejected SRTP with SIP 488, then the SDK silently retried using RTP
despite mandatory per-call encryption. The PBX sent the recipient an RTP offer.
The old required-SRTP Answer path returned `200 OK` with `m=audio 0`/inactive;
the PBX ended that leg with BYE/Q.850 cause 58 and ended the caller a few seconds
later. This was a media-policy mismatch caused by the app's SRTP default.

The fix defaults to RTP and makes encryption an explicit user choice. Encrypted
modes retain their policy: the SDK's automatic offer/answer retry is disabled,
and an incompatible incoming SDES offer is rejected with SIP 488 plus a clear
in-dialog message. The core-wide mandatory flag is not used because incoming
accounts can have different media policies. DTLS enforcement remains at its
handshake stage.

Real-PBX verification used the saved accounts and the normal MainWindow/CallDialog
Call and Answer paths, with temporary runtime-only dummy audio devices. Saved
microphone/speaker settings were preserved; credentials stayed in QtKeychain.
Both directions, 62223 → 62224 and 62224 → 62223, remained connected with active
bidirectional RTP for over 20 seconds each (22-second call durations; combined
download 151.752 / 152.861 kbit/s), then the checker explicitly hung up.
Evidence: `build/qa/rtp-real-pbx.log` and `rtp-real-pbx-reverse.log`. The opt-in checker is
`tests/pbx_check.cpp` / target `stel_pbx_check`; it is not run by ordinary CTest
and requires a configuration directory and both usernames.

Regression coverage adds model defaults/preserved explicit SRTP, no plaintext
retry after a rejected SRTP offer, rejecting a plaintext offer for an SRTP-required
account, and visible RTP defaults/round-trip SRTP→RTP selection in the account
editor. Existing audio/video, two-account, DTLS/ICE/BUNDLE, call controls, settings,
credentials and TLS coverage remain.

All three native CTest groups pass in 106.56 seconds: 49 core results,
19 UI results and 8 TLS scenarios. Qt 5.15.15 and Qt 6.8.2 compile, and the Qt 6
startup smoke test passes. Final build/package verification is recorded in `build/qa/rtp-ctest.log`,
`rtp-qt6-build.log`, `rtp-qt6-smoke.log`, `rtp-package.log` and the packaged UI/TLS
and single-file startup checks. The Linux dependency closure remains bundled,
with glibc >= 2.39 and OS services as documented. Windows runtime/Win7/8.1 remains
unverified on this host.

The separate call windows, software Qt video renderer, DTMF, hold/mute/record,
transfers and conference behavior are described in the
[0.5.0 report](TEST_REPORT_0.5.0.md).
