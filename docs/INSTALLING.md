# Install Stel

Download packages from [Releases](https://github.com/seifzadeh/stel/releases).
All formats for one release contain the same application and bundled libraries.
Packages and installers are currently unsigned.

## Windows

Open `Stel-X.Y.Z-windows-x64-setup.exe` and follow the setup wizard. It installs
for the current user, adds a Start-menu entry and offers a desktop shortcut.
Administrator access and separate Qt/SDK/Visual C++ runtime installation are
not required. Close Stel before upgrading or uninstalling; setup never ends
an active call automatically.

Remove Stel through **Settings → Apps** or **Uninstall Stel** in the Start menu.
Accounts, saved credentials, messages and fax history remain in your profile.
The portable `Stel-X.Y.Z-windows-x64.exe` remains available without installation.
Windows 7/8.1 compatibility still requires testing on those systems; installer
compatibility alone does not verify the application and its media drivers.

## Debian / Ubuntu

Open the `.deb` in your software installer, or use:

```sh
sudo apt install ./Stel-X.Y.Z-linux-x86_64.deb
```

## Fedora / RPM

```sh
sudo dnf install ./Stel-X.Y.Z-linux-x86_64.rpm
```

The package adds **Stel** to the application menu; `stel` also works in a terminal.
Remove it with `sudo apt remove stel` or `sudo dnf remove stel`. User accounts
and history are kept. A Secret Service password store is needed to remember
passwords; packages recommend a desktop keyring provider.

These are x86-64 packages. The current build requires **glibc 2.39 or newer**;
package managers enforce the minimum recorded in each build's bundle manifest.
Changing the package format does not make an older distribution compatible.
The `.run` and `.tar.gz` portable formats remain available.

## نصب به فارسی

در ویندوز فایل **Setup.exe** را اجرا کن. برنامه در حساب همین کاربر نصب می‌شود
و از منوی Start یا میان‌بر دسکتاپ اجرا می‌شود. برای ارتقا یا حذف، ابتدا Stel را
ببند. اطلاعات اکانت‌ها و تاریخچه با حذف برنامه پاک نمی‌شوند.

در دبیان و اوبونتو، **deb** و در فدورا، **rpm** را نصب کن؛ دستورهای بالا هم
قابل استفاده‌اند. بعد از نصب، Stel در منوی برنامه‌هاست. نسخهٔ ویندوز و لینوکس
کتابخانه‌های برنامه را همراه خود دارند؛ نصب جداگانهٔ Qt یا Linphone لازم نیست.
بستهٔ لینوکس به glibc 2.39 یا جدیدتر نیاز دارد.
