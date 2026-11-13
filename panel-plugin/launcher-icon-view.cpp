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

using namespace VibrisMenu;

//-----------------------------------------------------------------------------

LauncherIconView::LauncherIconView() :
	m_authority(false),
	m_width_pending(false),
	m_icon_renderer(nullptr),
	m_text_renderer(nullptr),
	m_icon_size(-1)
{
	// Create the view
	m_view = GTK_ICON_VIEW(gtk_icon_view_new());

	m_icon_renderer = vibrismenu_icon_renderer_new();
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
	// xpad: a text renderer comes with 2px on each side, which made the gap
	// between an icon and its label, and between two columns, 8 instead of 6
	g_object_set(m_text_renderer,
			"xpad", 0,
			"xalign", 0.0,
			"yalign", 0.5,
			"wrap-width", -1,
			"ellipsize", PANGO_ELLIPSIZE_NONE,
			nullptr);
	gtk_cell_layout_pack_start(cell_layout, m_text_renderer, true);
	gtk_cell_layout_set_attributes(cell_layout, m_text_renderer, "markup", COLUMN_TEXT, nullptr);
	// Column list: icon beside the text (horizontal) instead of above it
	gtk_icon_view_set_item_orientation(m_view, GTK_ORIENTATION_HORIZONTAL);
	gtk_icon_view_set_columns(m_view, 2);
	// row, column and icon-to-label gaps: reload_icon_size()

	reload_icon_size();

	// Use single clicks to activate items
	gtk_icon_view_set_activate_on_single_click(m_view, true);

	// Only allow up to one selected item
	gtk_icon_view_set_selection_mode(m_view, GTK_SELECTION_SINGLE);

	// Column-wise navigation: Down at a column's end -> top of the next column; Up at the top -> end of the previous
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
	disconnect_model();
	if (m_rebuild_id)
	{
		g_source_remove(m_rebuild_id);
	}
	if (m_display)
	{
		g_object_unref(m_display);
	}
	gtk_widget_destroy(GTK_WIDGET(m_view));
	g_object_unref(m_view);
	if (m_model)
	{
		g_object_unref(m_model);
	}
}

//-----------------------------------------------------------------------------

GtkTreePath* LauncherIconView::get_cursor() const
{
	GtkTreePath* path = nullptr;
	gtk_icon_view_get_cursor(m_view, &path, nullptr);
	GtkTreePath* result = model_path(path);
	if (path)
	{
		gtk_tree_path_free(path);
	}
	return result;
}

//-----------------------------------------------------------------------------

GtkTreePath* LauncherIconView::get_path_at_pos(int x, int y) const
{
	GtkTreePath* path = gtk_icon_view_get_path_at_pos(m_view, x, y);
	GtkTreePath* result = model_path(path);
	if (path)
	{
		gtk_tree_path_free(path);
	}
	return result;
}

//-----------------------------------------------------------------------------

GtkTreePath* LauncherIconView::get_selected_path() const
{
	GtkTreePath* path = nullptr;
	GList* selection = gtk_icon_view_get_selected_items(m_view);
	if (selection)
	{
		path = model_path(static_cast<GtkTreePath*>(selection->data));
	}
	g_list_free_full(selection, reinterpret_cast<GDestroyNotify>(&gtk_tree_path_free));
	return path;
}

//-----------------------------------------------------------------------------

bool LauncherIconView::is_path_selected(GtkTreePath* path) const
{
	GtkTreePath* vp = view_path(path);
	const bool selected = vp && gtk_icon_view_path_is_selected(m_view, vp);
	if (vp)
	{
		gtk_tree_path_free(vp);
	}
	return selected;
}

//-----------------------------------------------------------------------------

void LauncherIconView::activate_path(GtkTreePath* path)
{
	GtkTreePath* vp = view_path(path);
	if (vp)
	{
		gtk_icon_view_item_activated(m_view, vp);
		gtk_tree_path_free(vp);
	}
}

//-----------------------------------------------------------------------------

void LauncherIconView::scroll_to_path(GtkTreePath* path)
{
	GtkTreePath* vp = view_path(path);
	if (vp)
	{
		gtk_icon_view_scroll_to_path(m_view, vp, true, 0.5f, 0.5f);
		gtk_tree_path_free(vp);
	}
}

//-----------------------------------------------------------------------------

void LauncherIconView::select_path(GtkTreePath* path)
{
	GtkTreePath* vp = view_path(path);
	if (vp)
	{
		gtk_icon_view_select_path(m_view, vp);
		gtk_tree_path_free(vp);
	}
}

//-----------------------------------------------------------------------------

void LauncherIconView::set_cursor(GtkTreePath* path)
{
	GtkTreePath* vp = view_path(path);
	if (vp)
	{
		gtk_icon_view_set_cursor(m_view, vp, nullptr, false);
		gtk_tree_path_free(vp);
	}
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
	// keep our own reference: in column-major mode the icon view holds only
	// the display copy, and callers drop theirs right after set_model()
	if (model)
	{
		g_object_ref(model);
	}
	disconnect_model();
	if (m_model)
	{
		g_object_unref(m_model);
	}
	m_model = model;
	if (m_cm_cols > 1 && model)
	{
		rebuild_display();
	}
	else
	{
		gtk_icon_view_set_model(m_view, model);
	}
	// apply the cached width only; the owner decides when to re-measure
	update_item_width(false);
}

//-----------------------------------------------------------------------------

void LauncherIconView::unset_model()
{
	disconnect_model();
	gtk_icon_view_set_model(m_view, nullptr);
	if (m_model)
	{
		g_object_unref(m_model);
		m_model = nullptr;
	}
	m_to_model.clear();
	m_to_display.clear();
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

	// measure the labels themselves, not the size fixed after the last measure
	gtk_cell_renderer_set_fixed_size(m_text_renderer, -1, -1);
	gint text_w = 0, text_h = 0;

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
			gint w = 0, h = 0;
			gtk_cell_renderer_get_preferred_width(m_text_renderer, GTK_WIDGET(m_view), nullptr, &w);
			gtk_cell_renderer_get_preferred_height_for_width(m_text_renderer, GTK_WIDGET(m_view), w, nullptr, &h);
			text_w = std::max(text_w, w);
			text_h = std::max(text_h, h);
			any = true;
		} while (gtk_tree_model_iter_next(model, &iter));
	}

	int width = s_item_width;
	if (any)
	{
		gint minimum = 0, natural = 0;
		gtk_cell_area_context_get_preferred_width(context, &minimum, &natural);
		width = std::max(1, static_cast<int>(minimum));
		// Every label is one line and no item is narrower than the widest, so
		// the label cell gets the widest label's size. GtkIconView measures
		// every item's text on each relayout; with a fixed size that is free
		// — it was most of the cost of a drag step with many favorites.
		gtk_cell_renderer_set_fixed_size(m_text_renderer, text_w, text_h);
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
	// Left/Right: next/previous cell in the same row; if it is empty (placeholder) or
	// outside the columns, do not move — the caret never rests on an empty cell
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

	// With a single column GTK's own navigation is already right
	const int cols = gtk_icon_view_get_columns(m_view);
	if (cols < 2)
	{
		return FALSE;
	}

	// Row-major layout: even items in the left column, odd in the right;
	// empty cells (null launcher) are only at the end of the right column and are never targets
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
			target = t; // down in the same column
		}
		else if (col + 1 < cols && !is_placeholder(col + 1))
		{
			target = col + 1; // end of column -> top of the next column
		}
	}
	else
	{
		int t = idx - cols;
		if (t >= 0 && !is_placeholder(t))
		{
			target = t; // up in the same column
		}
		else if (col > 0)
		{
			for (int r = (n - 1) / cols; r >= 0; --r)
			{
				int c = r * cols + (col - 1);
				if (c < n && !is_placeholder(c))
				{
					target = c; // top of column -> end of the previous column
					break;
				}
			}
		}
	}
	gtk_tree_path_free(cursor);
	if (target < 0 || target >= n || is_placeholder(target))
	{
		return TRUE; // outer edge — stay
	}
	GtkTreePath* path = gtk_tree_path_new_from_indices(target, -1);
	gtk_icon_view_unselect_all(m_view);
	gtk_icon_view_select_path(m_view, path);
	gtk_icon_view_set_cursor(m_view, path, nullptr, FALSE);
	// Immediate, minimal scroll (no scroll_to_path animation)
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
	} // only scroll, minimally, if it is hidden
	gtk_tree_path_free(path);
	return TRUE;
}

//-----------------------------------------------------------------------------

void LauncherIconView::reload_icon_size()
{
	// Fetch icon size and the gaps from the spacing settings
	const int spacing = wm_settings->spacing | (wm_settings->spacing_rows << 6);
	if ((m_icon_size == wm_settings->launcher_icon_size.get_size()) && (m_spacing == spacing))
	{
		return;
	}
	m_icon_size = wm_settings->launcher_icon_size.get_size();
	m_spacing = spacing;

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

	// Gaps from the spacing settings: icon to label, columns, rows
	gtk_icon_view_set_spacing(m_view, wm_settings->spacing);
	// GtkIconView applies its "spacing" only between the two cells it makes
	// itself; with our own renderers the icon and the label touched. Set the
	// gap on the cell area directly.
	if (GtkCellArea* area = gtk_cell_layout_get_area(GTK_CELL_LAYOUT(m_view)))
	{
		if (GTK_IS_CELL_AREA_BOX(area))
		{
			gtk_cell_area_box_set_spacing(GTK_CELL_AREA_BOX(area), wm_settings->spacing);
		}
	}
	gtk_icon_view_set_column_spacing(m_view, wm_settings->spacing);
	// the list's own margin around its items
	gtk_icon_view_set_margin(m_view, wm_settings->spacing);
	gtk_icon_view_set_row_spacing(m_view, wm_settings->spacing_rows);

	// Icon size changed -> the cell width must be measured again (not the stale cache;
	// s_item_width is static and without measure(true) the menu width stays wrong)
	update_item_width(true);
}

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

GtkTreePath* LauncherIconView::model_path(GtkTreePath* path) const
{
	if (!path)
	{
		return nullptr;
	}
	if (gtk_icon_view_get_model(m_view) != GTK_TREE_MODEL(m_display) || !m_display)
	{
		return gtk_tree_path_copy(path);
	}
	const int i = gtk_tree_path_get_indices(path)[0];
	if (i < 0 || i >= static_cast<int>(m_to_model.size()) || m_to_model[i] < 0)
	{
		return nullptr;
	}
	return gtk_tree_path_new_from_indices(m_to_model[i], -1);
}

//-----------------------------------------------------------------------------

GtkTreePath* LauncherIconView::view_path(GtkTreePath* path) const
{
	if (!path)
	{
		return nullptr;
	}
	if (gtk_icon_view_get_model(m_view) != GTK_TREE_MODEL(m_display) || !m_display)
	{
		return gtk_tree_path_copy(path);
	}
	const int i = gtk_tree_path_get_indices(path)[0];
	if (i < 0 || i >= static_cast<int>(m_to_display.size()))
	{
		return nullptr;
	}
	return gtk_tree_path_new_from_indices(m_to_display[i], -1);
}

//-----------------------------------------------------------------------------

void LauncherIconView::disconnect_model()
{
	for (gulong& id : m_model_slots)
	{
		if (id && m_model)
		{
			g_signal_handler_disconnect(m_model, id);
		}
		id = 0;
	}
}

//-----------------------------------------------------------------------------

void LauncherIconView::set_column_major(int cols, int rows)
{
	cols = std::max(1, cols);
	rows = std::max(1, rows);
	if (cols == m_cm_cols && rows == m_cm_rows)
	{
		return;
	}
	m_cm_cols = cols;
	m_cm_rows = rows;
	gtk_icon_view_set_columns(m_view, cols);
	if (!m_model)
	{
		return;
	}
	if (cols > 1)
	{
		rebuild_display();
	}
	else if (gtk_icon_view_get_model(m_view) != m_model)
	{
		GtkTreePath* selected = get_selected_path();
		disconnect_model();
		gtk_icon_view_set_model(m_view, m_model);
		if (selected)
		{
			gtk_icon_view_select_path(m_view, selected);
			gtk_tree_path_free(selected);
		}
	}
}

//-----------------------------------------------------------------------------

void LauncherIconView::rebuild_display()
{
	if (!m_model)
	{
		return;
	}

	// keep the selection and cursor across the rebuild (model space)
	GtkTreePath* selected = get_selected_path();
	GtkTreePath* cursor = get_cursor();

	const int n = gtk_tree_model_iter_n_children(m_model, nullptr);
	const int cols = std::max(1, m_cm_cols);
	const int rows = std::max(m_cm_rows, (n + cols - 1) / cols);

	if (!m_display)
	{
		const int n_columns = gtk_tree_model_get_n_columns(m_model);
		std::vector<GType> types(n_columns);
		for (int i = 0; i < n_columns; ++i)
		{
			types[i] = gtk_tree_model_get_column_type(m_model, i);
		}
		m_display = gtk_list_store_newv(n_columns, types.data());
	}

	// detach while refilling: GtkIconView relayouts on every row otherwise
	gtk_icon_view_set_model(m_view, nullptr);
	gtk_list_store_clear(m_display);
	m_to_model.clear();
	m_to_display.assign(n, -1);

	std::vector<GtkTreeIter> source(n);
	GtkTreeIter iter;
	int k = 0;
	if (gtk_tree_model_get_iter_first(m_model, &iter))
	{
		do
		{
			source[k++] = iter;
		} while ((k < n) && gtk_tree_model_iter_next(m_model, &iter));
	}

	const int n_columns = gtk_tree_model_get_n_columns(m_model);
	const int used_rows = std::min(rows, std::max(1, n));
	for (int r = 0; r < used_rows; ++r)
	{
		for (int c = 0; c < cols; ++c)
		{
			const int s = c * rows + r;
			GtkTreeIter out;
			gtk_list_store_append(m_display, &out);
			const int d = static_cast<int>(m_to_model.size());
			if (s < n)
			{
				for (int col = 0; col < n_columns; ++col)
				{
					GValue value = G_VALUE_INIT;
					gtk_tree_model_get_value(m_model, &source[s], col, &value);
					gtk_list_store_set_value(m_display, &out, col, &value);
					g_value_unset(&value);
				}
				m_to_model.push_back(s);
				m_to_display[s] = d;
			}
			else
			{
				m_to_model.push_back(-1);
			}
		}
	}
	gtk_icon_view_set_model(m_view, GTK_TREE_MODEL(m_display));

	// follow later changes of the real model
	if (!m_model_slots[0])
	{
		auto changed = +[](gpointer data)
		{
			auto view = static_cast<LauncherIconView*>(data);
			if (!view->m_rebuild_id)
			{
				view->m_rebuild_id = g_idle_add_full(G_PRIORITY_HIGH_IDLE, +[](gpointer idle_data) -> gboolean
				{
					auto idle_view = static_cast<LauncherIconView*>(idle_data);
					idle_view->m_rebuild_id = 0;
					if (idle_view->m_cm_cols > 1)
					{
						idle_view->rebuild_display();
					}
					return G_SOURCE_REMOVE;
				}, view, nullptr);
			}
		};
		m_model_slots[0] = g_signal_connect_swapped(m_model, "row-inserted", G_CALLBACK(changed), this);
		m_model_slots[1] = g_signal_connect_swapped(m_model, "row-deleted", G_CALLBACK(changed), this);
		m_model_slots[2] = g_signal_connect_swapped(m_model, "row-changed", G_CALLBACK(changed), this);
		m_model_slots[3] = g_signal_connect_swapped(m_model, "rows-reordered", G_CALLBACK(changed), this);
	}

	if (selected)
	{
		select_path(selected);
		gtk_tree_path_free(selected);
	}
	if (cursor)
	{
		set_cursor(cursor);
		gtk_tree_path_free(cursor);
	}
}
