# Expansion keys / کلیدهای قابل تنظیم

The 12 keys below the display work like phone expansion keys.
Open **Settings → Expansion**, or right-click a key. Choose its type, label,
account and extension / SIP address, then press **Apply expansion keys**.
Clicking an unused key also opens its settings.

- **Speed Dial**: one click immediately calls the destination through that key's account.
- **BLF**: monitors the extension through the selected account; clicking also calls it.
- **Disabled**: leaves the key unassigned. Use **Clear selected key** to clear its fields.

BLF lamps show **green** for available, **red** for an active call, and flashing
red for ringing. Grey means unknown or unavailable; amber means waiting for the
PBX. Hover over a key or check its Status column for the subscription result.
An unknown extension is never assumed to be available.

The PBX must allow the account to subscribe to the extension's **dialog** event
and send `application/dialog-info+xml` notifications (RFC 4235). This is the
same SUBSCRIBE/NOTIFY mechanism used by Yealink BLF. On Asterisk/FreePBX, configure
extension hints and subscription permissions in the account's context. Enabling
ordinary presence or Auto Answer does not enable BLF by itself.

Subscriptions use the key's SIP account, transport, authentication and routing.
They renew automatically, stop when the key is cleared or its account is
unregistered, and retry after subscription errors. Full and partial dialog
notifications are supported, including more than one call on an extension.
This implementation uses individual dialog subscriptions; event-list/RLS BLF
and directed call pickup are not part of these two key types.

## راهنما به فارسی

۱۲ کلید زیر نمایشگر، پنل Expansion هستند. روی کلید خالی کلیک کن یا روی هر کلید
راست‌کلیک کن؛ تنظیمات همان کلید باز می‌شود. از **Settings → Expansion** هم می‌توانی
همه را ببینی. نوع کلید، نام دلخواه، اکانت و شمارهٔ داخلی را مشخص کن و
**Apply expansion keys** را بزن.

- **Speed Dial**: با یک کلیک، فوراً از اکانت همان کلید تماس می‌گیرد.
- **BLF**: وضعیت داخلی را از PBX می‌گیرد؛ با کلیک هم می‌توانی به همان داخلی زنگ بزنی.
- **Disabled**: کلید غیرفعال است.

چراغ سبز یعنی آزاد، قرمز یعنی در حال مکالمه و قرمز چشمک‌زن یعنی در حال زنگ‌خوردن.
خاکستری یعنی وضعیت مشخص نیست؛ زرد یعنی برنامه منتظر پاسخ PBX است.
خطای اشتراک را در ستون Status یا با نگه‌داشتن ماوس روی کلید می‌بینی.
تنظیمات کلیدها بعد از بستن و اجرای مجدد برنامه حفظ می‌شوند.

روی PBX باید BLF فعال باشد. در Asterisk/FreePBX، داخلی باید Hint داشته باشد و
اکانت هم اجازهٔ Subscribe به آن را داشته باشد. در صورت قطع اشتراک، برنامه
وضعیت را آزاد نشان نمی‌دهد و برای اتصال مجدد تلاش می‌کند.

Protocol references: [RFC 4235](https://www.rfc-editor.org/rfc/rfc4235),
[Yealink BLF](https://support.yealink.com/docs/sip-t48u/busy-lamp-field/0eae2a30bf704a3cb78f10be20d5f6f9).
