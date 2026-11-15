# Stel 0.9.0 — text and PDF fax input

Verified locally on Linux x86_64, 2026-11-15, with Qt 5.15.15,
Qt 6.8.2 and the patched liblinphone 5.3.105 fax SDK.

- All six native CTest groups pass: core, messages, fax documents, fax,
  Settings UI and mutual-TLS registration. Total: 345.46 seconds.
- Nine document tests pass with both Qt 5 and Qt 6. Inputs include mixed-size
  multipage PDF, Persian/English typed text, UTF-8/UTF-16/Windows-1256 TXT and
  images. Checks cover fine-resolution A4 geometry, 10 mm margins, physical
  aspect ratio, pagination, form-feed, resending without repeated scaling,
  invalid input, cancellation before calling and the internal preview.
- Loopback SIP fax tests send PDF with T.38, TXT with G.711 and prepared typed
  text between two accounts in one core. Received page pixels match the
  prepared outgoing TIFF. A separate two-page PDF test includes Letter and
  landscape pages, verifies both pages after T.38 reception, and passes in
  31.21 seconds. Other fax tests cover UDPTL packet loss and default ICE.
- Visual inspection confirms joined Persian glyphs, ZWNJ, mixed text, A4
  margins, intact PDF borders and preview navigation. The Fax form scrolls
  on smaller windows instead of clipping input fields.
- MuPDF builds using its checksum-pinned original source release and upstream
  static-library Make targets. Its source extraction also passes without
  symlinks, avoiding Windows symlink privilege requirements.
- Clean portable-runtime tests also pass: nine document results, plus PDF/T.38
  and TXT/G.711 SIP transfers (53.32 seconds). They use only bundled runtime
  libraries, with development search paths removed. The user package omits QA
  executables; its application and shared runtime match the tested QA bundle.
- Release-boundary tests, Python/Bash/PowerShell syntax checks and Actions
  workflow lint pass. No commit, push or release publication is part of this
  feature update.

Fax tests use a generated loopback PBX and documents. Interoperability with a
physical fax or external PBX has not been established by these tests. Windows
build/package recipes include the PDF renderer and font, but native Windows
build/runtime and Windows 7/8.1 compatibility are not verified on this host.
The Linux package bundles its runtime; fully static linking is not claimed.

Detailed logs and full-size QA images are under the development workspace's
`build/qa/fax-document*`, `pdf-qt6-*` and `fax-pdf-multipage-sip.log`, outside
the exported application source.
