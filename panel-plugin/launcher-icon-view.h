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

#ifndef VIBRISMENU_LAUNCHER_ICON_VIEW_H
#define VIBRISMENU_LAUNCHER_ICON_VIEW_H

#include "launcher-view.h"

#include <string>
#include <vector>

namespace VibrisMenu
{

class LauncherIconView : public LauncherView
{
public:
	LauncherIconView();
	~LauncherIconView();

	GtkWidget* get_widget() const override
	{
		return GTK_WIDGET(m_view);
	}

	GtkTreePath* get_cursor() const override;
	GtkTreePath* get_path_at_pos(int x, int y) const override;
	GtkTreePath* get_selected_path() const override;
	bool is_path_selected(GtkTreePath* path) const override;
	void activate_path(GtkTreePath* path) override;
	void scroll_to_path(GtkTreePath* path) override;
	void select_path(GtkTreePath* path) override;
	void set_cursor(GtkTreePath* path) override;

	void set_fixed_height_mode(bool fixed_height) override;
	void set_selection_mode(GtkSelectionMode mode) override;

	void hide_tooltips() override;
	void show_tooltips() override;

	void clear_selection() override;
	void collapse_all() override;

	void set_model(GtkTreeModel* model) override;
	void unset_model() override;

	void set_drag_source(GdkModifierType start_button_mask, const GtkTargetEntry* targets, gint n_targets, GdkDragAction actions) override;
	void set_drag_dest(const GtkTargetEntry* targets, gint n_targets, GdkDragAction actions) override;
	void unset_drag_source() override;
	void unset_drag_dest() override;

	void reload_icon_size() override;
	void update_item_width(bool measure) override;
	gboolean wrap_navigation(GdkEventKey* event);
	void set_width_authority() override;
	void schedule_width_update();
	int measure_item_width() const;
	// Show the model column by column (top to bottom, then the next column)
	// in `cols` columns of at least `rows` rows; cols <= 1 shows it as is
	void set_column_major(int cols, int rows);
	GtkTreePath* model_path(GtkTreePath* view_path) const override;

public:
	static int s_item_width;

private:
	bool m_authority;
	bool m_width_pending;

	// column-major display copy of m_model (nullptr: m_model is shown directly)
	GtkListStore* m_display = nullptr;
	std::vector<int> m_to_model;   // display row -> model row, -1 = empty cell
	std::vector<int> m_to_display; // model row -> display row
	int m_cm_cols = 0;
	int m_cm_rows = 0;
	gulong m_model_slots[4] = {0, 0, 0, 0};
	guint m_rebuild_id = 0;
	void rebuild_display();
	void disconnect_model();
	GtkTreePath* view_path(GtkTreePath* path) const;

private:

private:
	GtkIconView* m_view;
	GtkCellRenderer* m_icon_renderer;
	GtkCellRenderer* m_text_renderer;
	std::string m_font_key;
	int m_icon_size;
};

}

#endif // VIBRISMENU_LAUNCHER_ICON_VIEW_H
