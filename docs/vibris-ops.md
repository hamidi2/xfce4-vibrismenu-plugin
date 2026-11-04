# runbook ساخت / استقرار / دیباگ — فورک vibris

## 1. ساخت

```bash
ninja -C /opt/whisker-src/src-patched/build
```

- درخت کد: `/opt/whisker-src/src-patched/` — دایرکتوری build از قبل با meson تنظیم شده (`/opt/whisker-src/src-patched/build/build.ninja`).
- خروجی: `/opt/whisker-src/src-patched/build/panel-plugin/libwhiskermenu.so`
- اگر build از صفر لازم شد: `meson setup build` داخل src-patched.
- وابستگی‌ها (از README قدیمی): `libxfce4panel-2.0-dev libxfce4ui-2-dev libgarcon-1-dev libxfconf-0-dev libexo-2-dev meson ninja-build`.
- نکتهٔ publish: جابه‌جایی rename بین درخت‌ها (مثلاً cp از repo به src-patched) ممکن است به rebuild کامل نیاز داشته باشد.

## 2. استقرار (deploy)

```bash
sudo cp /opt/whisker-src/src-patched/build/panel-plugin/libwhiskermenu.so \
        /usr/lib/x86_64-linux-gnu/xfce4/panel/plugins/libwhiskermenu.so
md5sum /usr/lib/x86_64-linux-gnu/xfce4/panel/plugins/libwhiskermenu.so \
       /opt/whisker-src/src-patched/build/panel-plugin/libwhiskermenu.so
```

- md5 دو فایل باید یکسان باشد — این تأیید deploy است.
- نسخهٔ اصل دبیان در کنار همان مسیر به‌صورت `libwhiskermenu.so.distro-2.10.1` نگهداری می‌شود (بکاپ).
- `apt upgrade` باینری را برمی‌گرداند → بعد از هر آپدیت، دوباره build + cp لازم است.
- نمونهٔ md5 فعلی (برای مقایسهٔ شکل خروجی، نه مقدار): deployed `af9e8451ac43d8d610f4d1a86961292d`، build `3cbf57c373113318e3a83b4146ce0295` — اگر این دو فرق دارند یعنی آخرین build هنوز deploy نشده.

## 3. بارگذاری مجدد پلاگین — مهم‌ترین نکته

پلاگین به‌صورت **پروسهٔ external** اجرا می‌شود (wrapper-2.0 پروسهٔ مستقلی است که libwhiskermenu.so را dlopen می‌کند و xfce4-panel فقط یک proxy دارد). بنابراین:

- **Restart کردن نوار پنل پلاگین را reload نمی‌کند.** پس از cp، پروسهٔ کهنه با .so کهنه در حافظه باقی می‌ماند.

مرحلهٔ صحیح بعد از هر deploy:

```bash
pkill -f "wrapper-2.0.*libwhiskermenu"
```

پنل خودش wrapper را respawn می‌کند و این‌بار .so جدید load می‌شود.

**تأیید اینکه پروسهٔ نو است** (کیهیا: باید بعد از pkill باشد):
- زمان شروع پروسه:
  ```bash
  pgrep -af "wrapper-2.0.*libwhiskermenu"          # پیدا کردن pid
  ps -o pid,lstart,cmd -p <pid>                    # lstart باید بعد از pkill باشد
  ```
- ترفند inode از `/proc/<pid>/maps`: inode فایل map شده باید با inode فایل deploy‌شده یکی باشد:
  ```bash
  stat -c '%i' /usr/lib/x86_64-linux-gnu/xfce4/panel/plugins/libwhiskermenu.so
  grep libwhiskermenu /proc/<pid>/maps | head -1    # ستون inode
  ```
  اگر inode نقشه با inode فایل روی دیسک فرق داشت، پروسه هنوز نسخهٔ کهنه را در حافظه دارد (فایل قدیمی unlink نمی‌شود مگر همه پروسه‌ها رها کنند).

### restart نوار پنل (برای مواردی که خود پنل هم لازم است)

هر بار با unit یکتا تا تداخل با systemd-run قبلی پیش نیاید:

```bash
systemd-run --user --unit=panel-restoreN xfce4-panel
```

`N` را در هر اجرا یک عدد جدید بگذارید (panel-restore1، panel-restore2، …). خارج کردن: `systemctl --user stop panel-restoreN`.

## 4. لاگ‌های پروب (فعلاً فعال — پس از تثبیت باید حذف شوند)

| فایل | نویسنده | محتوا |
|---|---|---|
| `/tmp/wm-width.log` | `Window::set_size` (هر فراخوانی) + `LauncherIconView::update_item_width` | خطوط `set_size: old->new ... iw=... lav_nt=... cat_nt=... used=...` و `item-width: text=... icon=... xpad=... -> iw=... \| cell min/nat/wrap/scale` |
| `/tmp/wm-geo.log` | `FavoritesPage::set_menu_items` و `sync_capacity` | `layout: n/capacity/used/slots/iw/alloc_h/...` و `sync_cap: win/view/chrome/pitch/cap/built/scale` |
| `/tmp/wm-dump.log` | `Window::show` (بعد از sync_capacity، قبل از present؛ mode "w" = هر بار بازنویسی) | dump کامل درخت ویجت: type/name/alloc/min/nat برای همه — ابزار اصلی تشخیص مشکل عرض ۶۲۲ |

مشاهدهٔ زنده:
```bash
tail -f /tmp/wm-width.log /tmp/wm-geo.log
# دامپ ثابت آخرین show:
less /tmp/wm-dump.log
```
پاک‌سازی: این سه فایل را قبل از تست تمیز حذف کنید تا خطوط قدیمی گمراه نکند: `rm -f /tmp/wm-width.log /tmp/wm-geo.log /tmp/wm-dump.log`.

(لاگ قدیمی `/tmp/wm-debug.log` و env دیباگ `VIBRIS_DEBUG_WHITE` از نسخه‌های قبلی‌اند و در کد فعلی نوشته نمی‌شوند / فقط مربوط به بافر اولین فریم کامیت 36deea5.)

## 5. rollback

نقطهٔ بازگشت تأییدشده: کامیت `b4de5a0` (gravity SOUTH_WEST — آخرین وضعیت پایدار قبل از f456d07):

```bash
cd /opt/whisker-src/repo
git checkout b4de5a0 -- panel-plugin/   # یا کامل: git checkout b4de5a0 (detached)
cp -a /opt/whisker-src/repo/panel-plugin/. /opt/whisker-src/src-patched/panel-plugin/
ninja -C /opt/whisker-src/src-patched/build
sudo cp /opt/whisker-src/src-patched/build/panel-plugin/libwhiskermenu.so \
        /usr/lib/x86_64-linux-gnu/xfce4/panel/plugins/libwhiskermenu.so
pkill -f "wrapper-2.0.*libwhiskermenu"
```

(اگر تغییرات commit‌نشدهٔ src-patched را می‌خواهید نگه دارید، قبل از کپی از آن‌ها بکاپ بگیرید — مثلاً `cp -a /opt/whisker-src/src-patched /opt/whisker-src/src-patched.bak-uncommitted`.)

## 6. مسائل شناخته‌شده (به‌روز: وضعیت درخت commit‌نشده)

1. **عرض منو به ۶۲۲ قفل می‌شود (انتظار ۵۶۱)** — باز و تحت بررسی. علت: GTK minimum عرض پنجره؛ GtkIconView min = icon + spacing + text-cell-natural + spacing، ولی text cell مقدار `wrap-width = item_width فعلی` (≈294) را natural گزارش می‌کند در حالی که measure با فونت تم می‌گوید 236 — mismatch فونت/measure رندرر متن، حلقهٔ خودارجاعی wrap-width. فیکس‌های فعلی: `gtk_widget_set_size_request(view, 1, 1)` در `Page::Page`، فرمول `used*s_item_width+(used-1)*6` در `set_size`، و `update_item_width` جدید که از natural رندرر متن صرف‌نظر می‌کند. ابزار تشخیص: `/tmp/wm-dump.log` (IconView min=354 مشاهده شده) و پروب `item-width:` در `/tmp/wm-width.log`.
2. **پیرپیر (jitter) لبهٔ پایین حین درگ لبهٔ بالا** — در بزرگ‌تری حل شده (merged ConfigureWindow + gravity SOUTH_WEST + resize-only حین درگ + y-lock در کف min_h)؛ باقی‌ماندهٔ جزئی در برخی شرایط گزارش شده و به خواندن position بعد از apply (`apply_target_size`) ربط داده می‌شود.
3. **باز شدن منو در مرکز صفحه بعد از PrtScr در حین درگ** — بعد از یک اسکرین‌شات حین درگ، جابه‌جایی منو به حالت center می‌رود (برهم‌خوردن state موقعیت). موارد مرتبط: مسیر `resize_end` دیگر به button position برنمی‌گردد؛ بررسی مسیر `PositionAtCenter` / `center_window`.
4. **افکت‌های ظاهری مرتبط**: بدون compositor، GTK اسکرول‌بار overlay را سنتی مپ می‌کند حتی با overlay=TRUE — به همین دلیل policy نهایی NEVER + رزرو 17px در set_size شد؛ اگر سرریز ظاهر/محو می‌شود ولی عرض نه، اول `/tmp/wm-width.log` را چک کنید.
5. **هر apt upgrade نسخهٔ دبیان را برمی‌گرداند** — پس از آپدیت، deploy دوباره لازم است (بخش 2).

## 7. چک‌لیست پس از هر deploy

1. `ninja -C /opt/whisker-src/src-patched/build` — بدون خطا.
2. `sudo cp ... && md5sum` — دو md5 برابر.
3. `rm -f /tmp/wm-width.log /tmp/wm-geo.log /tmp/wm-dump.log`
4. `pkill -f "wrapper-2.0.*libwhiskermenu"`
5. تأیید پروسهٔ نو (lstart یا inode maps).
6. باز کردن منو؛ بررسی سه لاگ؛ در صورت رفتار غیرمنتظره `/tmp/wm-dump.log` را برای min/nat ویجت‌ها ببینید.
