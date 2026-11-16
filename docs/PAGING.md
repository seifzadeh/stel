# SIP paging and intercom

Stel 0.11 receives pages sent by your PBX with `Alert-Info` or `Call-Info`.
Ordinary calls still use their own **Auto Answer** setting.

Open **Settings → Accounts → Paging**. Each account has:

- **Accept SIP paging / intercom**: enabled by default.
- **Mute microphone when answering pages**: enabled by default. Turn it off
  for two-way intercom when the PBX allows two-way media.
- **Play a short tone**: enabled by default, before answering the page.

Pages answer with audio only. Stel selects a speaker when the audio driver
identifies one; otherwise it uses the output selected in Audio settings.
Dummy speaker selection is preserved for testing. The call window shows
**PAGING / INTERCOM**, the caller and the microphone state. You can mute,
unmute or hang up with the normal call controls.

Forwarding and Do not disturb take priority. A page arriving during another
call waits for manual answer and does not put that call on hold. The two local
legs of one call between accounts are treated as the same call.

## PBX headers

These immediate-answer forms are recognized without enabling ordinary Auto Answer:

```sip
Alert-Info: Auto Answer
Alert-Info: <Auto Answer>
Alert-Info: <sip:pbx@example.com>;info=alert-autoanswer;delay=0
Alert-Info: answer-after=0
Alert-Info: Intercom
Call-Info: <sip:pbx@example.com>;answer-after=0
```

Names and markers are case-insensitive; spaces and quoted parameter values are
accepted. `intercom=true` is also recognized. An explicit `Call-Info` answering
instruction takes priority over `Alert-Info`. Only immediate `answer-after=0`
is supported. Nonzero or malformed delay values do not authorize paging pickup.
A normal distinctive ringtone, or an auto-answer-looking word inside a URL,
does not trigger paging.

These forms follow Yealink's documented [auto-answer headers](https://support.yealink.com/docs/faq/call-feature/ad424f42247542759c247c28063b7393)
and [intercom configuration](https://support.yealink.com/docs/ax83h/intercom/ac7b3ecb3f0b4a2987ab54807e834f3f).
Stel reads the initial INVITE through liblinphone's
[remote call parameters](https://download.linphone.org/releases/docs/liblinphone/5.3/c/group__call__control.html).

## استفاده

در **Settings → Accounts → Paging** دریافت Page را برای هر اکانت تنظیم کن.
به‌صورت پیش‌فرض، Page با یک بوق کوتاه و میکروفون بسته پاسخ داده می‌شود؛
تماس عادی خودکار پاسخ داده نمی‌شود. برای اینترکام دوطرفه، تیک بستن میکروفون
را بردار؛ PBX هم باید مدیای دوطرفه را پشتیبانی کند.

اگر تماس دیگری برقرار باشد، Page آن را قطع یا Hold نمی‌کند و برای پاسخ دستی
منتظر می‌ماند. DND و فوروارد هم اولویت دارند. در پنجرهٔ تماس، نام تماس‌گیرنده
و حالت Paging مشخص است و می‌توانی تماس را قطع یا میکروفون را باز و بسته کنی.

## Screenshots

[Incoming page](../website/assets/screenshots/v0.11.0/paging.png) · [Call window](../website/assets/screenshots/v0.11.0/paging-call.png) · [Account settings](../website/assets/screenshots/v0.11.0/account-paging.png)
