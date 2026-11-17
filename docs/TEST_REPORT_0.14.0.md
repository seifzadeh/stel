# Stel 0.14.0 — calls on the main display

Verified locally on Linux, 2026-11-17.

- Qt 5.15.15: all 39 UI test results pass. Incoming and outgoing calls stay on the phone; answering, rejecting, DTMF, hold/resume, mute, recording, forward, attended transfer and conference are exercised with SIP calls.
- Video: real dummy-camera frames render on the LCD, expand into the optional window and return without hanging up. Repeated expansion and switching selected lines preserve the correct video target.
- The existing core, paging, BLF, message, fax-document, fax and TLS test groups pass. The UI group was rerun after adapting optional-window tests to the new default; its final result is 39 passed, 0 failed.
- Qt 6: build passes; inline audio/video tests pass (4 results, including setup/cleanup).
- The Linux portable dependency audit passes (225 ELF files; glibc 2.39 minimum). The single-file binary runs with development paths removed, including from a Persian/spaced path.
- deb/rpm payload, version, dependency metadata, desktop/icon integration and extracted standalone launch checks pass. System-wide package installation was not performed locally.
- All 8 release tooling tests pass. No commit, push or publication was performed for this change.
- The 29 screenshots come from production widgets, a disposable local PBX and real media. Their manifest hashes match. English/Persian website galleries pass at 1440px and 390px, including every image preview and overflow checks.

Windows compilation and installation remain checks of the existing GitHub Actions workflow; they were not run on this Linux host.

Local evidence: `../build/qa/inline-final-ui.log`, `inline-qt6-ui.log`,
`inline-all-tests.log`, `inline-package.log`, `inline-portable.log`,
`inline-installers-check.log`, `inline-site-check.log` and `inline-screenshots.log`.
