# مستند تغییرات vibris نسبت به vanilla xfce4-whiskermenu-plugin 2.10.1

این سند تمام تغییرات فورک «vibris» نسبت به سورس اصلی 2.10.1 را مستند می‌کند؛ به ترتیب کامیت، با توضیح هر hunk، فانکشن‌های درگیر، و دلیل (منبع دلیل: پیام کامیت‌ها و کامنت‌های فارسی داخل کد — بدون افزودنی از خودِ سند).

منابع:
- repo: `/opt/whisker-src/repo` (شاخه main). کامیت اول `cbdb203` = ایمپورت vanilla.
- درخت پچ‌شدهٔ جاری: `/opt/whisker-src/src-patched/` (HEAD = `f456d07` + تغییرات commit‌نشده).
- vanilla: `/tmp/vibris-doc-orig/xfce4-whiskermenu-plugin-2.10.1/` (اکسترکت از `/opt/whisker-src/whisker-2.10.1-orig.tar.xz`).

نکتهٔ اهمیت: کامیت `cbdb203` با tarball رسمی 2.10.1 بایت‌به‌بایت یکسان است (بررسی با `git archive cbdb203 panel-plugin` + `diff`). یعنی «خط پایه» واقعاً vanilla است و همهٔ تفاوت‌ها در کامیت‌های بعدی + تغییرات commit‌نشدهٔ درخت کاری است.

---

## 1. `cbdb203` — vanilla xfce4-whiskermenu-plugin 2.10.1

ایمپورت خالص سورس رسمی 2.10.1 (142 فایل، 48528 خط). هیچ تغییر منطقی.

---

## 2. `64b4dae` — customization: list-style two-column launcher, 48px icons/avatar, content-driven menu width, DoubleCmd wrap navigation

**نیت کلی** (پیام کامیت): تبدیل منوی علاقه‌مندی‌ها به «لیست دوستونهٔ چپ» با آیتم‌های افقی (آیکون کنار متن)، عرض منو از محتوا (نه عدد ثابت xfconf)، ناوبری wrap با کیبورد، آواتار 48px، و دکمه‌های هدر در اندازهٔ DIALOG. این کامیت اولین بازنویسی ساختاری است و ستون فقرات همهٔ کارهای بعدی (اندازه‌گیری عرض آیتم، `update_item_width`، ساختار `set_width_authority`) را می‌سازد.

علاوه بر تغییرات کد، فایل `README-HAMIDI.md` حذف شد — سند دور ۲۲-۲۳ که سه پچِ «پیش‌دوره» را توصیف می‌کرد (profile.cpp 48px، command.cpp DIALOG، launcher-icon-view horizontal/2 columns). یعنی این کامیت همان سه پچ قدیمی + خیلی بیشتر را به‌صورت واقعی وارد تاریخچهٔ git می‌کند. (توجه: خودِ فایل `command.cpp` در این کامیت تغییری نمی‌کند — نسخهٔ working-tree با `GTK_ICON_SIZE_DIALOG` در خط سازندهٔ Command هنوز commit‌نشده است؛ رجوع به بخش آخر سند.)

### favorites-page.cpp

- `FavoritesPage::set_menu_items()`: بعد از `get_view()->set_model(model)` دو فراخوانی جدید:
  ```cpp
  get_view()->set_width_authority();
  get_view()->update_item_width(true);
  ```
  یعنی صفحهٔ favorites «مرجع اندازه‌گیری عرض» است (فقط favorites عرض را از مدل خودش حساب می‌کند؛ دیگر صفحات صرفاً از کش استاتیک استفاده می‌کنند) و بلافاصله بعد از ست‌کردن مدل، عرض آیتم دوباره measure می‌شود.
- `on_row_changed`, `on_row_inserted`, `on_row_deleted`: در ابتدای هر سه، `get_view()->update_item_width(true)` اضافه شد — هر تغییر ردیفِ مدل (افزودن/حذف favorite، مثلاً با درگ) عرض عریض‌ترین نام را دوباره محاسبه می‌کند تا عرض منو همیشه از محتوای واقعی بیاید.

### icon-renderer.cpp — `whiskermenu_icon_renderer_get_preferred_width`

تغییر مهم در منطق minimum/natural رندرر آیکون. قبل:

```cpp
if (icon_renderer->stretch)
{
    width += 76 - (icon_renderer->size / 4);
    if (natural) *natural = (width * 2) - 1;
}
else if (natural) *natural = width;
if (minimum) *minimum = width;
```

بعد:

```cpp
if (minimum) *minimum = width;
if (natural) *natural = width;
```

پچِ vanilla وقتی `stretch=true` بود به عرض رندرر «پاداش» می‌داد (`+76 - size/4` و natural دوبرابر) — پیام کامیت می‌گوید این بونس «min-width ستون‌ها را باد می‌کرد» و با حذفش، عرض ستون‌ها فقط `pad*2 + size` می‌شود. این تغییر شرطی است که GtkIconView حداقل عرض ستون را از آیکون بگیرد و نه از عدد ساختگی stretch.

### launcher-icon-view.cpp

- سازندهٔ `LauncherIconView`:
  - تنظیمات جدید نمای دوستونهٔ افقی (کامنت: «لیست دوستونه: آیکون کنار متن (افقی) به‌جای زیرِ آن»):
    ```cpp
    gtk_icon_view_set_item_orientation(m_view, GTK_ORIENTATION_HORIZONTAL);
    gtk_icon_view_set_columns(m_view, 2);
    gtk_icon_view_set_row_spacing(m_view, 0);
    gtk_icon_view_set_column_spacing(m_view, 6);
    gtk_icon_view_set_spacing(m_view, 6);
    ```
  - `xalign` رندرر آیکون از `0.5` به `0.0` و `yalign` از `1.0` به `0.5` — آیتم‌ها چپ‌چین می‌شوند (flush-left).
  - اتصال `key-press-event` به `wrap_navigation` (نوازیابی دابل‌کامندری).
  - اعضای جدید `m_authority(false)`، `m_width_pending(false)`.
- `schedule_width_update()` (جدید): coalesce — با فلگ `m_width_pending` فقط یک idle در نوبت است؛ idle اجرا می‌شود، فلگ را برمی‌دارد و `update_item_width(m_authority)` می‌زند.
- `set_width_authority()` (جدید): `m_authority = true;` — فقط صفحه‌ای که صاحب مدل measure است آن را صدا می‌زند.
- `set_model()`: بعد از ست مدل، `update_item_width(m_authority)` اضافه شد.
- `s_item_width` — **متغیر static بین همهٔ صفحات مشترک** (`int LauncherIconView::s_item_width = 0;`): کشِ عرض آیتم.
- `update_item_width(bool measure)` (جدید): قلب اندازه‌گیری. حالت `measure=true`: با `gtk_widget_create_pango_layout` روی هر ردیف مدل، COLUMN_TEXT را با `pango_layout_set_markup` می‌گذارد و `pango_layout_get_pixel_size` می‌گیرد؛ عرض متن عریض‌ترین آیتم (`text_w`) نگه داشته می‌شود. سپس:
  ```cpp
  s_item_width = text_w + 6; // متن + گاپ (آیکون سهم رندرر است)
  ```
  کف 60px. حالت `measure=false`: فقط همان مقدار کش‌شده را اعمال می‌کند. در این نسخهٔ اولیه بعد از ست کردن item_width، یک بلوک پروب بزرگ (`view-check:` / `probe150:` در `/tmp/wm-debug.log`) وجود دارد که در کامیت‌های بعد حذف می‌شود. نکتهٔ ظریف: در این کامیت مقدار اعمالی روی ویجت در حالت measure هنوز `gtk_icon_view_set_item_width(m_view, 150)` برای پروب و بعد `set_item_width(m_view, s_item_width)` است.
- `wrap_navigation(GdkEventKey*)` (جدید): ناوبری ستونی. فرض: `cols = 2` و چیدمان «تا پر شدن ستون» (`rows_first = (n + cols - 1) / cols`): Down در ته ستون → سر ستون بعدی، Up در سر ستون → ته ستون قبلی؛ سپس `unselect_all` + `select_path` + `set_cursor` + `scroll_to_path` (با align `down?1.0:0.0`) — یعنی viewport دنبال cursor می‌رود. بدون cursor یا بدون Up/Down، `FALSE` برمی‌گرداند (به GTK واگذار).
- `reload_icon_size()`: کل switch تخصیص `item-padding` بر اساس `launcher_icon_size` (2/4/6) حذف شد و جایگزین شد با:
  ```cpp
  gtk_icon_view_set_item_padding(m_view, 0);
  update_item_width(false);
  ```
  یعنی padding صفر همیشه + اعمال کش عرض (بدون measure دوباره).

### launcher-icon-view.h

پروتوتایپ‌های جدید: `update_item_width(bool) override`، `wrap_navigation`، `set_width_authority() override`، `schedule_width_update()`، عضو `static int s_item_width` (public)، اعضای `m_authority`، `m_width_pending`.

### launcher-view.h

دو متد virtual با پیاده‌سازی خالی به interface اضافه شد — تا `LauncherTreeView` (نمای درختی) هم بدون تغییر کامپایل شود:
```cpp
virtual void update_item_width(bool measure) { (void)measure; }
virtual void set_width_authority() {}
```

### page.cpp

فقط کامنت فارسی اضافه شد (توضیح خط موجود):
```cpp
// اسکرول‌بار عمودی EXTERNAL تا فضای ستون‌ها را نگیرد (عرض = دقیقاً محتوا)
gtk_scrolled_window_set_policy(..., GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
```

### window.cpp

- `Window::show()`: دو ابزار دیباگ موقت اضافه شد:
  1. لاگ `show() called pos=%d` به `/tmp/wm-debug.log`.
  2. بعد از `gtk_window_present`، یک idle که کل درخت ویجت را با `wm_dump_walk` (تابع static جدید: بازگشتی، برای هر ویجت `gtk_widget_get_preferred_width` می‌گیرد و type/name/alloc/min/nat را می‌نویسد) در `/tmp/wm-debug.log` می‌ریزد (`=== show dump ===`).
- `Window::set_size()`: اولین نسخهٔ «عرض از محتوا». داخل بلوک `if ((m_geometry.width != width) || ...)`: بعد از ست h/w جدید، natural عرض ویجت favorites و `m_category_buttons` گرفته می‌شود و:
  ```cpp
  // عرض از محتوا: nat ویجت لانچر (2×آیتم شامل رندررها) + nat دسته‌ها + قاب (20)
  int content_w = (int)lav_nt + (int)cat_nt + 20;
  if (content_w > 300) width = content_w;
  ```
  سپس توالی قدیمی `set_size_request → gtk_window_resize(1,1) → set_size_request → gtk_window_resize(1,1)` (ترفند vanilla برای کوچک‌کردن پنجره زیر حداقل) به این تبدیل شد:
  ```cpp
  gtk_widget_set_size_request(GTK_WIDGET(m_window), m_geometry.width, m_geometry.height);
  gtk_window_resize(m_window, m_geometry.width, m_geometry.height);
  gtk_widget_set_size_request(GTK_WIDGET(m_window), m_geometry.width, m_geometry.height);
  gtk_window_resize(m_window, m_geometry.width, m_geometry.height);
  ```
  یعنی resize با اندازهٔ واقعی (نه `1,1`).

---

## 3. `36deea5` — vibris: live relayout during drag, capacity-fill columns, width from GTK natural, buffered first frame, header symmetry, theme-font item measure, scrollbar arithmetic per-frame

بزرگ‌ترین کامیت فورک (978+/203-، 10 فایل). **نیت کلی**: بازچینش زندهٔ favorites حین درگِ resize، چیدمان «پُرکردن تا ظرفیت ستون» به‌جای سطرِ ثابت GTK، عرض منو از natural واقعی GTK، حذف فلش اولین فریم (realize+pump+رندر در بافر قبل از present)، تقارن هدر، اندازه‌گیری متن با فونت تم، و arithmetic اسکرول‌بار در هر فریم. پیام این کامیت فقط تیتر است؛ رationale در کامنت‌های کد است.

### favorites-page.cpp / favorites-page.h — بازنویسی کامل چیدمان

- static جدید: `int FavoritesPage::s_measured_pitch = 0;` — pitch واقعی ردیف که بعد از اولین رندر اندازه‌گیری می‌شود.
- دیتا ممبرهای جدید (h): `m_resync_source`، `m_pending_add`، `m_pending_remove`، `m_columns`، `m_used_cols`، `m_built_capacity`، `m_overflow`، `m_model_slots` (نگاشت سلول مدل → ایندکس منطقی favorites؛ −1 = خانهٔ خالی). متدهای جدید: `on_height_changed()`، `sync_capacity()`، `sort_favorites()`، `rebuild()`، `schedule_resync()`، `resync_now()`، `get_used_columns()`.
- `FavoritesPage::~FavoritesPage`: حذف idle/timeout معلق (`m_resync_source`) قبل از unset.
- `FavoritesPage::add()`: به‌جای insert مستقیم در GtkListStore، حالا `wm_settings->favorites.push_back(...)` + `rebuild()`. کامنت: «ترتیب نهایی را sort_favorites() در بازچینش تعیین می‌کند» — مدل از حالت دست‌خوردهٔ تدریجی به مدلِ بازسازی‌شدهٔ کامل تغییر پارادیم داد.
- `FavoritesPage::remove()`: مشابه — حذف desktop_id از favorites + `rebuild()`؛ جای پیمایش مدل برای پیدا کردن ردیف.
- `FavoritesPage::set_menu_items()`: بازنویسی اساسی:
  - اول `sort_favorites()` (sort خودکار صعودی).
  - `m_columns` از تنظیم جدید `wm_settings->favorites_columns` (پیش‌فرض 2، clamp به تعداد آیتم).
  - `capacity = get_window()->favorites_capacity_rows()` (فقط حالت ستونی؛ حالت تک‌ستونه capacity = n).
  - سه شاخهٔ چیدمان و پر کردن `m_model_slots`:
    - تک‌ستونه: همهٔ آیتم‌ها پشت سر هم؛ `m_overflow = (n > capacity)`.
    - حالت افقی (`favorites_horizontal`): پر کردن سطری — سلول `(r,c) ← r*cols + c`، خانه‌های خالی = −1.
    - حالت ستونی: «پر کردن تا ظرفیت» — آیتم به ستون بعد نمی‌رود مگر ستون جاری پر شود؛ `counts[c]` برای هر ستون؛ ستون آخر اگر سرریز شد آیتم‌های اضافه زیرش ادامه می‌یابند (scrollbar عمودی، نه ستون تازه).
  - پروب موقت در `/tmp/wm-geo.log` (`layout: n=... capacity=... used=... iw=... alloc_h=... resizing=...`).
  - ساخت GtkListStore جدید: هر سلول دقیقاً یک ردیف؛ سلول −1 → ردیف با `COLUMN_TEXT == ""` (placeholder). کامنت: «هر سلول دقیقاً یک ردیف تا m_model_slots با ردیف‌ها هم‌اندیس بماند».
  - `gtk_icon_view_set_columns(..., m_used_cols)` — فقط ستون‌های دارای آیتم. کامنت: «ستون خالی وجود ندارد که نه دیده شود و نه با arrow-right بشود داخلش رفت (خواستهٔ کاربر)».
  - `overlay-scrolling` از `wm_settings->favorites_scrollbar_overlay`.
- `extend_context_menu()`: آیتم‌های Sort A-Z / Z-A حذف شدند (`(void)menu`) — «sort خودکار صعودی جایگزین sort دستی شده».
- `on_row_changed` / `on_row_inserted`: منطق `favorites.set(pos,...)`/`insert(pos,...)` وابسته به جایگاه حذف شد؛ جایگزین: اگر launcher جدید و تکراری نبود → `m_pending_add = launcher; schedule_resync();`. کامنت: «ردیفی که بعداً پر شده (مثل درگ از صفحهٔ اپ‌ها) به فهرست اضافه شود».
- `on_row_deleted`: جای مدل با نگاشت `m_model_slots[pos]` به ایندکس منطقی favorites برگردانده می‌شود؛ `m_pending_remove = logical; schedule_resync();`.
- `sort_favorites()` (جایگزین `sort_ascending`/`sort_descending`): sort صعودی بر اساس نام با `Element::less_than`؛ شناسه‌های بدون لانچر حذف نمی‌شوند و ته لیست می‌مانند.
- `rebuild()`: `set_menu_items()` + `get_window()->sync_width()`. کامنت: «عرض منو فقط به دنبال تغییر واقعی محتوا/چیدمان sync می‌شود — rebuild از مسیر show صدا زده نمی‌شود».
- `on_height_changed()`: تغییر ارتفاع منو = تغییر ظرفیت → `rebuild()`؛ «تنها جای مجاز برای تغییر توزیع پس از resize».
- `schedule_resync()`: coalesce — حین درگ با `g_idle_add_full(G_PRIORITY_HIGH_IDLE)` (قبل از paint همان فریم)؛ خارج از درگ با `g_timeout_add(400)` (ضد چشمک در تغییرات پیاپی).
- `sync_capacity()`: بعد از pump در show (تخصیص نهایی، هنوز مخفی): pitch واقعی را از `gtk_icon_view_get_cell_rect` ردیف 0 می‌خواند (`rect.height + row-spacing`) و در `s_measured_pitch` می‌گذارد؛ اگر `favorites_capacity_rows()` با `m_built_capacity` فرق دارد → `resync_now()` — «همین‌جا (نامرئی) بازچینش می‌شود».
- `resync_now()`: اعمال `m_pending_add`/`m_pending_remove`، در صورت درگ اول `update_min_h()`، بعد `rebuild()`.
- `view_created()`:
  - `min_content_height = 1` روی ScrolledWindow — کامنت طولانی: min پیش‌فرض GTK3 = min فرزند (کل محتوای icon view) است؛ نتیجه‌اش این بود که با کوچک کردن ارتفاع، تا وقتی بازچینش دوسطونه می‌نشیند پنجره به پایین رشد می‌کرد (=پیرپیر پایین).
  - هوک `size-allocate` روی icon view: حین درگ، تصمیم scrollbar «حسابی و همان‌فریم» (overflow = `n > cols * cap` از `favorites_capacity_rows()`؛ `gtk_widget_show/hide(vsb)`) و `schedule_resync` فقط با تغییر واقعی ظرفیت («rebuild بی‌دلیل مدل را خالی-پر می‌کرد = چشمک آیتم‌ها (پیرپیر آیتم‌ها)»). کامنت مهم: «sync مستقیم داخل size-allocate تجربه شد و مدل را وسط پاس تخصیص خراب می‌کند (لیست خالی شد) — idle بین allocate و paint می‌نشیند».
  - policy اسکرول `NEVER, AUTOMATIC` + `overlay-scrolling` از تنظیمات.

### launcher-icon-view.cpp

- `update_item_width`: حذف لاگ‌ها؛ اندازه‌گیری متن حالا با **فونت تم**:
  ```cpp
  GtkStyleContext* style = gtk_widget_get_style_context(GTK_WIDGET(m_view));
  const PangoFontDescription* theme_font = gtk_style_context_get_font(style, GTK_STATE_FLAG_NORMAL);
  pango_layout_set_font_description(layout, theme_font);
  gtk_style_context_add_class(style, "launchers");
  ```
  کامنت: «measure با فونت واقعی تم (CSS)، نه فونت پیش‌فرض context — اختلاف این دو، item_width را بزرگ‌تر از رندر واقعی می‌کرد و منجر به فضای مردهٔ سمت راست پنجره می‌شد».
  فرمول عرض اصلاح شد به:
  ```cpp
  s_item_width = 2 * item_pad + icon + 6 + text_w;
  ```
  با کامنت ساختار سلول GtkIconView: «cell_area عرضش = item_width؛ داخل آن icon cell چسبیده به چپ، text cell چسبیده به راست (pack_end) — فاصلهٔ آزاد بین‌شان = item_width − (icon+text)». بلوک پروب بزرگ حذف شد.
- `wrap_navigation`: بازنویسی برای چیدمان سطری GTK (آیتم‌های زوج ستون چپ، فرد ستون راست): حرکت عمودی = `idx ± cols`؛ `is_placeholder` (ردیف با COLUMN_LAUNCHER==null) مقصد نمی‌شود؛ محدود شدن به `cols = gtk_icon_view_get_columns(m_view)` واقعی (با guard `cols < 2 → FALSE` چون «با یک ستون، ناوبری طبیعی GTK همان درست است»). اسکرول به‌جای انیمیشن `scroll_to_path`، «فوری و حداقلی»: با `gtk_icon_view_get_cell_rect` + `GtkAdjustment`، فقط اگر cell بیرون viewport است `gtk_adjustment_set_value` حداقلی.
- `reload_icon_size`: دوبار پشت‌سرهم `gtk_icon_view_set_item_padding(m_view, 0)` به یک بار کاهش یافت و `update_item_width(false)` به `update_item_width(true)` تغییر کرد — کامنت: «اندازهٔ آیکون عوض شد → عرض سلول باید دوباره measure شود (نه کش کهنه؛ s_item_width استاتیک است)».

### plugin.cpp

- `Plugin::Plugin`: `gtk_widget_set_valign(GTK_WIDGET(m_button_icon), GTK_ALIGN_CENTER)`.
- `Plugin::size_changed`: در پنل چندردیفه، آیکون دیگر تمام span نمی‌شود:
  ```cpp
  const gint span_rows = std::min<gint>(xfce_panel_plugin_get_nrows(m_plugin), 2);
  icon_size = (icon_size * span_rows * 3) / 4;
  ```
  کامنت: «سقف = حداکثر دو ردیف، سه‌چهارم فضا».

### profile.cpp

- `Profile::update_picture`: آواتار از 32px ثابت به `wm_settings->launcher_icon_size.get_size()`.
- crop مربع مرکزی برای عکس‌های غیرمربعی (کامنت: «عکس‌های غیرمربعی بدون این، بیضی می‌شوند»): `gdk_pixbuf_new_subpixbuf` مرکزی + `gdk_pixbuf_scale_simple` به اندازهٔ هدف.
- inset اپتیکال 2px: در حالت round شعاع clip از `half_size` به `half_size - inset`؛ در حالت مربع، `cairo_rectangle(inset, inset, size-2*inset, ...)` — کامنت: «glyph آیکون‌های تم داخل جعبه‌شان حاشیهٔ شفاف دارند ولی آواتار تا لبه پر می‌شد».

### settings-dialog.cpp

- تیتر دیالوگ تنظیمات: `_("Whisker Menu")` → `_("Vibris Menu")`.
- بخش جدید «Favorites Layout» در تب Behavior: کمبو Number of columns (1..4، تنظیم `favorites_columns` + `reload_menu`)، چک‌باکس Fill across rows first (`favorites_horizontal`)، چک‌باکس Overlay scrollbar (`favorites_scrollbar_overlay`).

### settings.cpp / settings.h

سه تنظیم جدید: `favorites_columns("/favorites-columns", 0, 0, 4)`، `favorites_horizontal("/favorites-horizontal", false)`، `favorites_scrollbar_overlay("/favorites-scrollbar-overlay", true)`؛ با load در `Settings::load` و `property_changed`.
- در `Settings::property_changed`: تغییر هرکدام از `/favorites-columns`، `/favorites-horizontal`، `/favorites-scrollbar-overlay` یا `/menu-height` → `m_plugin->reload_menu()` (بازچینش زنده).
- عنوان پیش‌فرض دکمه: `_("Applications")` → `_("Vibris Menu")`.

### window.cpp

- عنوان پنجره: `"Whisker Menu"` → `"Vibris Menu"` (title انگلیسی نیمه‌مخفی برای شناسایی توسط WM).
- **Resizerها**: فقط `Resizer::Top` باقی ماند؛ بقیهٔ هفت لبه `nullptr`. کامنت: «منو به پنل پایین چسبیده است؛ لبه‌های دیگر عملکرد درستی نداشتند». گرید resizer به یک سلول (`gtk_grid_attach(grid, m_resize[Resizer::Top]->get_widget(), 0, 0, 3, 1)`) تغییر کرد.
- هدر: CSS provider سراسری `.command-button { padding: 0px; min-width: 0px; min-height: 0px; }` + `gtk_box_set_spacing(m_commands_box, 6)` + marginهای 6px برای `m_profile->get_picture()` و `m_commands_box` (کامنت RTL: «margin_start در RTL سمت معکوس می‌شود؛ با هر دو، گپ هر دو لبه 6px ثابت»). pixel-size آیکون دکمه‌های هدر از تنظیم `launcher_icon_size` (با `gtk_container_foreach`).
- `m_panels_stack`: `gtk_stack_set_hhomogeneous(m_panels_stack, FALSE)` — «عرض پنجره از صفحهٔ فعال می‌آید نه ماکزیمم همهٔ صفحات».
- هندلر `notify::visible-child` روی `m_panels_stack` و `m_contents_stack` → idle → `set_size(wm_settings->menu_width, wm_settings->menu_height)` — عرض با تعویض صفحه به‌روز می‌شود.
- `show()`: 
  - `gdk_monitor_get_workarea` → `m_workarea`، `m_max_bottom = m_workarea.y + m_workarea.height` («منو هرگز نباید پنل را بپوشاند»).
  - قبل از present: `gtk_widget_realize` + پمپ حداکثر 25 رویداد (`gtk_main_iteration_do(FALSE)`)؛ سپس رندر کامل در `cairo_image_surface_create(CAIRO_FORMAT_ARGB32,...)` با `gtk_widget_draw` — «معادل BitBlt»: هیچ پیکسلی روی صفحه نمی‌رود، اولین فریم یکپارچه map می‌شود. `VIBRIS_DEBUG_WHITE` فقط پس‌زمینهٔ سفید دیباگ داخل همین بافر (و بعداً `gdk_window_set_background_rgba` فقط با همان env).
  - `m_favorites->sync_capacity()` بعد از pump («ظرفیت ستون‌ها با تخصیص نهایی — هنوز مخفی؛ هر اصلاحی اینجا نامرئی است»).
  - لاگ دیباگ `show()` و dump idle حذف شدند.
- `Window::favorites_capacity_rows()` (جدید، const): ظرفیت ردیف هر ستون favorites:
  - pitch = `s_measured_pitch` اگر >0؛ وگرنه فرمولی `max(icon, text_h) + 2*item_pad + rs` (text_h = بلندترین متن با pango روی مدل).
  - `avail`: اگر تخصیص فعلی با `m_geometry.height` هم‌خوان است → `alloc_h` و ذخیرهٔ `m_last_chrome = win_alloc - alloc_h`؛ وگرنه از chrome ذخیره‌شده؛ وگرنه فرمول کالیبرهٔ اولیه (`menu_height - title_h - search_h - 12 - 10`). کامنت مهم: «اختلاف estimate/allocation ظرفیت را یکی جابه‌جا می‌کرد و ستون‌ها ۱↔۲ فلیپ می‌شدند (عرض ۵۸۴↔۵۹۶ … = پیرپیر)».
  - `avail -= 2 * iv_margin` (margin پیش‌فرض 6×2 GtkIconView؛ «نبودش یعنی ظرفیت یکی بیش‌تر حساب می‌شد و scrollbar مرزی ظاهر می‌شد»).
- `Window::resize()`: بازنویسی کامل — حل هدف در سه قید (workarea بالا، `m_min_h`، چسبیدن پایین به پنل): clamp `target_y` به `m_workarea.y`، clamp `target_h` به `m_min_h`، clamp `target_y + target_h` به `m_max_bottom`. سپس mo션‌ها فقط `m_target_*` را ست می‌کنند و یک idle (`m_size_idle_id`) در هر چرخهٔ main-loop `apply_target_size()` را اجرا می‌کند — «اعمالِ re-entrant داخل هندلرِ موشن = پیرپیر حین حرکت تند».
- `Window::sync_width()` (جدید): `set_size(10, m_geometry.height)` — عرض کهنهٔ menu_width جلوی جمع‌شدن عرض را نگیرد؛ `set_size` عرض را از natural می‌گیرد.
- `Window::update_min_h()` (جدید): `m_min_h = max(10, preferred_height(m_vbox) + 10)`.
- `Window::apply_target_size()` (جدید): اعمال `m_target_*` به `m_geometry` + `set_size` + `check_scrollbar_needed` + `move_window`.
- `resize_start()`: `m_min_h` از preferred_height `m_vbox` («خود پنجره را نگیر چون set_size_request ما minimum آن را override کرده و همیشه = ارتفاع فعلی می‌شود»).
- `resize_end()`: اگر idle معلق است → cancel + `apply_target_size()` همگام؛ ذخیرهٔ menu_width/height؛ **حذف جابه‌جایی به get_menu_position/center** — «درگِ لبه فقط همان لبه را حرکت داده… فراخوانی get_menu_position اینجا کل منو می‌پراند (منشأ پرش پس از رها کردن)»؛ پایان با `m_favorites->on_height_changed()`.
- `on_key_press_event` (Esc مسیر): cancel فقط ریزرهای non-null + حذف `m_size_idle_id`.
- `move_window()`: `gtk_window_resize(m_window, m_geometry.width, m_geometry.height)` اضافه شد قبل از `gtk_window_move` — کامنت: «gtk_window_move/resize فقط state داخلی را به‌روز می‌کنند و GTK خودش در idle بعدی «یک» ConfigureRequest واحد می‌فرستد. اگر مستقیم gdk_window_move_resize بزنیم، GTK از هندسهٔ جدید بی‌خبر می‌ماند و در idle خودش با position/size کهنه override می‌کند».
- `set_size()`: بلوک عرض‌از‌محتوا **بیرون** شرط تغییر و حالا همیشه (حتی حین درگ؛ «همهٔ کار در اثنای درگ انجام می‌شود»): برای favorites، `lav_nt` = `gtk_widget_get_preferred_width(v)` واقعی («نه فرمول با item_width کهنه (۱۲px اختلاف = لرزش عرض)، و نه تخصیص جاری که حلقهٔ بازخوردی می‌ساخت»)؛ `content_w = lav_nt + cat_nt + 26` (قاب از 20 به 26). پروب `/tmp/wm-width.log` (`set_size: ... iw= ... lav_nt= ... cat_nt= ... used=...`) داخل شرط تغییر. توالی set_size_request/resize به یک جفت ساده شد با کامنت: «set_size_request همیشه — این حداقلِ toplevel نزد GTK است؛ اگر حین درگ به‌روز نشود، GTK هر gtk_window_resize را به همان حداقلِ کهنه کلمپ می‌کند و ارتفاع تغییر نمی‌کند در حالی که y حرکت کرده (منشأ «سُرخوردن» کل منو)».

### window.h

اعضای جدید: `m_workarea`، `m_max_bottom`، `m_target_x/y/w/h`، `m_size_idle_id`، `m_min_h`، `m_last_chrome (mutable)`؛ متدهای جدید public: `apply_target_size`، `is_resizing`، `set_size` (از private به public)، `sync_width`، `update_min_h`، `favorites_capacity_rows`.

---

## 4. `b7f43d8` — vibris: no highlight on empty slots, drag-hover guard, transient-state fix (requested height not stale alloc), no scrollbar width flash, light left margin

### category-button.cpp

hover-activate دسته‌ها با فلگ `FavoritesPage::drag_in_progress` گارد می‌شود (هم در `hover_timeout` شروعکننده هم در مسیر enter مستقیم) — «hover دسته‌ها و hover-selection نباید درگیر شوند (شکایت dragging with hover)».

### favorites-page.cpp / .h

- static `bool FavoritesPage::drag_in_progress = false;` + member `static int placeholder_guard;` (اعلان؛ استفاده در کامیت‌های بعد حذف شد).
- `set_menu_items()`: حین درگ و بدون سرریز واقعی، vscrollbar بلافاصله مخفی («در گپِ بین rebuild و allocate، پهنای آن به عرض اضافه نگردد (جهش عرض)»).
- `set_menu_items()`: هندلر `selection-changed` روی icon view — اگر انتخاب فقط placeholder باشد فوراً `gtk_icon_view_unselect_all` («hover یا arrow-right نباید آبی‌شان کند (خواستهٔ کاربر)»).
- `view_created()`: اتصال `drag-begin`/`drag-end` روی view favorites برای ست/clear کردن `drag_in_progress`.

### window.cpp

- قاب پنجره: کلاس CSS `vibris-frame` و قانون `frame.vibris-frame { border-left: 1px solid alpha(white, 0.10); }` — «سایهٔ OUT فقط پایین/راست را روشن می‌کند؛ خط روشن چپ هم برای تقارن (خواستهٔ کاربر) با همان آلفای ظریف تم تیره».
- `favorites_capacity_rows()`: اصلاح حالت گذرا — «حین درگ از هندسهٔ خواسته − chrome ذخیره‌شده بگیر نه تخصیص: تخصیص یک فریم عقب می‌ماند و همان حالت گذرای «آخرین آیتم رفته ستون اول ولی هنوز دوسطونه‌ایم» می‌شود (خواستهٔ کاربر: یک تکون، نه دو تکون)». یعنی `avail` اول از `m_last_chrome` و فقط fallback به `alloc_h`.

---

## 5. `2df8740` — vibris: stepped drag (height quantized to row pitch), arrow-left/right skip empty slots, item width measured after realize (fixes overestimate), capacity from requested height (no transient two-column)

### favorites-page.cpp — `sync_capacity()`

اول تابع، `get_view()->update_item_width(true)` اضافه شد: «ویجت realize شده و فونت/CSS تم اعمال شده — حالا عرض آیتم را با حقیقت رندر measure می‌کنیم (measure زودهنگامِ قبل از realize فونت درست نداشت و عرض هر آیتم چند ده پیکسل بیش از واقع می‌شد؛ مثلاً «Double Commander»)».

### launcher-icon-view.cpp — `wrap_navigation`

- `is_placeholder` به ابتدای تابع منتقل/عمومی شد و کلیدهای Left/Right هم هندل شدند: «سلول بعدی/قبلیِ هم‌ردیف؛ اگر خالی (placeholder) یا بیرون ستون‌هاست، اصلاً حرکت نکن — caret روی هیچ خانهٔ خالی نمی‌ماند». یعنی Right/Left فقط اگر `t/cols == idx/cols` و `!is_placeholder(t)` جابه‌جا می‌شوند و در غیر این صورت `TRUE` (بلعیده می‌شود).
- (up/down همان منطق کامیت قبل + استفاده از `is_placeholder` منتقل‌شده.)

### window.cpp — `resize()`

درگ پله‌ای: بعد از clampها، ارتفاع به مضارب pitch ردیف گرد می‌شود:
```cpp
int pitch = FavoritesPage::s_measured_pitch;  // fallback: launcher_icon_size + 6
const int chrome = (m_last_chrome > 0) ? m_last_chrome : (m_min_h - 10);
const int avail = target_h - chrome;
if (avail > pitch) {
    const int rows = std::max(1, avail / pitch);
    const int new_h = rows * pitch + chrome;
    if (new_h != target_h && new_h >= m_min_h) { target_y += target_h - new_h; target_h = new_h; }
}
```
«پایین ثابت می‌ماند: بعد از گرد کردن، y جبران می‌شود».

---

## 6. `18805ca` — vibris: revert stepped drag; constant scrollbar-width reservation when overlay off (no width flash during drag-down)

### window.cpp

- بلوک درگ پله‌ای کامیت `2df8740` کاملاً حذف (revert) — تجربه شد و کار نکرد.
- `set_size()`: وقتی `favorites_scrollbar_overlay` خاموش است، عرض scrollbar همیشه به `lav_nt` اضافه می‌شود:
  ```cpp
  GtkWidget* sb = gtk_scrolled_window_get_vscrollbar(...);
  gtk_widget_get_preferred_width(sb, &sb_mn, &sb_nt);
  lav_nt += std::max(sb_mn, sb_nt);
  ```
  «overlay خاموش: عرض scrollbar همیشه رزرو — ثابت و بدون جهش؛ overlay روشن: شناور روی محتوا، عرضی نمی‌گیرد».

---

## 7. `17a7ed1` — vibris: window-level drag guard (all pages), vscrollbar ALWAYS during resize (constant viewport, no fat flash), capacity probe on real display

### favorites-page.cpp — `sync_capacity()`

پروب `/tmp/wm-geo.log` (`sync_cap: menu_h(set)=... win=... view=... chrome_win-view=... pitch_meas=... cap=... built=... scale=...`).

### window.cpp

- سازندهٔ Window: گارد درگ در سطح پنجره برای **همهٔ صفحات** (search/recent/applications/favorites) — «گاردِ قبلی فقط روی favorites بود و درگِ شروع‌شده از All Applications را پوشش نمی‌داد». همان drag-begin/drag-end → `FavoritesPage::drag_in_progress`.
- `resize_start()`: حین درگ policy عمودی favorites = `GTK_POLICY_ALWAYS` — «viewport favorites پهنایش ثابت است و در گپِ بین resize و بازچینش، ورود/خروجِ sb عرض را نمی‌لرزاند (جهش «چاق/لاغر» هنگام درگ به پایین)».
- `resize_end()`: بازگشت به `GTK_POLICY_AUTOMATIC` («رفتار نهایی تأییدشده»).

---

## 8. `32ee001` — vibris: scrollbar pinned during resize (policy ALWAYS, no per-frame show/hide) — kills width fat-flash on drag-down

### favorites-page.cpp

- `set_menu_items()`: بلوک مخفی‌سازی شرطی vscrollbar (کامیت b7f43d8) حذف — «per-frame show/hide» منسوخ.
- هوک size-allocate در `view_created()`: محاسبهٔ overflow و `gtk_widget_show/hide(vsb)` حذف شد؛ فقط `cap != m_built_capacity → schedule_resync` باقی ماند. کامنت: «اسکرول‌بار در طول درگ دست‌نخورده: policy ALWAYS (resize_start) آن را همیشه مپ نگه می‌دارد؛ show/hideِ همین‌جا هر فریم viewport را ۱۲px جابه‌جا می‌کرد (فلش «چاق/لاغر»)».

---

## 9. `ab10d87` — vibris: REAL fix for width flash — favorites scrollbar is overlay (never takes layout space); remove gutter reservation and opacity hacks

**تصمیم نهایی دربارهٔ اسکرول‌بار favorites: overlay همیشگی.**

### favorites-page.cpp — `view_created()`

```cpp
// قبل:
gtk_scrolled_window_set_policy(sw, GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
g_object_set(sw, "overlay-scrolling", wm_settings->favorites_scrollbar_overlay ? TRUE : FALSE, nullptr);
// بعد:
g_object_set(sw, "overlay-scrolling", TRUE, nullptr);
gtk_scrolled_window_set_policy(sw, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
```
`min_content_height = 1` ماند (با کامنت طولانی پیرپیر پایین از کامیت 36deea5). کامنت جدید: «اسکرول‌بار شناور (overlay) — هیچ‌وقت از چیدمان جا نمی‌گیرد، پس نه فلش ۱۲px در درگ هست و نه گودال زائد».
- هوک size-allocate: `gtk_widget_set_opacity(vsb, overflow ? 1.0 : 0.0)` برگشت (visibility از opacity؛ «opacity چیدمان را به‌هم نمی‌ریزد پس هیچ جهش عرضی وجود ندارد»).

### window.cpp

- `resize_start()`: ست کردن policy ALWAYS حذف؛ کامنت: «policy favorites در view_created ست شده و دیگر دست نمی‌خورد».
- `resize_end()`: بازگشت policy حذف.
- `set_size()`: افزودن شرطی عرض scrollbar (کامیت 18805ca) حذف — دیگر نیازی نیست چون sb هیچ‌وقت جا نمی‌گیرد.

---

## 10. `746f3a6` — vibris: merged move+resize (single ConfigureWindow — xtrace-proven root cause of bottom jitter), floor y-lock, overlay-sb visibility from requested geometry

### favorites-page.cpp — هوک size-allocate در `view_created()`

visibility اسکرول‌بار overlay «از هندسهٔ خواسته» محاسبه می‌شود و بیرون گارد درگ: opacity بر اساس `n > cols * cap` از `favorites_capacity_rows()` — «تا بازچینشِ ظرفیت مخفی می‌ماند و با سرریز واقعی ظاهر می‌شود — حتی یک فریم فلش ندارد». resync فقط `is_resizing() && cap != m_built_capacity`.

### window.cpp

- `resize()`: کف ارتفاع حالا y را هم قفل می‌کند:
  ```cpp
  // قبل: target_h = m_min_h; // GTK کوچک‌تر را نمی‌پذیرد — y باید خودش تنظیم شود
  target_h = m_min_h;
  target_y = m_geometry.y; // وگرنه کل پنجره سر می‌خورد و لبهٔ پایین جابه‌جا می‌شود = پیرپیر
  ```
- `move_window()`: بازگشت به **gdk_window_move_resize مستقیم** — معکوس تصمیم کامیت 36deea5! کامنت (اثبات xtrace): «gtk_window_move و gtk_window_resize هر کدام به‌محض mapped بودن، XMoveWindow و XResizeWindow «جداگانه» می‌فرستند — نتیجه: اول کل پنجره جابه‌جا دیده می‌شود، بعد resize می‌رسد و لبهٔ پایین می‌پرد و برمی‌گردد (همان پیرپیر پایین). راه‌حل: یک درخواست merged با هر چهار مقدار». فقط اگر GdkWindow هنوز ساخته نشده، fallback به `gtk_window_resize + gtk_window_move`.

---

## 11. `b4de5a0` — vibris: gravity SOUTH_WEST — X pins bottom edge atomically during height resizes; resize-only during drag (no move request); y-lock at min-height floor

### window.cpp

- سازندهٔ Window: `gtk_window_set_gravity(m_window, GDK_GRAVITY_SOUTH_WEST);` — «X خودش هنگام تغییر ارتفاع، y را طوری تنظیم می‌کند که لبهٔ پایین ثابت بماند — اتمیک و در همان درخواست resize. پیرپیر پایین از بین می‌رود چون y اصلاً از ما نمی‌آید».
- `move_window()`: حین درگ (`m_resizing && win`) فقط resize بدون move:
  ```cpp
  if (m_resizing && win)
      gdk_window_resize(win, m_geometry.width, m_geometry.height);
  else if (win)
      gdk_window_move_resize(win, m_geometry.x, m_geometry.y, ...);
  ```
  «حین درگ: فقط resize — gravity جنوب‌غرب y را در همان درخواست تنظیم می‌کند و لبهٔ پایین پیکسل‌به‌پیکسل ثابت می‌ماند».

---

## 12. `f456d07` (HEAD) — vibris: policy NEVER always (width war dead), position sync after apply, cell-renderer item width measure

### favorites-page.cpp — `view_created()`

```cpp
// قبل:
g_object_set(sw, "overlay-scrolling", TRUE, nullptr);
gtk_scrolled_window_set_policy(sw, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
// بعد:
gtk_scrolled_window_set_policy(sw, GTK_POLICY_NEVER, GTK_POLICY_NEVER);
```
کامنت (اثبات xtrace روی :0): «بدون compositor، GTK اسکرول‌بار را سنتی مپ می‌کند (حتی با overlay=TRUE) و مپ/دمپِ آن در گپِ درگ = جنگ عرض ۹۶۱/۹۷۸. پس policy NEVER همیشه: sb هرگز مپ نمی‌شود؛ عرض ۱۷px فقط وقتی سرریز واقعی است در set_size اضافه می‌شود (تصمیم از هندسهٔ خواسته — بدون تأخیر)». یعنی مکانیزم overlay-opacity حذف و رزرو صریح 17px در `set_size` جایگزین شد.
- هوک size-allocate: بلوک opacity حذف؛ فقط گارد درگ + resync باقی ماند.

### launcher-icon-view.cpp — `update_item_width`

فرمول عرض از «icon حجم فرمولی» به «مجموع natural واقعی رندررها»:
```cpp
// قبل: s_item_width = 2 * item_pad + icon + 6 + text_w;
int cells_w = 0;
GtkCellArea* area = gtk_cell_layout_get_area(GTK_CELL_LAYOUT(m_view));
// برای هر cell: gtk_cell_renderer_get_preferred_width(cell, m_view, &mn, &nt); cells_w += nt;
s_item_width = 2 * item_pad + cells_w + 6;
```
«measure دقیق: عرض واقعی رندررهای درون icon view (همان که GTK رسم می‌کند)».

### window.cpp

- `apply_target_size()`: بعد از `move_window()`، همگام‌سازی با واقعیت:
  ```cpp
  gdk_window_get_position(win, &ax, &ay);
  if (ax > -9000 && ay > -9000) { m_geometry.x = ax; m_geometry.y = ay; }
  ```
  کامنت: «WM ممکن است درخواست را کلمپ کند (مثلاً منو زیر پنل نرود) — بدون این، m_geometry از واقعیت عقب می‌ماند و هر فریم بعدی از مقدار کهنه حساب می‌شود = پرش ۹۷px (اثبات: xtrace روی :0، درخواست bottom=2160 که WM به ۲۰۶۳ اصلاح کرد)».
- `set_size()`: افزودن رزرو 17px اسکرول‌بار با سرریز واقعی:
  ```cpp
  const int n = model ? gtk_tree_model_iter_n_children(model, nullptr) : 0;
  const int cols = (wm_settings->favorites_columns > 0) ? wm_settings->favorites_columns : 2;
  if (n > cols * favorites_capacity_rows()) {
      gtk_widget_get_preferred_width(sb, &sb_mn, &sb_nt);
      lav_nt += std::max(sb_mn, sb_nt);
  }
  ```
  با کامنت «sb با policy NEVER هرگز در requisition نیست — جنگ ۹۶۱/۹۷۸ تمام».

---

## 13. تغییرات commit‌نشدهٔ درخت کاری (`src-patched` نسبت به HEAD `f456d07`)

اینها آخرین و تازه‌ترین فیکس‌ها هستند و هنوز کامیت نشده‌اند. فایل‌های درگیر: `launcher-icon-view.cpp`، `window.cpp`، `favorites-page.cpp`، `page.cpp`، `command.cpp`، `whiskermenu.desktop.in`.

### 13.1 page.cpp — سازندهٔ `Page` (رفع min-width غلط GTK / عرض ۶۲۲)

دو تغییر:
1. policy اسکرول از `NEVER, AUTOMATIC` به `AUTOMATIC, AUTOMATIC`:
   ```cpp
   // AUTOMATIC هر دو جهت: با NEVER، حداقلِ ScrolledWindow = عرض کامل IconView می‌شد و
   // پنجره اجازهٔ کوچک‌شدن به عرض محتوا را پیدا نمی‌کرد (منشأ گپ راست بزرگ).
   ```
2. override اندازهٔ view:
   ```cpp
   gtk_widget_set_size_request(m_view->get_widget(), 1, 1);
   ```
   کامنت: min پیش‌فرض GtkIconView «آیکون + گپ + wrap-width(=item_width)» است — GTK درخواست ۵۶۱ را به ۶۲۲ کلمپ می‌کرد (اثبات: wm-dump.log، IconView min=354). عرض/ارتفاع واقعی از فرمول‌های قطعی خودمان می‌آید؛ اینجا فقط کفِ غلط GTK حذف می‌شود (ضمناً درگ ارتفاع هم از زیر محتوا رد شود).

### 13.2 launcher-icon-view.cpp — `update_item_width` (رفع حلقهٔ خودارجاعی رندرر متن)

کامیت f456d07 عرض رندررها را جمع می‌زد؛ مشکل: GtkIconView روی text renderer مقدار `wrap-width` (= item_width فعلی) ست می‌کند، پس natural گزارش‌شدهٔ text cell به item_width قبلی کلمپ است → «حلقهٔ خودارجاعی: متن بلند هرگز جا نمی‌شود و می‌شکند». نسخهٔ جدید:
```cpp
int icon_w = 0, text_xpad = 0;
// آیکون: natural واقعی از رندرر؛ متن: فقط xpad از رندرر
// عرض متن = text_w (یک‌سطری واقعی با فونت تم، بالای تابع)
s_item_width = 2 * item_pad + icon_w + 6 + 2 * text_xpad + text_w;
```
یعنی عرض رندرر متن دیگر استفاده نمی‌شود؛ فقط `xpad` آن. + پروب اعتبارسنجی `/tmp/wm-width.log` (`item-width: text=... icon=... xpad=... -> iw=... | cell min=... nat=... wrap=... scale=...`) — برچسب‌گذاری‌شده «تا تأیید نهایی؛ بعداً حذف می‌شود». هدر `#include <stdio.h>` هم اضافه شد.

### 13.3 window.cpp

- `show()`: پروب موقت درخت کامل → `/tmp/wm-dump.log` (با mode `"w"` = بازنویسی هر بار):
  ```cpp
  if (FILE* df = fopen("/tmp/wm-dump.log", "w"))
      { wm_dump_walk(GTK_WIDGET(m_window), 0, df); fclose(df); }
  ```
  (تابع `wm_dump_walk` از کامیت 64b4dae در کد باقی مانده است و این اولین مصرف فعالِ آن بعد از حذف لاگ‌های قدیمی است.) هدف پروب: «پیدا کردن ویجتی که min width پنجره را بالا می‌برد (۶۲۲ به‌جای ۵۶۱)».
- `update_min_h()` — بازنویسی مهم:
  ```cpp
  // قبل: m_min_h = max(10, preferred_height(m_vbox) + 10);   // کل محتوا
  const int chrome = std::max(0, vbox_h - page_h);            // vbox_h − page_h(favorites)
  const int row = std::max(FavoritesPage::s_measured_pitch, 48);
  m_min_h = std::max(10, chrome + row + 10);
  ```
  کامنت: «کف درگ = chrome + حداقل یک ردیف — نه کل محتوا. min ارتفاع vbox شامل min کل صفحات است (GtkStack) و min صفحهٔ favorites با policy NEVER همان محتوای کامل است → درگ رو به پایین سرِ «جا شدن محتوا» سفت می‌شد».
- `resize_start()`: بدنهٔ حساب min_h حذف و به `update_min_h()` واگذار شد.
- `set_size()`: عرض view favorites دیگر از natural ویجت گرفته نمی‌شود؛ فرمول قطعی از ستون‌های استفاده‌شده:
  ```cpp
  const int used = m_favorites->get_used_columns();
  if (used > 0 && LauncherIconView::s_item_width > 0)
      lav_nt = used * LauncherIconView::s_item_width + (used - 1) * 6;
  else
      gtk_widget_get_preferred_width(v, &lav_mn, &lav_nt);  // fallback
  ```
  کامنت: «gtk_widget_get_preferred_width روی icon view تا layout نشدن صفر برمی‌گرداند (اثبات: wm-width.log، lav_nt=0 همیشه) — پس natural ویجت مبنای عرض نیست».
  + پروب `/tmp/wm-width.log` حالا **بی‌قید و هر فراخوانی** ثبت می‌کند (از داخل شرط تغییر اندازه بیرون کشیده شد) و با null-guard `m_favorites ? ... : -1`. همچنین fallback `dbg_page == nullptr` هندل شد.

### 13.4 favorites-page.cpp — `set_menu_items()`

پیش از بلوک پروب، policy عمودی favorites به‌صورت دینامیک بر اساس سرریز واقعیِ همین بازچینش:
```cpp
gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(get_widget()),
        GTK_POLICY_NEVER,
        m_overflow ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER);
```
کامنت: «اسکرول‌بار عمودی فقط با سرریز واقعی ظاهر می‌شود؛ عرضش در set_size از قبل رزرو می‌شود (+17px هندسه‌محور) پس هیچ جهش عرضی ندارد» — ترکیب policy NEVER همیشگی (f456d07) با AUTOMATIC موضعی روی سرریز.

### 13.5 command.cpp — `Command::Command`

```cpp
// قبل: GtkWidget* image = gtk_image_new_from_icon_name(m_icon, GTK_ICON_SIZE_LARGE_TOOLBAR);
GtkWidget* image = gtk_image_new_from_icon_name(m_icon, GTK_ICON_SIZE_DIALOG);
```
دکمه‌های هدر (settings/logout/restart) در اندازهٔ DIALOG — ادامهٔ سنت README-HAMIDI (دکمه‌های 48px بدون تاری). توجه: خط 286 هم (`image = ... GTK_ICON_SIZE_DIALOG`) در vanilla موجود بوده و مال مسیر دیگری است.

### 13.6 whiskermenu.desktop.in

`Name=Whisker Menu` → `Name=Vibris Menu`.

### 13.7 لاگ‌های پروب فعال فعلی

| فایل لاگ | نویسنده | محتوا |
|---|---|---|
| `/tmp/wm-width.log` | `Window::set_size` (هر فراخوانی) + `LauncherIconView::update_item_width` | `set_size: old -> new (h ...) resizing=... iw=... lav_nt=... cat_nt=... used=...` و `item-width: text=... icon=... xpad=... -> iw=... | cell min/nat/wrap/scale` |
| `/tmp/wm-geo.log` | `FavoritesPage::set_menu_items` (بلوک `layout:`) و `sync_capacity` (`sync_cap:`) | چیدمان ستون‌ها، ظرفیت، pitch، chrome |
| `/tmp/wm-dump.log` | `Window::show` (بعد از sync_capacity، قبل از present، mode `"w"`) | خروجی `wm_dump_walk`: min/nat/alloc تک‌تک ویجت‌های درخت پنجره |

(لاگ قدیمی `/tmp/wm-debug.log` کامیت‌های 64b4dae/36deea5 در نسخهٔ فعلی دیگر نوشته نمی‌شود.)

---

## 14. مسیر حل مسئله در یک نگاه (خلاصهٔ علت‌شناسی از کامنت‌ها)

1. **پیرپیر پایین هنگام درگ لبهٔ بالا**: ریشه سه‌لایه بود — (a) دو درخواست X جدا برای move و resize (اثبات xtrace، فیکس: `gdk_window_move_resize` merged → سپس gravity SOUTH_WEST + resize-only حین درگ)، (b) min_content_height پیش‌فرض ScrolledWindow که پنجره را به پایین رشد می‌داد (فیکس: min_content_height=1 و بعدها کف درگ = chrome+یک ردیف)، (c) clamp به min_h با y متحرک (فیکس: y-lock در کف).
2. **فلش عرض (fat flash / جنگ ۹۶۱/۹۷۸)**: ورود و خروجِ مپ‌شدن vscrollbar در گپ بین resize و بازچینش. تجربه‌های ناموفق: policy ALWAYS در resize_start، opacity per-frame، رزرو ثابت. فیکس نهایی: policy NEVER همیشگی + رزرو 17px در set_size فقط با سرریز واقعی (از هندسهٔ خواسته).
3. **چشمک آیتم‌ها**: rebuild بی‌دلیل در هر فریم درگ → فقط با تغییر واقعی ظرفیت (`cap != m_built_capacity`)، coalesce با idle اولویت‌بالا.
4. **فلیپ ۱↔۲ ستون / عرض ۵۸۴↔۵۹۶**: اختلاف estimate/allocation در ظرفیت → m_last_chrome و pitch اندازه‌گیری‌شدهٔ واقعی.
5. **عرض ۶۲۲ به‌جای ۵۶۱ (تغییرات فعلی، باز)**: min عرض GtkIconView (icon + spacing + text-cell natural که wrap-width=294 دارد در حالی که measure فونت-تم می‌گوید 236) — فیکس‌های فعلی: `size_request(1,1)` روی view، عرض از فرمول used_columns، و تحقیق جاری روی mismatch فونت (پروب item-width).
