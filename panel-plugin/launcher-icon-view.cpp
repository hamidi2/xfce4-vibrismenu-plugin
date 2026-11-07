/*
 * Copyright (C) 2019-2025 Graeme Gott <graeme@gottcode.org>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "launcher-icon-view.h"

#include "element.h"
#include "icon-renderer.h"
#include "settings.h"
#include "slot.h"
#include <gdk/gdkkeysyms.h>

#include <algorithm>
#include <stdio.h>

using namespace WhiskerMenu;

//-----------------------------------------------------------------------------

LauncherIconView::LauncherIconView() :
	m_icon_renderer(nullptr),
	m_text_renderer(nullptr),
	m_icon_size(-1),
	m_authority(false),
	m_width_pending(false)
{
	// Create the view
	m_view = GTK_ICON_VIEW(gtk_icon_view_new());

	m_icon_renderer = whiskermenu_icon_renderer_new();
	g_object_set(m_icon_renderer,
			"stretch", true,
			"xalign", 0.0,
			"yalign", 0.5,
			nullptr);
	GtkCellLayout* cell_layout = GTK_CELL_LAYOUT(m_view);
	gtk_cell_layout_pack_start(cell_layout, m_icon_renderer, false);
	gtk_cell_layout_set_attributes(cell_layout, m_icon_renderer, "gicon", COLUMN_ICON, "launcher", COLUMN_LAUNCHER, nullptr);

	// Own text renderer instead of gtk_icon_view_set_markup_column().
	// Reason (GTK 3.24 gtkiconview.c, adjust_wrap_width): with the built-in
	// text cell and an explicit item-width, GtkIconView forces the text cell's
	// fixed width to MAX(2 * (item_width - icon_width - 4*item_padding), 50) —
	// it DOUBLES the text column. The real item width then becomes
	// icon + spacing + 2*(item_width - icon), always wider than the widest
	// label (the "extra space after Double Commander"), and any formula that
	// feeds a measured width back into item-width diverges. With our own
	// renderer GtkIconView has no text_cell, never touches wrap-width/width,
	// and the cell area reports the label's true single-line width.
	m_text_renderer = gtk_cell_renderer_text_new();
	g_object_set(m_text_renderer,
			"xalign", 0.0,
			"yalign", 0.5,
			"wrap-width", -1,
			"ellipsize", PANGO_ELLIPSIZE_NONE,
			nullptr);
	gtk_cell_layout_pack_start(cell_layout, m_text_renderer, true);
	gtk_cell_layout_set_attributes(cell_layout, m_text_renderer, "markup", COLUMN_TEXT, nullptr);
	// لیست دوستونه: آیکون کنار متن (افقی) به‌جای زیرِ آن
	gtk_icon_view_set_item_orientation(m_view, GTK_ORIENTATION_HORIZONTAL);
	gtk_icon_view_set_columns(m_view, 2);
	gtk_icon_view_set_row_spacing(m_view, 0);
	gtk_icon_view_set_column_spacing(m_view, 6);
	gtk_icon_view_set_spacing(m_view, 6);

	reload_icon_size();

	// Use single clicks to activate items
	gtk_icon_view_set_activate_on_single_click(m_view, true);

	// Only allow up to one selected item
	gtk_icon_view_set_selection_mode(m_view, GTK_SELECTION_SINGLE);

	// ناوبری دابل‌کامندری: Down در ته ستون ← سر ستون بعدی؛ Up در سر ستون ← ته ستون قبلی
	g_signal_connect(m_view, "key-press-event",
		G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer user_data) -> gboolean
		{
			return static_cast<LauncherIconView*>(user_data)->wrap_navigation(event);
		}),
	this);

	// Theme / font / CSS changed: the stored favorites width is stale, so this
	// also counts as "content changed". The next show() picks it up in set_size.
	g_signal_connect(m_view, "style-updated",
		G_CALLBACK(+[](GtkWidget*, gpointer user_data)
		{
			// style-updated also fires for focus/backdrop state changes on
			// every show/hide — re-measure only if the font really changed
			auto view = static_cast<LauncherIconView*>(user_data);
			if (!view->m_authority)
			{
				return;
			}
			PangoContext* pc = gtk_widget_get_pango_context(GTK_WIDGET(view->m_view));
			gchar* key = pango_font_description_to_string(pango_context_get_font_description(pc));
			if (view->m_font_key != key)
			{
				view->m_font_key = key;
				view->update_item_width(true);
			}
			g_free(key);
		}),
	this);

	g_object_ref_sink(m_view);

	gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(m_view)), "launchers");

	// Handle hover selection
	enable_hover_selection(GTK_WIDGET(m_view));
}

//-----------------------------------------------------------------------------

LauncherIconView::~LauncherIconView()
{
	gtk_widget_destroy(GTK_WIDGET(m_view));
	g_object_unref(m_view);
}

//-----------------------------------------------------------------------------

GtkTreePath* LauncherIconView::get_cursor() const
{
	GtkTreePath* path = nullptr;
	gtk_icon_view_get_cursor(m_view, &path, nullptr);
	return path;
}

//-----------------------------------------------------------------------------

GtkTreePath* LauncherIconView::get_path_at_pos(int x, int y) const
{
	return gtk_icon_view_get_path_at_pos(m_view, x, y);
}

//-----------------------------------------------------------------------------

GtkTreePath* LauncherIconView::get_selected_path() const
{
	GtkTreePath* path = nullptr;
	GList* selection = gtk_icon_view_get_selected_items(m_view);
	if (selection)
	{
		path = gtk_tree_path_copy(static_cast<GtkTreePath*>(selection->data));
	}
	g_list_free_full(selection, reinterpret_cast<GDestroyNotify>(&gtk_tree_path_free));
	return path;
}

//-----------------------------------------------------------------------------

bool LauncherIconView::is_path_selected(GtkTreePath* path) const
{
	return gtk_icon_view_path_is_selected(m_view, path);
}

//-----------------------------------------------------------------------------

void LauncherIconView::activate_path(GtkTreePath* path)
{
	gtk_icon_view_item_activated(m_view, path);
}

//-----------------------------------------------------------------------------

void LauncherIconView::scroll_to_path(GtkTreePath* path)
{
	gtk_icon_view_scroll_to_path(m_view, path, true, 0.5f, 0.5f);
}

//-----------------------------------------------------------------------------

void LauncherIconView::select_path(GtkTreePath* path)
{
	gtk_icon_view_select_path(m_view, path);
}

//-----------------------------------------------------------------------------

void LauncherIconView::set_cursor(GtkTreePath* path)
{
	gtk_icon_view_set_cursor(m_view,path, nullptr, false);
}

//-----------------------------------------------------------------------------

void LauncherIconView::set_fixed_height_mode(bool)
{
}

//-----------------------------------------------------------------------------

void LauncherIconView::set_selection_mode(GtkSelectionMode mode)
{
	gtk_icon_view_set_selection_mode(m_view, mode);
}

//-----------------------------------------------------------------------------

void LauncherIconView::hide_tooltips()
{
	gtk_icon_view_set_tooltip_column(m_view, -1);
}

//-----------------------------------------------------------------------------

void LauncherIconView::show_tooltips()
{
	gtk_icon_view_set_tooltip_column(m_view, COLUMN_TOOLTIP);
}

//-----------------------------------------------------------------------------

void LauncherIconView::clear_selection()
{
	gtk_icon_view_unselect_all(m_view);
}

//-----------------------------------------------------------------------------

void LauncherIconView::collapse_all()
{
}

//-----------------------------------------------------------------------------

void LauncherIconView::schedule_width_update()
{
	if (m_width_pending)
	{
		return;
	}
	m_width_pending = true;
	g_idle_add(+[](gpointer data) -> gint
	{
		auto v = static_cast<LauncherIconView*>(data);
		v->m_width_pending = false;
		v->update_item_width(v->m_authority);
		return G_SOURCE_REMOVE;
	}, this);
}

//-----------------------------------------------------------------------------

void LauncherIconView::set_width_authority()
{
	m_authority = true;
}

void LauncherIconView::set_model(GtkTreeModel* model)
{
	m_model = model;
	gtk_icon_view_set_model(m_view, model);
	// apply the cached width only; the owner decides when to re-measure
	update_item_width(false);
}

//-----------------------------------------------------------------------------

void LauncherIconView::unset_model()
{
	m_model = nullptr;
	gtk_icon_view_set_model(m_view, nullptr);
}

//-----------------------------------------------------------------------------

void LauncherIconView::set_drag_source(GdkModifierType start_button_mask, const GtkTargetEntry* targets, gint n_targets, GdkDragAction actions)
{
	gtk_icon_view_enable_model_drag_source(m_view, start_button_mask, targets, n_targets, actions);
}

//-----------------------------------------------------------------------------

void LauncherIconView::set_drag_dest(const GtkTargetEntry* targets, gint n_targets, GdkDragAction actions)
{
	gtk_icon_view_enable_model_drag_dest(m_view, targets, n_targets, actions);
}

//-----------------------------------------------------------------------------

void LauncherIconView::unset_drag_source()
{
	gtk_icon_view_unset_model_drag_source(m_view);
}

int LauncherIconView::s_item_width = 0;

//-----------------------------------------------------------------------------

void LauncherIconView::update_item_width(bool measure)
{
	// Measure only when asked (favorites content changed: add / remove /
	// rename / icon size / theme font). Capacity relayouts during a drag and
	// every show() just re-apply the cached value.
	if (measure && m_authority)
	{
		s_item_width = measure_item_width();
	}
	if (s_item_width > 0)
	{
		gtk_icon_view_set_item_width(m_view, s_item_width);
	}
}

//-----------------------------------------------------------------------------

int LauncherIconView::measure_item_width() const
{
	// Exact by construction: run every row through the very same GtkCellArea
	// that GtkIconView renders with (same renderers, same widget font/CSS,
	// same cell spacing) and take the accumulated minimum width. GtkIconView
	// sizes an item as MAX(cell-area minimum, item-width), so setting
	// item-width to this value makes the rendered item exactly as wide as the
	// widest label + icon + spacing — no guessing, no feedback loop.
	GtkTreeModel* model = gtk_icon_view_get_model(m_view);
	GtkCellArea* area = gtk_cell_layout_get_area(GTK_CELL_LAYOUT(m_view));
	if (!model || !area)
	{
		return s_item_width;
	}

	GtkCellAreaContext* context = gtk_cell_area_create_context(area);
	GtkTreeIter iter;
	bool any = false;
	if (gtk_tree_model_get_iter_first(model, &iter))
	{
		do
		{
			Element* element = nullptr;
			gtk_tree_model_get(model, &iter, COLUMN_LAUNCHER, &element, -1);
			if (!element)
			{
				continue; // empty column slot
			}
			gtk_cell_area_apply_attributes(area, model, &iter, false, false);
			gtk_cell_area_get_preferred_width(area, context, GTK_WIDGET(m_view), nullptr, nullptr);
			any = true;
		} while (gtk_tree_model_iter_next(model, &iter));
	}

	int width = s_item_width;
	if (any)
	{
		gint minimum = 0, natural = 0;
		gtk_cell_area_context_get_preferred_width(context, &minimum, &natural);
		width = std::max(1, static_cast<int>(minimum));
	}
	g_object_unref(context);
	return width;
}

//-----------------------------------------------------------------------------

void LauncherIconView::unset_drag_dest()
{
	gtk_icon_view_unset_model_drag_dest(m_view);
}

gboolean LauncherIconView::wrap_navigation(GdkEventKey* event)
{
	GtkTreeModel* model = gtk_icon_view_get_model(m_view);
	if (!model)
	{
		return FALSE;
	}
	auto is_placeholder = [this, model](int i) -> gboolean
	{
		GtkTreePath* p = gtk_tree_path_new_from_indices(i, -1);
		GtkTreeIter it;
		Element* e = nullptr;
		gboolean empty = TRUE;
		if (gtk_tree_model_get_iter(model, &it, p))
		{
			gtk_tree_model_get(model, &it, COLUMN_LAUNCHER, &e, -1);
			empty = (e == nullptr);
		}
		gtk_tree_path_free(p);
		return empty;
	};
	guint keyval = 0;
	gdk_event_get_keyval(reinterpret_cast<GdkEvent*>(event), &keyval);
	const bool down = (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down);
	const bool up = (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up);
	const bool right = (keyval == GDK_KEY_Right || keyval == GDK_KEY_KP_Right);
	const bool left = (keyval == GDK_KEY_Left || keyval == GDK_KEY_KP_Left);
	// چپ/راست: سلول بعدی/قبلیِ هم‌ردیف؛ اگر خالی (placeholder) یا بیرون
	// ستون‌هاست، اصلاً حرکت نکن — caret روی هیچ خانهٔ خالی نمی‌ماند
	if (right || left)
	{
		GtkTreePath* cursor = nullptr;
		gtk_icon_view_get_cursor(m_view, &cursor, nullptr);
		GtkTreePath* sel = get_selected_path();
		if (!cursor && sel)
		{
			cursor = sel;
		}
		if (!cursor)
		{
			return TRUE;
		}
		const int idx = gtk_tree_path_get_indices(cursor)[0];
		gtk_tree_path_free(cursor);
		if (sel)
		{
			gtk_tree_path_free(sel);
		}
		const int cols = gtk_icon_view_get_columns(m_view);
		if (cols < 2)
		{
			return TRUE;
		}
		const int t = right ? idx + 1 : idx - 1;
		if (t < 0 || (t / cols) != (idx / cols))
		{
			return TRUE;
		}
		if (!is_placeholder(t))
		{
			GtkTreePath* path = gtk_tree_path_new_from_indices(t, -1);
			gtk_icon_view_unselect_all(m_view);
			gtk_icon_view_select_path(m_view, path);
			gtk_icon_view_set_cursor(m_view, path, nullptr, FALSE);
			gtk_tree_path_free(path);
		}
		return TRUE;
	}
	if (!down && !up)
	{
		return FALSE;
	}

	// با یک ستون، ناوبری طبیعی GTK همان درست است
	const int cols = gtk_icon_view_get_columns(m_view);
	if (cols < 2)
	{
		return FALSE;
	}

	// چیدمان سطری: آیتم‌های زوج ستون چپ، فرد ستون راست؛
	// سلول‌های خالی (لانچر null) فقط ته ستون راست‌اند و نباید مقصد شوند
	const int n = gtk_tree_model_iter_n_children(model, nullptr);

	GtkTreePath* cursor = nullptr;
	gtk_icon_view_get_cursor(m_view, &cursor, nullptr);
	if (!cursor)
	{
		return FALSE;
	}
	const int idx = gtk_tree_path_get_indices(cursor)[0];
	const int col = idx % cols;

	int target = -1;
	if (down)
	{
		int t = idx + cols;
		if (t < n && !is_placeholder(t))
		{
			target = t; // پایین در همان ستون
		}
		else if (col + 1 < cols && !is_placeholder(col + 1))
		{
			target = col + 1; // ته ستون → سر ستون بعدی
		}
	}
	else
	{
		int t = idx - cols;
		if (t >= 0 && !is_placeholder(t))
		{
			target = t; // بالا در همان ستون
		}
		else if (col > 0)
		{
			for (int r = (n - 1) / cols; r >= 0; --r)
			{
				int c = r * cols + (col - 1);
				if (c < n && !is_placeholder(c))
				{
					target = c; // سر ستون → ته ستون قبلی
					break;
				}
			}
		}
	}
	gtk_tree_path_free(cursor);
	if (target < 0 || target >= n || is_placeholder(target))
	{
		return TRUE; // لبهٔ کلی — بمان
	}
	GtkTreePath* path = gtk_tree_path_new_from_indices(target, -1);
	gtk_icon_view_unselect_all(m_view);
	gtk_icon_view_select_path(m_view, path);
	gtk_icon_view_set_cursor(m_view, path, nullptr, FALSE);
	// اسکرول فوری و حداقلی (بدون انیمیشن scroll_to_path)
	GdkRectangle rect = {0, 0, 0, 0};
	if (gtk_icon_view_get_cell_rect(m_view, path, nullptr, &rect) && rect.height > 0)
	{
		GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(m_view));
		gdouble v = gtk_adjustment_get_value(vadj);
		gdouble page = gtk_adjustment_get_page_size(vadj);
		if (rect.y < 0)
		{
			gtk_adjustment_set_value(vadj, v + rect.y);
		}
		else if (rect.y + rect.height > page)
		{
			gtk_adjustment_set_value(vadj, v + (rect.y + rect.height - page));
		}
	} // فقط اگر پنهان است، حداقل اسکرول
	gtk_tree_path_free(path);
	return TRUE;
}

//-----------------------------------------------------------------------------

void LauncherIconView::reload_icon_size()
{
	// Fetch icon size
	if (m_icon_size == wm_settings->launcher_icon_size.get_size())
	{
		return;
	}
	m_icon_size = wm_settings->launcher_icon_size.get_size();

	// Configure icon renderer
	if (m_icon_size > 1)
	{
		g_object_set(m_icon_renderer, "size", m_icon_size, "visible", true, nullptr);
	}
	else
	{
		g_object_set(m_icon_renderer, "visible", false, nullptr);
	}

	// Reset padding to fix icon clipping
	gtk_icon_view_set_item_padding(m_view, 0);

	// اندازهٔ آیکون عوض شد → عرض سلول باید دوباره measure شود (نه کش کهنه؛
	// s_item_width استاتیک است و بدون measure(true) عرض منو غلط می‌ماند)
	update_item_width(true);
}

//-----------------------------------------------------------------------------
