# Changelog

## 0.15.1 — 2026-11-18

- Halve the whole compact interface: phone 420 × 315, Settings 420 × 300
  and call window 230 × 290. Scale fonts, icons, forms, controls and spacing
  together while keeping fax documents at standard A4 size.
- Preserve Windows fractional DPI scales instead of rounding 150/175% up.
  Fit windows to the current monitor work area, including taskbar/title bar space.
- Keep Settings Close outside scrolling content and give each page its own
  scrolling area, so tall pages do not enlarge the whole dialog.
- Put account Save/Register/Unregister actions near the registration status.
- Adapt the chat composer and fax icons to the smaller phone display.
- Add UI checks at 100%, 125%, 150%, 175%, 200%, 250% and 300% scaling to Linux/Windows CI.

## 0.15.0 — 2026-11-17

- Add Settings → Remote API, with token authentication, client IP/CIDR controls,
  local-only defaults and optional HTTPS using the existing SDK TLS backend.
- Support Yealink HTTP Action URI paths and common call commands: dial, answer,
  hang up, hold, mute, blind/attended transfer, conference and expansion keys.
- Add a JSON API for accounts/registration, call controls, devices/codecs,
  messages, contacts, history, BLF status, main-screen capture and event polling.
- Upload text, PDF, TXT or images, prepare A4 pages in the background, send or
  receive T.38/G.711 fax and download completed fax files through the API.
- Cover authentication, HTTP parsing, HTTPS, real SIP call operations, BLF,
  Persian messages, document conversion and two-way fax in integration tests.

## 0.14.0 — 2026-11-17

- Keep incoming and outgoing calls on the main phone display. Answer, reject,
  hold, mute, record and send DTMF without opening a separate window.
- Add Forward, Attended transfer and Fax controls to the LCD, with inline
  destination, conference selection and call-details panels.
- Show remote video and a local preview in the LCD corner. Click to expand,
  then use Return to phone to continue the call on the main screen.
- Keep disconnect confirmation when closing an expanded live-call window.
- Cover inline voice/video calls, real forwarding, attended transfer, conference,
  line selection and repeated video expansion with SIP and live media tests.

## 0.13.0 — 2026-11-16

- Replace the five navigation shortcuts under the display with 12 configurable
  expansion keys. Each key has its own label, SIP account and destination.
- Add one-click Speed Dial and Yealink-style BLF using SIP dialog subscriptions.
  Available extensions show green, busy extensions red, ringing flashes red,
  and unconfirmed or disconnected status stays grey.
- Add Settings → Expansion and right-click editing, with saved configurations,
  automatic subscription renewal and recovery, and multi-account routing.
- Safely drain pending BLF notifications when accounts reconnect or the app closes.
- Test real SIP SUBSCRIBE/NOTIFY, full and partial dialog updates, subscription
  rejection, direct dialing, UI status and configuration persistence.

## 0.12.0 — 2026-11-16

- Add a Windows setup wizard with per-user installation, Start-menu and optional
  desktop shortcuts, upgrades and an uninstaller that preserves user data.
- Add Debian/Ubuntu .deb and RPM installers with desktop integration. Both
  install the audited portable binary and declare its minimum glibc version.
- Build all installers in the existing release workflow. Gate publication on
  Debian, Fedora and Windows install/reinstall/launch/uninstall checks.
- Validate the explicit application version separately from runtime startup
  messages and include captured output in installer validation failures.
- Use native Windows source paths for installer icons and bundled notices.
- Keep portable downloads and dependency sources, checksum all new assets and
  include chassis artwork in the corresponding source archive.

## 0.11.0 — 2026-11-16

- Receive SIP paging and intercom through Yealink-compatible Alert-Info and
  Call-Info headers, independently of ordinary-call Auto Answer.
- Add per-account Paging controls. Pages answer with audio only, a muted
  microphone and a short local alert tone by default; two-way intercom is optional.
- Preserve active calls, forwarding, DND and account media-encryption policy.
  Show Page identity in the call window, main display and call history.
- Cover header parsing, actual PBX INVITEs, audio reception, RTP/SRTP,
  per-account settings, cancellation and microphone state across later calls.

## 0.10.0 — 2026-11-15

- Rebuild the main window around the silver desk-phone reference: handset, blue
  display, vertical navigation, line status, illuminated keys and physical dial pad.
- Keep the fax panel alongside the phone, with Send Fax, Inbox and Outbox,
  document thumbnails and optional editable A4 cover pages.
- Connect the physical keys to live calls, DTMF, hold, mute, recording, transfer,
  conference, redial and audio output selection. Retain per-call dialogs.
- Scale the whole chassis uniformly, support dragging/resizing and custom window
  controls, and align the logo and dialogs with the blue/silver appearance.
- Exercise the hardware controls on real loopback calls and verify cover-page
  order, A4 geometry and the three-page preview.
- Respect shared media when resuming either end of a call between accounts in
  the same application; avoid automatically placing the other end on hold.

## 0.9.0 — 2026-11-15

- Fix a call-recording crash on Windows: copy the assigned recording path before
  the SDK replaces call parameters when starting recording. Cover repeated
  start/stop on incoming and outgoing calls and the recording notification path.
- Send typed text, TXT and multipage PDF directly from the Fax tab; retain
  TIFF/PNG/JPEG inputs. Persian text uses a bundled Vazirmatn font.
- Convert every page to standard A4 fine-resolution fax with safe margins,
  preserved proportions and automatic text pagination. Preview all pages before
  sending; document conversion runs in the background and can be cancelled.
- Include the native MuPDF renderer, font and dependency notices in portable
  builds. Received faxes open in the internal multipage viewer.
- Keep Linux development test executables in opt-in QA bundles to reduce the
  user download size.
- Verify PDF, TXT and typed-text fax transmission through loopback SIP peers,
  alongside document geometry, mixed PDF sizes and Persian encoding tests.

## 0.8.0 — 2026-11-15

- Add a Fax tab with per-call T.38 (default) or G.711 pass-through selection,
  TIFF/image document preparation, live progress and a received-fax table.
- Add Fax to incoming call windows, local TIFF/history storage, export and
  T.30-confirmed success/failure reporting. Fax calls use private software modems.
- Extend the pinned Linphone SDK for native T.38 SDP/re-INVITE negotiation and
  UDPTL media, with SpanDSP fax and libtiff dependencies in portable packages.
- Preserve account security and voice-codec preferences; T.38 requires RTP media
  and reports negotiation failure without silently changing protocol.

## 0.7.1 — 2026-11-13

- Restyle the main navigation, Settings categories and account/license subtabs
  with rounded tabs, matching line icons and clear selected/hover/focus states.
- Draw navigation icons with Qt at the requested resolution for high-DPI displays,
  without adding an icon-font or image-plugin dependency. Retain the LTR layout.

## 0.7.0 — 2026-11-13

- Add Messages as the second main tab, with per-account SIP MESSAGE conversations,
  incoming/outgoing bubbles, local history, unread badges and send/failure states.
- Preserve the LTR interface while handling Persian shaping, bidirectional mixed
  text, ZWNJ, emoji and multiline UTF-8 input. Enter sends; Shift+Enter adds a line.
- Keep drafts per conversation and show failed sends with an explicit retry.
- Verify exact UTF-8 payloads between accounts in one core, independent cores and
  separate mutually authenticated TLS cores, plus chat persistence and UI input.

## 0.6.0 — 2026-11-09

- Settings → About shows the application/SDK/Qt versions, principal developer
  Mehrdad Seifzadeh, project links, open-source credits and offline license texts.
- Stel's original code and artwork use GPL-3.0-or-later. Upstream AGPL and other
  third-party licenses remain intact and are documented separately.
- GitHub Actions builds and tests Linux x86_64 and Windows x64, packages native
  single-file applications and publishes a versioned release only after both pass.
- GitHub Pages website includes actual application screenshots and release links.
- Existing multi-account calls, account Auto Answer/Caller ID Name, media settings,
  DTMF, hold, transfers, conferencing and confirmed call-window closing are retained.

## 0.5.3 — 2026-11-09

- Add independent per-account Auto Answer and standards-based SIP Caller ID Name.
- Preserve legacy caller display names when loading old account settings.

## 0.5.2 — 2026-11-09

- Confirm hangup when closing a live call dialog; No leaves the call controls open.

## 0.5.1 — 2026-11-09

- Default new accounts to RTP, with explicit opt-in mandatory SRTP.
- Fix answering calls between accounts in the same application.
