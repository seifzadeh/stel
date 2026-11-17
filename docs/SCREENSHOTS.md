# Application screenshots

The website and both READMEs use PNGs captured directly from Stel's production
Qt widgets. The current set is under `website/assets/screenshots/v0.15.0/`.
`capture.json` records the application/SDK/Qt versions, capture date, source
revision, image dimensions and SHA-256 hashes.

## Captured pages

| Page | صفحه |
| --- | --- |
| [Main screen · active call](../website/assets/screenshots/v0.15.0/main-window.png) | [صفحهٔ اصلی، تماس فعال](../website/assets/screenshots/v0.15.0/main-window.png) |
| [Dialling](../website/assets/screenshots/v0.15.0/main-idle.png) | [شماره‌گیری](../website/assets/screenshots/v0.15.0/main-idle.png) |
| [Incoming call](../website/assets/screenshots/v0.15.0/incoming-call.png) | [تماس ورودی](../website/assets/screenshots/v0.15.0/incoming-call.png) |
| [Voice call and DTMF](../website/assets/screenshots/v0.15.0/call.png) | [تماس صوتی و DTMF](../website/assets/screenshots/v0.15.0/call.png) |
| [Video call](../website/assets/screenshots/v0.15.0/video-call.png) | [تماس تصویری](../website/assets/screenshots/v0.15.0/video-call.png) |
| [Expanded video](../website/assets/screenshots/v0.15.0/video-expanded.png) | [پنجرهٔ تصویر بزرگ](../website/assets/screenshots/v0.15.0/video-expanded.png) |
| [Transfer on the LCD](../website/assets/screenshots/v0.15.0/call-transfer.png) | [انتقال داخل LCD](../website/assets/screenshots/v0.15.0/call-transfer.png) |
| [Conference selection](../website/assets/screenshots/v0.15.0/call-conference.png) | [انتخاب تماس کنفرانس](../website/assets/screenshots/v0.15.0/call-conference.png) |
| [Call details](../website/assets/screenshots/v0.15.0/call-details.png) | [جزئیات تماس](../website/assets/screenshots/v0.15.0/call-details.png) |
| [Incoming SIP page](../website/assets/screenshots/v0.15.0/paging.png) | [دریافت Page](../website/assets/screenshots/v0.15.0/paging.png) |
| [Paging details on the LCD](../website/assets/screenshots/v0.15.0/paging-call.png) | [جزئیات Paging در LCD](../website/assets/screenshots/v0.15.0/paging-call.png) |
| [Persian and English chat](../website/assets/screenshots/v0.15.0/messages.png) | [پیام‌های فارسی و انگلیسی](../website/assets/screenshots/v0.15.0/messages.png) |
| [Contacts](../website/assets/screenshots/v0.15.0/contacts.png) | [مخاطبین](../website/assets/screenshots/v0.15.0/contacts.png) |
| [Call history](../website/assets/screenshots/v0.15.0/history.png) | [سابقهٔ تماس](../website/assets/screenshots/v0.15.0/history.png) |
| [Send fax](../website/assets/screenshots/v0.15.0/fax-send.png) | [ارسال فکس](../website/assets/screenshots/v0.15.0/fax-send.png) |
| [Fax inbox](../website/assets/screenshots/v0.15.0/fax.png) | [صندوق دریافت فکس](../website/assets/screenshots/v0.15.0/fax.png) |
| [Fax outbox](../website/assets/screenshots/v0.15.0/fax-outbox.png) | [صندوق ارسال فکس](../website/assets/screenshots/v0.15.0/fax-outbox.png) |
| [A4 fax preview](../website/assets/screenshots/v0.15.0/fax-preview.png) | [پیش‌نمایش A4 فکس](../website/assets/screenshots/v0.15.0/fax-preview.png) |
| [Account settings](../website/assets/screenshots/v0.15.0/accounts.png) | [مدیریت اکانت‌ها](../website/assets/screenshots/v0.15.0/accounts.png) |
| [Account TLS certificate](../website/assets/screenshots/v0.15.0/account-security.png) | [گواهی TLS اکانت](../website/assets/screenshots/v0.15.0/account-security.png) |
| [NAT and ICE](../website/assets/screenshots/v0.15.0/account-nat.png) | [تنظیمات NAT و ICE](../website/assets/screenshots/v0.15.0/account-nat.png) |
| [Call forwarding](../website/assets/screenshots/v0.15.0/account-forwarding.png) | [فوروارد تماس](../website/assets/screenshots/v0.15.0/account-forwarding.png) |
| [Paging settings](../website/assets/screenshots/v0.15.0/account-paging.png) | [تنظیمات Paging](../website/assets/screenshots/v0.15.0/account-paging.png) |
| [Audio and codecs](../website/assets/screenshots/v0.15.0/codecs.png) | [صدا و کدک‌ها](../website/assets/screenshots/v0.15.0/codecs.png) |
| [Video and codecs](../website/assets/screenshots/v0.15.0/video-codecs.png) | [تصویر و کدک‌ها](../website/assets/screenshots/v0.15.0/video-codecs.png) |
| [Signalling and media](../website/assets/screenshots/v0.15.0/network.png) | [سیگنال و مدیا](../website/assets/screenshots/v0.15.0/network.png) |
| [Server certificate checks](../website/assets/screenshots/v0.15.0/security.png) | [بررسی گواهی سرور](../website/assets/screenshots/v0.15.0/security.png) |
| [Expansion · Speed Dial and BLF](../website/assets/screenshots/v0.15.0/expansion.png) | [تنظیم کلیدهای Expansion](../website/assets/screenshots/v0.15.0/expansion.png) |
| [About Stel](../website/assets/screenshots/v0.15.0/about.png) | [دربارهٔ Stel](../website/assets/screenshots/v0.15.0/about.png) |
| [Remote Control API](../website/assets/screenshots/v0.15.0/remote-api.png) | [تنظیم API کنترل از راه دور](../website/assets/screenshots/v0.15.0/remote-api.png) |


## Refresh on Linux

Use a configured development build with `STEL_BUILD_TESTS=ON` and the normal
runtime library paths from [BUILDING.md](BUILDING.md), then run:

```sh
bash scripts/capture-screenshots.sh build build/screenshots
```

The helper creates a private application directory and disposable Secret Service,
registers two demo accounts on a loopback PBX, adds demo contacts, makes
voice/video calls on the main LCD, expands and docks video, exchanges Persian/English SIP messages and sends a two-page
T.38 fax, then receives an Alert-Info page. Audio and video use the built-in dummy devices. Call history comes
from those calls. The captures cover all main pages, the fax send/inbox/outbox
views and every Settings tab. Settings, credentials and history belong to this
temporary session. A normal run takes about a minute.

Review the images, copy them and `capture.json` into the versioned website asset
directory, then update the README, hero/Open Graph image and gallery paths. Keep
the gallery's English/Persian captions and width/height attributes in sync with
the images. Versioned paths let browsers load the refreshed screenshots.

Build and check the website with `python3 scripts/build-site.py` before publishing.
The capture executable is an optional development target and is not included in
user packages.
