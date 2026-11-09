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

#ifndef VIBRISMENU_FAVORITES_PAGE_H
#define VIBRISMENU_FAVORITES_PAGE_H

#include "page.h"

#include <vector>

namespace VibrisMenu
{

class FavoritesPage : public Page
{
public:
	// During a real drag-and-drop (adding to/removing from the list) category
	// hover and hover-selection must stay out of the way
	static bool drag_in_progress;
public:
	// Relayout now if the menu height changed the column capacity: called in
	// the same main-loop turn as the height change, so both are painted in
	// one frame (never the new height with the old columns or a scrollbar)
	void relayout_if_capacity_changed();
	void resync_now();
	explicit FavoritesPage(Window* window);
	~FavoritesPage();

	bool contains(Launcher* launcher) const;

	void add(Launcher* launcher);
	void remove(Launcher* launcher);
	void set_menu_items();
	void unset_menu_items();
	void on_height_changed();
	// Real capacity after the pump in show — resync if it differs from the last layout
	void sync_capacity();
	// Number of columns that hold items — the basis of the menu width (the
	// configured column count is a maximum; an empty column takes no space)
	int get_used_columns() const
	{
		return m_used_cols;
	}
	// width the vertical scrollbar permanently occupies (0 when it overlays)
	int get_scrollbar_reserve();
	// every column full: the only state in which the scrollbar is shown
	bool has_overflow() const;

private:
	static int placeholder_guard;
	// Real row pitch measured after the first render — estimate for later shows
	static int s_measured_pitch;
	friend class Window;
	void extend_context_menu(GtkWidget* menu) override;
	bool remember_launcher(Launcher* launcher) override;
	void on_row_changed(GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter);
	void on_row_inserted(GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter);
	void on_row_deleted(GtkTreePath* path);
	std::vector<Launcher*> sort() const;
	void sort_favorites();
	void rebuild();
	void schedule_resync();
	void view_created() override;
	static bool scrollbar_overlay_effective();
	void apply_scrollbar_mode();

	guint m_resync_source = 0;
	Launcher* m_pending_add = nullptr;
	int m_pending_remove = -1;
	int m_columns = 2;
	int m_used_cols = 1;
	int m_built_capacity = 0;
	// Overflow past the capacity of all columns = a real vertical scrollbar
	bool m_overflow = false;
	// non-overlay mode: scrollbar currently shown (policy ALWAYS) vs EXTERNAL
	bool m_sb_shown = false;
	// set_menu_items() is rewriting rows itself (ignore row signals)
	bool m_inplace = false;
	// favorites list content changed since the last width measurement
	bool m_content_dirty = true;
	// Maps model cells (row-major) to logical favorites indexes; -1 = empty cell
	std::vector<int> m_model_slots;
};

}

#endif // VIBRISMENU_FAVORITES_PAGE_H
