# Half-size interface and display scaling

Stel 0.15.1 renders the interface at 50% of its previous compact dimensions.
At 100% desktop scaling, the phone opens at 420 × 315 logical pixels,
Settings at 420 × 300, and the optional call window at 230 × 290.
The nominal sizes before application scaling are 840 × 630, 840 × 600
and 460 × 580 respectively. Windows can still be resized.

The same factor applies to fonts, icons, field heights, buttons, spacing,
scrollbars, menus and dialogs. The phone and fax artwork scale with their
controls. Small lengths round to whole pixels. Fax documents keep their
standard A4 page size; only their on-screen preview becomes smaller.

Application sizing is separate from the monitor's DPI setting. Windows
125%, 150%, 175%, 200%, 250% and 300% scaling still work normally, including
when moving between monitors. Fractional DPI scales are preserved.
Windows fit the current monitor's work area, with space for the taskbar
and title bar. A completely off-screen window is brought back.

Settings keeps Close outside scrolling content and account
Save/Register/Unregister beside the registration status. Each tab scrolls
independently. Scroll within a page to reach fields below the visible area.

`stel_layout_tests` exercises the actual windows at seven display scales:
100%, 125%, 150%, 175%, 200%, 250% and 300%. It checks sizes, visible controls,
keypad input, scrolling, screen bounds and off-screen recovery.

## رابط با نصف اندازه

تمام رابط با اندازهٔ ۵۰٪ نمایش داده می‌شود؛ فونت، آیکن، فاصله‌ها، کادرها و
دکمه‌ها هم با همین نسبت کوچک شده‌اند. اندازهٔ پیش‌فرض صفحهٔ اصلی ۴۲۰×۳۱۵،
تنظیمات ۴۲۰×۳۰۰ و پنجرهٔ تماس ۲۳۰×۲۹۰ است. این اندازه‌ها برای مقیاس ۱۰۰٪
دسکتاپ هستند؛ مقیاس نمایش ویندوز هم طبق معمول اعمال می‌شود.

پنجره‌ها همچنان قابل تغییر اندازه‌اند. Close بیرون بخش اسکرول تنظیمات است و
کلیدهای Save/Register/Unregister بالای فرم اکانت قرار دارند. اگر صفحه کوتاه
بود، داخل تب اسکرول کن. اندازهٔ سند فکس همچنان A4 است.
