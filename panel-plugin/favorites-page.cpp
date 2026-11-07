/*
 * Copyright (C) 2013-2021 Graeme Gott <graeme@gottcode.org>
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

#include "favorites-page.h"

#include "applications-page.h"
#include "image-menu-item.h"
#include "launcher.h"
#include "launcher-icon-view.h"
#include "launcher-view.h"
#include "settings.h"
#include "slot.h"
#include "window.h"

#include <algorithm>

#include <glib/gi18n-lib.h>

using namespace WhiskerMenu;

int FavoritesPage::s_measured_pitch = 0;
bool FavoritesPage::drag_in_progress = false;

//-----------------------------------------------------------------------------

FavoritesPage::FavoritesPage(Window* window) :
	Page(window, "user-bookmarks", _("Favorites"))
{
	view_created();
}

//-----------------------------------------------------------------------------

FavoritesPage::~FavoritesPage()
{
	if (m_resync_source)
	{
		g_source_remove(m_resync_source);
	}
	unset_menu_items();
}

//-----------------------------------------------------------------------------

bool FavoritesPage::contains(Launcher* launcher) const
{
	if (!launcher)
	{
		return false;
	}

	std::string desktop_id(launcher->get_desktop_id());
	return std::find(wm_settings->favorites.begin(), wm_settings->favorites.end(), desktop_id) != wm_settings->favorites.end();
}

//-----------------------------------------------------------------------------

void FavoritesPage::add(Launcher* launcher)
{
	if (!launcher || contains(launcher))
	{
		return;
	}

	// The final order is set by sort_favorites() during relayout
	wm_settings->favorites.push_back(launcher->get_desktop_id());
	m_content_dirty = true;
	rebuild();
}

//-----------------------------------------------------------------------------

void FavoritesPage::remove(Launcher* launcher)
{
	if (!launcher)
	{
		return;
	}

	const std::string desktop_id(launcher->get_desktop_id());
	StringList& fav = wm_settings->favorites;
	for (int i = fav.size() - 1; i >= 0; --i)
	{
		if (fav[i] == desktop_id)
		{
			fav.erase(i);
		}
	}
	m_content_dirty = true;
	rebuild();
}

//-----------------------------------------------------------------------------

void FavoritesPage::set_menu_items()
{
	sort_favorites();

	StringList& fav = wm_settings->favorites;
	const int n = fav.size();

	// Columns: the column count is set by the user (/favorites-columns = 1..4; default 2),
	// and each column is filled up to its vertical capacity: an item moves to the next
	// column only once the current one is full. Overflow past the total capacity continues
	// in the last column with a vertical scrollbar. Width = used columns x widest item
	// + margins
	m_columns = (wm_settings->favorites_columns > 0) ? wm_settings->favorites_columns : 2;
	m_columns = std::min(m_columns, std::max(1, n));

	// Rows per column (column-first mode only; from the real/menu height)
	// Real capacity even with one column: it decides whether the scrollbar is
	// needed, and size-allocate compares it with m_built_capacity — using n
	// here made every drag frame look like a capacity change (needless
	// rebuild = blinking items) in single-column mode.
	const int capacity = get_window()->favorites_capacity_rows();

	const std::vector<int> old_slots = m_model_slots;
	const int old_used_cols = m_used_cols;
	m_model_slots.clear();
	m_overflow = false;

	if (m_columns == 1 || capacity < 1)
	{
		m_used_cols = 1;
		m_overflow = (n > capacity);
		for (int i = 0; i < n; ++i)
		{
			m_model_slots.push_back(i);
		}
	}
	else if (wm_settings->favorites_horizontal)
	{
		// Row-first: fill rows; cell (r,c) <- item r*cols + c
		m_used_cols = std::min(m_columns, std::max(1, n));
		const int rows = (n + m_columns - 1) / m_columns;
		for (int r = 0; r < rows; ++r)
		{
			for (int c = 0; c < m_columns; ++c)
			{
				const int f = r * m_columns + c;
				m_model_slots.push_back(f < n ? f : -1);
			}
		}
		m_overflow = rows > capacity;
	}
	else
	{
		// Column-first: fill up to capacity (capacity rows per column); an item moves to the
		// next column only when the current one is full. If the last column overflows,
		// the extra items continue below it (vertical scrollbar, not a new column)
		int rows = 0;
		int counts[4] = {0, 0, 0, 0};
		for (int c = 0; c < m_columns; ++c)
		{
			const int remaining = n - c * capacity;
			counts[c] = (c == m_columns - 1) ? std::max(0, remaining) : std::max(0, std::min(capacity, remaining));
			rows = std::max(rows, counts[c]);
		}
		m_used_cols = 0;
		for (int c = 0; c < m_columns; ++c)
		{
			if (counts[c] > 0)
			{
				m_used_cols = c + 1;
			}
		}
		m_overflow = n > m_columns * capacity;
		for (int r = 0; r < rows; ++r)
		{
			for (int c = 0; c < m_used_cols; ++c)
			{
				const int f = c * capacity + r;
				m_model_slots.push_back(r < counts[c] ? f : -1);
			}
		}
	}

	m_built_capacity = capacity;

	// Same content, only a capacity change (drag / height change): never
	// replace the model — a new model makes GtkIconView drop and re-lay-out
	// every item (the items blinking while dragging). Unchanged layout: do
	// nothing; changed layout: rewrite only the rows that differ, in place.
	GtkListStore* current = GTK_IS_LIST_STORE(gtk_icon_view_get_model(GTK_ICON_VIEW(get_view()->get_widget())))
			? GTK_LIST_STORE(gtk_icon_view_get_model(GTK_ICON_VIEW(get_view()->get_widget()))) : nullptr;
	if (current && !m_content_dirty)
	{
		if ((old_slots != m_model_slots) || (old_used_cols != m_used_cols))
		{
			m_inplace = true;
			GtkTreeModel* tm = GTK_TREE_MODEL(current);
			const int old_n = gtk_tree_model_iter_n_children(tm, nullptr);
			const int new_n = m_model_slots.size();
			for (int i = 0; i < new_n; ++i)
			{
				const int slot = m_model_slots[i];
				if ((i < old_n) && (i < (int)old_slots.size()) && (old_slots[i] == slot))
				{
					continue;
				}
				Launcher* launcher = slot >= 0 ? get_window()->get_applications()->find(fav[slot]) : nullptr;
				GtkTreeIter iter;
				if (i < old_n)
				{
					gtk_tree_model_iter_nth_child(tm, &iter, nullptr, i);
				}
				else
				{
					gtk_list_store_append(current, &iter);
				}
				gtk_list_store_set(current, &iter,
						LauncherView::COLUMN_ICON, launcher ? launcher->get_icon() : nullptr,
						LauncherView::COLUMN_TEXT, launcher ? launcher->get_text() : "",
						LauncherView::COLUMN_TOOLTIP, launcher ? launcher->get_tooltip() : nullptr,
						LauncherView::COLUMN_LAUNCHER, launcher,
						-1);
			}
			for (int i = old_n - 1; i >= new_n; --i)
			{
				GtkTreeIter iter;
				if (gtk_tree_model_iter_nth_child(tm, &iter, nullptr, i))
				{
					gtk_list_store_remove(current, &iter);
				}
			}
			if (old_used_cols != m_used_cols)
			{
				gtk_icon_view_set_columns(GTK_ICON_VIEW(get_view()->get_widget()), m_used_cols);
			}
			m_inplace = false;
		}
		apply_scrollbar_mode();
		return;
	}

	// Build the model — exactly one row per cell so m_model_slots shares row indexes
	GtkListStore* store = gtk_list_store_new(
			LauncherView::N_COLUMNS,
			G_TYPE_ICON,
			G_TYPE_STRING,
			G_TYPE_STRING,
			G_TYPE_POINTER);
	for (const int slot : m_model_slots)
	{
		Launcher* launcher = slot >= 0 ? get_window()->get_applications()->find(fav[slot]) : nullptr;
		if (launcher)
		{
			gtk_list_store_insert_with_values(
					store, nullptr, G_MAXINT,
					LauncherView::COLUMN_ICON, launcher->get_icon(),
					LauncherView::COLUMN_TEXT, launcher->get_text(),
					LauncherView::COLUMN_TOOLTIP, launcher->get_tooltip(),
					LauncherView::COLUMN_LAUNCHER, launcher,
					-1);
		}
		else
		{
			gtk_list_store_insert_with_values(
					store, nullptr, G_MAXINT,
					LauncherView::COLUMN_TEXT, "",
					-1);
		}
	}

	GtkTreeModel* model = GTK_TREE_MODEL(store);
	// Lay out only the columns that hold items — there is no empty column to see
	// or to enter with arrow-right
	gtk_icon_view_set_columns(GTK_ICON_VIEW(get_view()->get_widget()), m_used_cols);
	apply_scrollbar_mode();
	get_view()->set_model(model);
	get_view()->set_width_authority();
	// Item width is measured ONLY when the favorites content changed
	// (add / remove / rename / reload). Capacity relayouts — every drag
	// step, every height change — reuse the stored value.
	get_view()->update_item_width(m_content_dirty);
	m_content_dirty = false;

	connect(model, "row-changed",
		[this](GtkTreeModel* tree_model, GtkTreePath* path, GtkTreeIter* iter)
		{
			on_row_changed(tree_model, path, iter);
		});

	connect(model, "row-inserted",
		[this](GtkTreeModel* tree_model, GtkTreePath* path, GtkTreeIter* iter)
		{
			on_row_inserted(tree_model, path, iter);
		});

	connect(model, "row-deleted",
		[this](GtkTreeModel*, GtkTreePath* path)
		{
			on_row_deleted(path);
		});

	g_object_unref(model);
}

//-----------------------------------------------------------------------------

void FavoritesPage::unset_menu_items()
{
	// the next set_menu_items() comes with a (possibly) different item list
	m_content_dirty = true;
	// Clear treeview
	get_view()->unset_model();
}

//-----------------------------------------------------------------------------

void FavoritesPage::extend_context_menu(GtkWidget* menu)
{
	// Automatic ascending sort replaced manual ordering; nothing is added to the menu
	(void)menu;
}

//-----------------------------------------------------------------------------

bool FavoritesPage::remember_launcher(Launcher* launcher)
{
	return wm_settings->favorites_in_recent ? true : !contains(launcher);
}

//-----------------------------------------------------------------------------

void FavoritesPage::on_row_changed(GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter)
{
	if (m_inplace)
	{
		return; // our own relayout, not a user drag-and-drop
	}

	// A row filled in later (e.g. dragged from the applications page) is added to the list
	Element* element = nullptr;
	gtk_tree_model_get(model, iter, LauncherView::COLUMN_LAUNCHER, &element, -1);
	Launcher* launcher = dynamic_cast<Launcher*>(element);
	if (!launcher || contains(launcher))
	{
		return;
	}
	m_pending_add = launcher;
	schedule_resync();
}

//-----------------------------------------------------------------------------

void FavoritesPage::on_row_inserted(GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter)
{
	if (m_inplace)
	{
		return; // our own relayout, not a user drag-and-drop
	}

	Element* element = nullptr;
	gtk_tree_model_get(model, iter, LauncherView::COLUMN_LAUNCHER, &element, -1);
	Launcher* launcher = dynamic_cast<Launcher*>(element);
	if (!launcher || contains(launcher))
	{
		return;
	}
	m_pending_add = launcher;
	schedule_resync();
}

//-----------------------------------------------------------------------------

void FavoritesPage::on_row_deleted(GtkTreePath* path)
{
	if (m_inplace)
	{
		return; // our own relayout, not a user drag-and-drop
	}

	// Map the model position back to the logical favorites index
	const int pos = gtk_tree_path_get_indices(path)[0];
	if (pos < 0 || pos >= static_cast<int>(m_model_slots.size()))
	{
		return;
	}
	const int logical = m_model_slots[pos];
	if (logical < 0 || logical >= wm_settings->favorites.size())
	{
		return;
	}
	if (m_pending_remove < 0)
	{
		m_pending_remove = logical;
	}
	schedule_resync();
}

//-----------------------------------------------------------------------------

std::vector<Launcher*> FavoritesPage::sort() const
{
	std::vector<Launcher*> items;
	items.reserve(wm_settings->favorites.size());
	for (const auto& favorite : wm_settings->favorites)
	{
		Launcher* launcher = get_window()->get_applications()->find(favorite);
		if (!launcher)
		{
			continue;
		}
		items.push_back(launcher);
	}
	std::sort(items.begin(), items.end(), &Element::less_than);
	return items;
}

//-----------------------------------------------------------------------------

void FavoritesPage::sort_favorites()
{
	// Automatic ascending sort by name; ids without a launcher are kept at the end
	std::vector<Launcher*> items;
	items.reserve(wm_settings->favorites.size());
	StringList missing("", {});
	for (const auto& favorite : wm_settings->favorites)
	{
		Launcher* launcher = get_window()->get_applications()->find(favorite);
		if (launcher)
		{
			items.push_back(launcher);
		}
		else
		{
			missing.push_back(favorite);
		}
	}
	std::sort(items.begin(), items.end(), &Element::less_than);

	StringList& fav = wm_settings->favorites;
	fav.clear();
	for (auto launcher : items)
	{
		fav.push_back(launcher->get_desktop_id());
	}
	for (const auto& id : missing)
	{
		fav.push_back(id);
	}
}

//-----------------------------------------------------------------------------

void FavoritesPage::rebuild()
{
	// The menu width is synced only after a real content/layout change —
	// rebuild is not called from the show path
	set_menu_items();
	get_window()->sync_width();
}

//-----------------------------------------------------------------------------

void FavoritesPage::on_height_changed()
{
	// A menu height change = a column capacity change -> relayout; this is also a kind
	// of content change and the only place the distribution may change after a resize
	rebuild();
}

//-----------------------------------------------------------------------------

void FavoritesPage::schedule_resync()
{
	if (m_resync_source)
	{
		return; // coalesce: the pending idle runs with the latest state
	}
	if (get_window()->is_resizing())
	{
		// While dragging: no delay — a high-priority idle runs before the paint of the same
		// frame; later motions either reach the pending idle (which uses the new data) or
		// create a new one. That is how a half-done relayout gets cancelled.
		m_resync_source = g_idle_add_full(G_PRIORITY_HIGH_IDLE, +[](gpointer data) -> gint
		{
			auto page = static_cast<FavoritesPage*>(data);
			page->m_resync_source = 0;
			page->resync_now();
			return G_SOURCE_REMOVE;
		}, this, nullptr);
	}
	else
	{
		m_resync_source = g_timeout_add(400, +[](gpointer data) -> gint
		{
			auto page = static_cast<FavoritesPage*>(data);
			page->m_resync_source = 0;
			page->resync_now();
			return G_SOURCE_REMOVE;
		}, this);
	}
}

//-----------------------------------------------------------------------------

void FavoritesPage::sync_capacity()
{
	// Runs on every show(): only the real row pitch is read here (needs a
	// realized view). The item width is NOT re-measured on show — it was
	// measured when the favorites content last changed.
	GtkAllocation a;
	gtk_widget_get_allocation(get_view()->get_widget(), &a);
	if (a.height <= 1)
	{
		return;
	}
	GtkTreePath* path = gtk_tree_path_new_from_indices(0, -1);
	GdkRectangle rect = {0, 0, 0, 0};
	if (gtk_icon_view_get_cell_rect(GTK_ICON_VIEW(get_view()->get_widget()), path, nullptr, &rect)
			&& (rect.height > 0))
	{
		gint rs = 0;
		g_object_get(G_OBJECT(get_view()->get_widget()), "row-spacing", &rs, nullptr);
		s_measured_pitch = rect.height + rs;
	}
	gtk_tree_path_free(path);
	const int cap = get_window()->favorites_capacity_rows();
	if (cap != m_built_capacity)
	{
		resync_now();
	}
}

//-----------------------------------------------------------------------------

void FavoritesPage::resync_now()
{
	// Relayout stays live while dragging (every change is applied immediately)
	if (get_window()->is_resizing())
	{
		get_window()->update_min_h();
	}
	if (m_pending_add)
	{
		// The final order is set by sort_favorites() during relayout
		wm_settings->favorites.push_back(m_pending_add->get_desktop_id());
		m_pending_add = nullptr;
		m_content_dirty = true;
	}
	if (m_pending_remove >= 0)
	{
		if (m_pending_remove < wm_settings->favorites.size())
		{
			wm_settings->favorites.erase(m_pending_remove);
			m_content_dirty = true;
		}
		m_pending_remove = -1;
	}
	rebuild();
}

//-----------------------------------------------------------------------------

void FavoritesPage::view_created()
{
	// Dragging stays enabled for adding/removing; the order comes from the automatic sort;
	// the column count is set only by the user
	set_reorderable(true);

	// Track a real DnD
	g_signal_connect(get_view()->get_widget(), "drag-begin",
		G_CALLBACK(+[](GtkWidget*, GdkDragContext*, gpointer) -> void
		{
			FavoritesPage::drag_in_progress = true;
		}), nullptr);
	g_signal_connect(get_view()->get_widget(), "drag-end",
		G_CALLBACK(+[](GtkWidget*, GdkDragContext*, gpointer) -> void
		{
			FavoritesPage::drag_in_progress = false;
		}), nullptr);

	// No horizontal scrolling ever: the menu is always as wide as its content.
	// Vertical: AUTOMATIC (overlay) or ALWAYS (reserved) — see
	// apply_scrollbar_mode(). f456d07 had set it to NEVER, which makes the
	// scrolled window's minimum height = full content height: that is what
	// made dragging the top edge down "stiff" and removed the scrollbar.
	GtkWidget* sw = get_widget();
	apply_scrollbar_mode();
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sw), 1);

	// Empty column slots (placeholders) must never look selected — neither by
	// hover nor by arrow keys. Connected ONCE here; it used to be connected in
	// every set_menu_items(), i.e. one more handler on every drag step.
	g_signal_connect(get_view()->get_widget(), "selection-changed",
		G_CALLBACK(+[](GtkIconView* view, gpointer) -> void
		{
			GList* sel = gtk_icon_view_get_selected_items(view);
			if (!sel)
			{
				return;
			}
			GtkTreeModel* m = gtk_icon_view_get_model(view);
			gboolean any_real = FALSE;
			for (GList* l = sel; l; l = l->next)
			{
				GtkTreeIter it;
				if (m && gtk_tree_model_get_iter(m, &it, static_cast<GtkTreePath*>(l->data)))
				{
					WhiskerMenu::Element* e = nullptr;
					gtk_tree_model_get(m, &it, LauncherView::COLUMN_LAUNCHER, &e, -1);
					if (e)
					{
						any_real = TRUE;
					}
				}
			}
			g_list_free_full(sel, reinterpret_cast<GDestroyNotify>(&gtk_tree_path_free));
			if (!any_real)
			{
				gtk_icon_view_unselect_all(view);
			}
		}),
		nullptr);

	// Live relayout while dragging (on every motion, not only on release):
	// a height allocation change during the drag = a capacity change -> schedule_resync,
	// which runs as a high-priority idle (no delay, coalesced). Syncing directly inside
	// size-allocate was tried and corrupts the model mid-allocation (the list emptied)
	// — the idle sits between allocate and paint, so the relayout finishes before the frame renders.
	// Outside a drag the capacity is computed from menu_height when the content changes.
	connect(get_view()->get_widget(), "size-allocate",
		[this](GtkWidget*, GdkRectangle*) -> gboolean
		{
			// Scrollbar visible only on real overflow (every column full),
			// decided from the requested geometry, not the lagging adjustment:
			// while items are still being moved to the next column it stays
			// hidden. Opacity has no layout effect, so no width flash.
			GtkWidget* vsb = gtk_scrolled_window_get_vscrollbar(GTK_SCROLLED_WINDOW(get_widget()));
			const bool overflow = has_overflow();
			gtk_widget_set_opacity(vsb, overflow ? 1.0 : 0.0);
			gtk_widget_set_sensitive(vsb, overflow);
			if (!scrollbar_overlay_effective() && (overflow != m_sb_shown))
			{
				// overflow flipped: show/hide the scrollbar in its reserved
				// strip (width unchanged) — outside the allocation pass
				g_idle_add_full(G_PRIORITY_HIGH_IDLE, +[](gpointer data) -> gboolean
				{
					static_cast<FavoritesPage*>(data)->apply_scrollbar_mode();
					return G_SOURCE_REMOVE;
				}, this, nullptr);
				m_sb_shown = overflow;
			}
			if (get_window()->is_resizing())
			{
				// Relayout only on a real capacity change — not on every drag frame. A needless
				// rebuild emptied and refilled the model, making the items flicker
				const int cap = get_window()->favorites_capacity_rows();
				if (cap != m_built_capacity)
				{
					schedule_resync();
				}
			}
			return FALSE;
		});
}
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

bool FavoritesPage::scrollbar_overlay_effective()
{
	// GtkScrolledWindow uses overlay indicators only when all three agree
	// (gtkscrolledwindow.c, update_use_indicators): our widget property, the
	// gtk-overlay-scrolling setting, and GTK_OVERLAY_SCROLLING != "0".
	// If GTK will not overlay, we must reserve the space ourselves —
	// otherwise the scrollbar takes layout width when it appears.
	gboolean setting = TRUE;
	g_object_get(gtk_settings_get_default(), "gtk-overlay-scrolling", &setting, nullptr);
	return setting && (g_strcmp0(g_getenv("GTK_OVERLAY_SCROLLING"), "0") != 0);
}

//-----------------------------------------------------------------------------

void FavoritesPage::apply_scrollbar_mode()
{
	GtkScrolledWindow* sw = GTK_SCROLLED_WINDOW(get_widget());
	if (scrollbar_overlay_effective())
	{
		// floats over the content, takes no width
		g_object_set(sw, "overlay-scrolling", TRUE, nullptr);
		gtk_scrolled_window_set_policy(sw, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	}
	else
	{
		// Its width is always reserved (get_scrollbar_reserve), so the
		// favorites width never changes. No overflow: EXTERNAL = scrollbar
		// hidden, the reserved strip stays empty, and the content does not
		// force the minimum height (NEVER would = stiff drag). Overflow
		// (every column full): ALWAYS — it appears in the reserved strip.
		g_object_set(sw, "overlay-scrolling", FALSE, nullptr);
		m_sb_shown = has_overflow();
		gtk_scrolled_window_set_policy(sw, GTK_POLICY_NEVER,
				m_sb_shown ? GTK_POLICY_ALWAYS : GTK_POLICY_EXTERNAL);
	}
}

//-----------------------------------------------------------------------------

int FavoritesPage::get_scrollbar_reserve()
{
	if (scrollbar_overlay_effective())
	{
		return 0;
	}
	// constant: measured once (the CSS width does not depend on visibility)
	static int s_reserve = 0;
	if (s_reserve <= 0)
	{
		GtkWidget* vsb = gtk_scrolled_window_get_vscrollbar(GTK_SCROLLED_WINDOW(get_widget()));
		gint minimum = 0, natural = 0;
		gtk_widget_get_preferred_width(vsb, &minimum, &natural);
		s_reserve = std::max(minimum, natural);
	}
	return s_reserve;
}

//-----------------------------------------------------------------------------

bool FavoritesPage::has_overflow() const
{
	GtkTreeModel* model = gtk_icon_view_get_model(GTK_ICON_VIEW(get_view()->get_widget()));
	if (!model)
	{
		return false;
	}
	const int n = wm_settings->favorites.size();
	const int cols = std::max(1, m_columns);
	return n > cols * get_window()->favorites_capacity_rows();
}
