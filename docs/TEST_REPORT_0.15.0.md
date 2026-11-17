# Stel 0.15.0 — Remote Control API

Verified locally on Linux, 2026-11-17.

- Qt 5.15.15 and Qt 6 builds pass. The API test suite has 18 passing results on each, with no failures or skipped cases.
- Real HTTP requests cover Bearer/Basic authentication, token rotation, IP/CIDR checks, malformed HTTP framing, input validation and rejected browser origins. API tokens and SIP passwords are absent from settings/status responses.
- Real SIP calls cover voice/video dial and answer, rejection, DTMF reception, hold/resume, mute, recording, forward, blind transfer, attended transfer with two call IDs, automatic consultation transfer and conference.
- API tests exchange Persian SIP messages, mark them read, receive actual BLF SUBSCRIBE/NOTIFY status, and check the main UI stays inline. The included Python client is exercised against the running application.
- Typed Persian text, uploaded TXT and PDF prepare successfully as A4 documents. Documents survive API restart. Both T.38 and G.711 fax are sent and received through the API, with completed page counts and TIFF downloads.
- HTTPS certificate verification passes through the SDK TLS backend. The packaged binary also passes HTTPS, authentication, status, DND, Yealink command and JPEG screenshot checks with development library/plugin paths removed.
- The existing eight native test groups pass: core, paging, BLF, messages, fax documents, fax, UI and TLS registration. The API group was rerun after fixing a test that read history before call termination; its final CTest result passes in 60.15 seconds.
- The Linux portable dependency closure audit passes: 225 ELF files, glibc 2.39 minimum. Standalone startup/version checks pass from a Unicode/spaced path.
- deb/rpm payload, version, menu/icon metadata and extracted standalone launch checks pass. System-wide installation was not performed locally. All 8 release-tooling tests pass.
- Focused final account/transfer tests pass on Qt 5 and Qt 6 after exposing asynchronous SDK errors/notifications; final PDF/TXT upload tests also pass on both.
- The website has 30 current production-widget screenshots. Every English/Persian image preview passes at desktop and mobile sizes with no horizontal overflow.

Windows compilation, API tests, packaged HTTPS checks and installer checks are part of the existing GitHub Actions workflow. They were not executed on this Linux host. No commit, push or publication was performed.

Evidence is outside the source export in `../build/qa/`: `api-tests.log`,
`api-qt6-tests.log`, `api-all-tests.log`, `api-ctest-final.log`, `api-package.log`,
`api-portable.log`, `api-packaged-remote.log`, `api-installers-check.log`,
`api-final-events.log`, `api-files-final.log`, `api-site-check.log` and
`api-screenshots.log`.
