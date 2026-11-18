# Stel 0.15.1 — half-size interface

Verified locally on Linux, 2026-11-18.

- Qt 5.15.15 and Qt 6.8.2 builds pass.
- All seven layout groups pass on both Qt versions at 100%, 125%, 150%, 175%, 200%, 250% and 300% display scaling. They check actual production widgets, the smaller default sizes, field heights, keypad clicks, reachable controls, scrolling, screen bounds and off-screen recovery.
- Defaults at 100% desktop scaling: phone 420 × 315, Settings 420 × 300, optional call window 230 × 290 logical pixels. Fonts, CSS lengths, icons, form spacing and platform style metrics use a common 0.5 application scale. The phone and fax artwork scale with their native controls.
- The Qt 5 UI suite passes all 39 results, including account registration, Settings close/restart, voice/video calls, forwarding, transfer and conference.
- All 18 Remote API results pass. The integration suite includes real SIP voice/video, Persian messages and T.38/G.711 fax.
- All 11 fax-document results pass. A4 document preparation remains independent of the smaller UI.
- Portable dependency audit passes: 225 ELF files, glibc 2.39 minimum. Standalone version/startup and packaged HTTPS API checks pass without development library/plugin paths.
- Newly built DEB/RPM packages pass metadata, unchanged embedded-payload, menu/icon and extracted standalone-launch checks. System-wide installation was not performed locally.
- Thirty screenshots were recaptured at the actual default sizes, using disposable SIP accounts, real voice/video calls, Persian/English messages and a completed two-page T.38 fax. Image hashes and website dimensions match the capture manifest.
- The static website builds and all 8 release-tooling tests pass.

Windows CI is configured to run the same seven layout groups. This Linux host
has not compiled or executed these new UI changes on Windows. No commit, push
or publication was performed for these UI changes.

Evidence under `../build/qa/half-ui/`: `build-qt5-final.log`,
`build-qt6-final.log`, `tests-qt5.log`, `layout-qt6.log`, `screenshots.log`,
`package.log`, `portable.log`, `packaged-api.log`, `installers.log`,
`installers-check.log`, `site.log` and `release-tools.log`.
