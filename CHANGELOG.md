# Changelog

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
