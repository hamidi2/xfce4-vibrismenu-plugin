# نقشهٔ کد فورک vibris (وضعیت فعلی src-patched = HEAD f456d07 + تغییرات commit‌نشده)

این سند فقط فایل‌های `panel-plugin/` تغییر‌یافته نسبت به vanilla 2.10.1 را پوشش می‌دهد و برای هر فایل، توابعِ افزوده‌شده یا دست‌خوردهٔ اساسی را با توضیح «آنچه اکنون می‌کنند» فهرست می‌کند. فایل‌های تغییر‌نیافته (categories, applications-page, recent-page, search-page, query, resizer.cpp و…) ذکر نشده‌اند — البته توجه: `resizer.cpp/h` بدون تغییر است ولی فقط `Resizer::Top` در window.cpp نمونه‌سازی می‌شود.

مقایسه با vanilla: `diff -ru /tmp/vibris-doc-orig/xfce4-whiskermenu-plugin-2.10.1/panel-plugin /opt/whisker-src/src-patched/panel-plugin`

---

## panel-plugin/launcher-icon-view.cpp / .h

نمای دوستونهٔ لیستی favorites؛ قلب اندازه‌گیری عرض.

| تابع / عضو | وضعیت | توضیح |
|---|---|---|
| `LauncherIconView::LauncherIconView()` | اصلاح‌شده | نمای افقی دوستونه (`item_orientation(HORIZONTAL)`، `columns(2)`، `row_spacing(0)`، `column_spacing(6)`، `spacing(6)`)؛ رندرر آیکون چپ‌چین (`xalign 0.0`, `yalign 0.5`)؛ اتصال key-press → `wrap_navigation`؛ اعضای `m_authority`، `m_width_pending` |
| `static int s_item_width` | جدید، public | کش سراسری عرض یک آیتم favorites — بین همهٔ صفحات مشترک؛ مبنای فرمول عرض منو در `Window::set_size` |
| `update_item_width(bool measure)` | جدید (override از launcher-view.h) | با `measure=true`: عرض یک‌سطری عریض‌ترین نام را با Pango روی **فونت تم** (`gtk_style_context_get_font` + class `launchers`) حساب می‌کند؛ سپس عرض آیکون را از natural رندرر آیکون و `xpad` را از رندرر متن می‌گیرد (عمداً از natural رندرر متن استفاده **نمی‌کند** چون GtkIconView روی آن `wrap-width = item_width` فعلی ست می‌کند = حلقهٔ خودارجاعی) و `s_item_width = 2*item_pad + icon_w + 6 + 2*text_xpad + text_w` (کف 60). با `measure=false`: فقط اعمال کش. + پروب `/tmp/wm-width.log` (موقت) |
| `schedule_width_update()` | جدید | coalesce عرض با idle واحد (`m_width_pending`) |
| `set_width_authority()` | جدید | این نمایندگان مرجع اندازه‌گیری‌اند (فقط favorites صدا می‌زند) → `m_authority = true` |
| `set_model(GtkTreeModel*)` | اصلاح‌شده | بعد از ست مدل، `update_item_width(m_authority)` |
| `wrap_navigation(GdkEventKey*)` | جدید | ناوبری دوستونه: Down/Up با گام `cols` (چیدمان سطری GTK)، wrap بین ستون‌ها، placeholderها (`COLUMN_LAUNCHER == nullptr`) مقصد نمی‌شوند، Left/Right فقط در همان ردیف و روی سلول واقعی؛ اسکرول فوری حداقلی با GtkAdjustment (بدون انیمیشن)؛ با یک ستون، به GTK واگذار می‌کند |
| `reload_icon_size()` | اصلاح‌شده | `item_padding = 0` همیشه؛ سپس `update_item_width(true)` (اندازهٔ آیکون عوض شد → measure دوباره) |

`launcher-view.h`: دو متد virtual با پیاده‌سازی خالی (`update_item_width`، `set_width_authority`) تا `LauncherTreeView` هم سازگار بماند.

## panel-plugin/favorites-page.cpp / .h

صفحهٔ favorites؛ چیدمان تا-پر-شدن-ظرفیت و بازچینش زنده.

| تابع / عضو | وضعیت | توضیح |
|---|---|---|
| `static int s_measured_pitch` | جدید | pitch واقعی ردیف (cell height + row-spacing) اندازه‌گیری‌شده پس از اولین رندر؛ تخمین ظرفیت showهای بعدی |
| `static bool drag_in_progress` | جدید، public | فلگ سراسری DnD واقعی؛ گارد hover-activate دسته‌ها و hover-selection |
| `m_columns / m_used_cols / m_built_capacity / m_overflow / m_model_slots / m_pending_add / m_pending_remove / m_resync_source` | جدید | وضعیت چیدمان؛ `m_model_slots` نگاشت سلول‌های مدل (سطری) به ایندکس منطقی favorites، −1 = placeholder |
| `get_used_columns()` | جدید | تعداد ستون‌های دارای آیتم — مبنای عرض منو در `set_size` |
| `add(Launcher*)` / `remove(Launcher*)` | بازنویسی | فقط favorites list را دستکاری + `rebuild()`؛ مدل هیچ‌وقت دستی insert/remove نمی‌شود |
| `set_menu_items()` | بازنویسی کامل | `sort_favorites()`؛ تعیین `m_columns` از `/favorites-columns`؛ `capacity = favorites_capacity_rows()`؛ پر کردن `m_model_slots` در سه حالت (تک‌ستونه / سطری / تا-پر-شدن-ستونی با سرریز در ستون آخر)؛ ساخت GtkListStore با placeholder برای سلول‌های −1؛ `gtk_icon_view_set_columns(m_used_cols)`؛ policy عمودی دینامیک (`m_overflow ? AUTOMATIC : NEVER` — تغییر commit‌نشده)؛ پروب `/tmp/wm-geo.log` (`layout:`)؛ هندلر `selection-changed` که انتخابِ فقط-placeholder را خالی می‌کند |
| `extend_context_menu()` | بازنویسی | خالی — sort دستی حذف (sort خودکار صعودی) |
| `on_row_changed / on_row_inserted / on_row_deleted` | بازنویسی | به‌جای ویرایش positional در favorites: ثبت `m_pending_add` (با نگاشت placeholder-aware برای delete) + `schedule_resync()` |
| `sort_favorites()` | جدید (جای sort_ascending/descending) | sort صعودی خودکار بر اساس نام؛ شناسه‌های بدون لانچر ته لیست می‌مانند |
| `rebuild()` | جدید | `set_menu_items()` + `get_window()->sync_width()` |
| `on_height_changed()` | جدید | تغییر ارتفاع = تغییر ظرفیت → `rebuild()`؛ از `resize_end` صدا زده می‌شود |
| `schedule_resync()` | جدید | coalesce: حین درگ idle با `G_PRIORITY_HIGH_IDLE` (همان فریم)، خارج از درگ timeout 400ms |
| `resync_now()` | جدید | اعمال pending add/remove (+`update_min_h` حین درگ) + `rebuild()` |
| `sync_capacity()` | جدید | بعد از pump مخفی در `show()`: `update_item_width(true)` (measure بعد از realize)، خواندن pitch واقعی از cell rect ردیف 0 → `s_measured_pitch`؛ اگر ظرفیت جدید با `m_built_capacity` فرق دارد → `resync_now()`؛ پروب `/tmp/wm-geo.log` (`sync_cap:`) |
| `view_created()` | بازنویسی | `set_reorderable(true)`؛ DnD فلگ؛ `overlay-scrolling` مپ (کامیت‌های f456d07/src-patched در policy تداخل جزئی دارند — آخرین وضعیت: `NEVER, NEVER` در view_created و بازنویسی دینامیک در هر `set_menu_items`)؛ `min_content_height(1)`؛ هوک `size-allocate`: فقط حین درگ و فقط با تغییر واقعی ظرفیت → `schedule_resync` |

## panel-plugin/window.cpp / .h

پنجرهٔ منو؛ هندسه، درگ، عرض از محتوا.

| تابع / عضو | وضعیت | توضیح |
|---|---|---|
| `Window::Window(Plugin*)` | اصلاح‌شده | title «Vibris Menu»؛ `gtk_window_set_gravity(GDK_GRAVITY_SOUTH_WEST)`؛ فقط `Resizer::Top` باقی (بقیه nullptr، گرید تک‌سلولی)؛ CSS سراسری: `.command-button {padding/min 0}` و `frame.vibris-frame {border-left ...}`؛ هدر: pixel-size آیکون‌های command از `launcher_icon_size`، spacing/margin قطعی 6px (RTL-safe)؛ `m_panels_stack` non-hhomogeneous + هندلرهای `notify::visible-child` برای re-size در تعویض صفحه؛ گارد DnD برای همهٔ صفحات |
| `show(Position)` | اصلاح‌شده | workarea → `m_workarea`/`m_max_bottom`؛ realize مخفی + پمپ ≤25 رویداد؛ رندر در بافر ARGB32 قبل از present (حذف فلش اولین فریم؛ `VIBRIS_DEBUG_WHITE` فقط دیباگ)؛ `m_favorites->sync_capacity()`؛ present؛ پروب `/tmp/wm-dump.log` (commit‌نشده، mode "w")؛ clamp نهایی پایین به workarea |
| `favorites_capacity_rows() const` | جدید | pitch از `s_measured_pitch` یا فرمول؛ avail از «هندسهٔ خواسته − chrome ذخیره‌شده» (`m_last_chrome`) نه تخصیصِ عقب‌مانده؛ کسر `2*iv_margin`؛ return `avail / pitch` |
| `resize(dx,dy,dw,dh)` | بازنویسی | حل (y,h) در سه قید (workarea، `m_min_h` با y-lock در کف، `m_max_bottom`)؛ موشن فقط `m_target_*` را ست می‌کند؛ idle واحد (`m_size_idle_id`) → `apply_target_size()` |
| `apply_target_size()` | جدید | اعمال هدف: `set_size` + `check_scrollbar_needed` + `move_window`؛ سپس خواندن `gdk_window_get_position` و sync `m_geometry.x/y` با واقعیت (WM ممکن است کلمپ کند — پرش ۹۷px) |
| `sync_width()` | جدید | `set_size(10, m_geometry.height)` — فقط عرض از محتوا؛ بعد از هر `rebuild()` favorites |
| `update_min_h()` | جدید، بازنویسی‌شده (commit‌نشده) | کف درگ = chrome (`vbox_h − page_h`) + یک ردیف (`max(s_measured_pitch, 48)`) + 10 — نه کل محتوای vbox (که min کل صفحات GtkStack را می‌کشید و درگ را سفت می‌کرد) |
| `resize_start()` | اصلاح‌شده | فقط `m_resizing=true` + `update_min_h()` + focus؛ policy دست نمی‌خورد |
| `resize_end()` | اصلاح‌شده | اگر idle معلق است همگام اعمال؛ ذخیرهٔ menu_width/height در xfconf؛ **بدون** جابه‌جایی به button/center (منشأ پرش بعد از رها کردن)؛ `on_height_changed()` نهایی |
| `move_window()` | اصلاح‌شده | اگر GdkWindow هست: حین درگ فقط `gdk_window_resize` (gravity SOUTH_WEST پایین را pin می‌کند)؛ خارج از درگ `gdk_window_move_resize` merged (single ConfigureWindow — ریشهٔ پیرپیر)؛ بدون GdkWindow fallback به `gtk_window_resize + gtk_window_move` |
| `set_size(int,int)` (public شد) | بازنویسی | clamp به monitor؛ عرض از محتوا همیشه: favorites → `lav_nt = used * s_item_width + (used-1)*6` (fallback natural؛ فرمول commit‌نشده) + 17px اسکرول‌بار فقط با سرریز واقعی (`n > cols*capacity`)؛ دیگر صفحات → natural ویجت؛ `content_w = lav_nt + cat_nt + 26` با کف 300؛ سپس اگر تغییری: `m_geometry` + `gtk_widget_set_size_request` (حداقل toplevel — ضد «سُرخوردن»)؛ پروب `/tmp/wm-width.log` بی‌قید (commit‌نشده) |
| `check_scrollbar_needed()` | بدون تغییر (vanilla) | sidebar عمودی؛ نام‌ربا با اسکرول‌بار favorites |
| `on_key_press_event` | اصلاح‌شده | مسیر Esc: cancel ریزرهای non-null + حذف `m_size_idle_id` + set_size به مقادیر ذخیره‌شده |

window.h اضافه‌ها: `m_workarea`, `m_max_bottom`, `m_target_x/y/w/h`, `m_size_idle_id`, `m_min_h`, `m_last_chrome(mutable)`؛ متدهای بالا؛ `is_resizing()`.

## panel-plugin/page.cpp / page.h

| تابع | وضعیت | توضیح |
|---|---|---|
| `Page::Page(Window*, icon, text)` | اصلاح‌شده (commit‌نشده) | ScrolledWindow با policy `AUTOMATIC, AUTOMATIC` (NEVER افقی min عرضِ کل IconView را به min پنجره می‌کشید)؛ `gtk_widget_set_size_request(m_view->get_widget(), 1, 1)` — حذف کف غلط min GtkIconView (GTK درخواست ۵۶۱ را ۶۲۲ کلمپ می‌کرد) |

## panel-plugin/settings.cpp / .h

| عضو / تابع | وضعیت | توضیح |
|---|---|---|
| `favorites_columns`, `favorites_horizontal`, `favorites_scrollbar_overlay` | جدید | سه تنظیم xfconf: `/favorites-columns` (0..4)، `/favorites-horizontal`، `/favorites-scrollbar-overlay` (پیش‌فرض true؛ در نسخهٔ فعلی policy واقعی از آن مستقل شده) |
| `Settings::load` / `property_changed` | اصلاح‌شده | load سه تنظیم جدید؛ تغییر هرکدام یا `/menu-height` → `m_plugin->reload_menu()` (بازچینش زنده) |
| `m_button_title_default` | اصلاح‌شده | «Vibris Menu» |

## panel-plugin/settings-dialog.cpp

| تابع | وضعیت | توضیح |
|---|---|---|
| `SettingsDialog::SettingsDialog` | اصلاح‌شده | تیتر «Vibris Menu» |
| `SettingsDialog::init_behavior_tab` | اصلاح‌شده | بخش «Favorites Layout»: کمبو Number of columns (1..4)، چک‌باکس Fill across rows first، چک‌باکس Overlay scrollbar — همه با `m_plugin->reload_menu()` |

## panel-plugin/command.cpp

| تابع | وضعیت | توضیح |
|---|---|---|
| `Command::Command` | اصلاح‌شده (commit‌نشده) | آیکون دکمه‌های هدر از `GTK_ICON_SIZE_LARGE_TOOLBAR` به `GTK_ICON_SIZE_DIALOG` (بزرگ و بدون تاری) |

## panel-plugin/icon-renderer.cpp

| تابع | وضعیت | توضیح |
|---|---|---|
| `whiskermenu_icon_renderer_get_preferred_width` | اصلاح‌شده | حذف بونس `stretch` (`+76 − size/4` و natural دوبرابر)؛ minimum = natural = `pad*2 + size` — ضد بادکردن min ستون‌ها |

## panel-plugin/profile.cpp

| تابع | وضعیت | توضیح |
|---|---|---|
| `Profile::update_picture` | اصلاح‌شده | اندازهٔ آواتار = `launcher_icon_size.get_size()` (نه 32 ثابت)؛ crop مربع مرکزی + scale برای عکس‌های غیرمربعی؛ clip با inset اپتیکال 2px در هر دو حالت round و square |

## panel-plugin/plugin.cpp

| تابع | وضعیت | توضیح |
|---|---|---|
| `Plugin::Plugin` | اصلاح‌شده | `gtk_widget_set_valign(m_button_icon, CENTER)` |
| `Plugin::size_changed` | اصلاح‌شده | در پنل چندردیفه: `icon_size = icon_size * min(nrows,2) * 3 / 4` — سقف span دو ردیف |

## panel-plugin/category-button.cpp

| تابع | وضعیت | توضیح |
|---|---|---|
| هندلرهای enter-notify (hover-activate) | اصلاح‌شده | گارد `!FavoritesPage::drag_in_progress` — hover حین DnD صفحه عوض نمی‌کند |

## panel-plugin/whiskermenu.desktop.in

`Name=Vibris Menu` (commit‌نشده).

## panel-plugin/launcher-view.h

دو virtual جدید با default empty: `update_item_width(bool)`، `set_width_authority()`.
