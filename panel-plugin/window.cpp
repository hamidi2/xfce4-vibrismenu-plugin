/*
 * Copyright (C) 2013-2025 Graeme Gott <graeme@gottcode.org>
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

#include "window.h"

#include "applications-page.h"
#include "category-button.h"
#include "command.h"
#include "favorites-page.h"
#include "launcher-icon-view.h"
#include "launcher-view.h"
#include "plugin.h"
#include "profile.h"
#include "recent-page.h"
#include "resizer.h"
#include "search-page.h"
#include "settings.h"
#include "slot.h"

#include <libxfce4ui/libxfce4ui.h>
#include <gdk/gdkkeysyms.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#ifdef HAVE_GTK_LAYER_SHELL
#include <algorithm>

#include <gtk-layer-shell.h>
#endif

#include <ctime>

using namespace WhiskerMenu;

//-----------------------------------------------------------------------------

WhiskerMenu::Window::Window(Plugin* plugin) :
	m_plugin(plugin),
	m_window(nullptr),
	m_position(PositionAtButton),
	m_sidebar_size_group(nullptr),
	m_geometry{0,0,1,1},
	m_layout_ltr(true),
	m_layout_categories_horizontal(false),
	m_layout_categories_alternate(false),
	m_layout_search_alternate(false),
	m_layout_commands_alternate(false),
	m_layout_profile_alternate(false),
	m_profile_shape(0),
	m_supports_alpha(false),
	m_child_has_focus(false),
	m_resizing(false)
{
	// Create the window
	m_window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
	gtk_widget_set_name(GTK_WIDGET(m_window), "whiskermenu-window");
	// Untranslated window title to allow window managers to identify it; not visible to users.
	gtk_window_set_title(m_window, "Vibris Menu");
#ifdef HAVE_GTK_LAYER_SHELL
	if (!gtk_layer_is_supported())
#endif
	{
		gtk_window_set_modal(m_window, true);
	}
	gtk_window_set_decorated(m_window, false);
	gtk_window_set_skip_taskbar_hint(m_window, true);
	gtk_window_set_skip_pager_hint(m_window, true);
	// South-west gravity: on a height change X itself adjusts y so the bottom edge
	// stays put — atomically, in the same resize request. This removes the bottom
	// flicker (separate move/resize frames) since y no longer comes from us
	gtk_window_set_gravity(m_window, GDK_GRAVITY_SOUTH_WEST);
	gtk_window_set_type_hint(m_window, GDK_WINDOW_TYPE_HINT_MENU);
	gtk_window_stick(m_window);
	gtk_widget_add_events(GTK_WIDGET(m_window), GDK_FOCUS_CHANGE_MASK | GDK_STRUCTURE_MASK);

#ifdef HAVE_GTK_LAYER_SHELL
	if (gtk_layer_is_supported())
	{
		gtk_layer_init_for_window(m_window);

		// Position from top left, and excludes other windows
		gtk_layer_set_exclusive_zone(m_window, -1);
		gtk_layer_set_anchor(m_window, GTK_LAYER_SHELL_EDGE_TOP, true);
		gtk_layer_set_anchor(m_window, GTK_LAYER_SHELL_EDGE_BOTTOM, false);
		gtk_layer_set_anchor(m_window, GTK_LAYER_SHELL_EDGE_LEFT, true);
		gtk_layer_set_anchor(m_window, GTK_LAYER_SHELL_EDGE_RIGHT, false);

		// Grab keyboard focus when shown
		gtk_layer_set_keyboard_mode(m_window, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

		// Position menu above other windows
		gtk_layer_set_layer(m_window, GTK_LAYER_SHELL_LAYER_OVERLAY);
	}
#endif

	connect(m_window, "enter-notify-event",
		[this](GtkWidget*, GdkEvent*) -> gboolean
		{
			m_child_has_focus = false;
			return GDK_EVENT_PROPAGATE;
		});

	connect(m_window, "focus-in-event",
		[this](GtkWidget*, GdkEvent*) -> gboolean
		{
			m_child_has_focus = false;
			return GDK_EVENT_PROPAGATE;
		});

	connect(m_window, "focus-out-event",
		[this](GtkWidget* widget, GdkEvent*) -> gboolean
		{
			if (wm_settings->stay_on_focus_out || m_child_has_focus || !gtk_widget_get_visible(widget))
			{
				return GDK_EVENT_PROPAGATE;
			}

			// Needed to make focus out event happen after button press event,
			// otherwise it is impossible to toggle panel button.
			g_idle_add(
				+[](gpointer user_data) -> gboolean
				{
					static_cast<Window*>(user_data)->hide(true);
					return G_SOURCE_REMOVE;
				},
			this);

			return GDK_EVENT_PROPAGATE;
		});

	connect(m_window, "key-press-event",
		[this](GtkWidget* widget, GdkEvent* event) -> gboolean
		{
			return on_key_press_event(widget, reinterpret_cast<GdkEventKey*>(event));
		});

	connect(m_window, "key-press-event",
		[this](GtkWidget* widget, GdkEvent* event) -> gboolean
		{
			return on_key_press_event_after(widget, reinterpret_cast<GdkEventKey*>(event));
		},
		Connect::After);

	connect(m_window, "map-event",
		[this](GtkWidget*, GdkEvent*) -> gboolean
		{
			return on_map_event();
		});

	connect(m_window, "state-flags-changed",
		[this](GtkWidget* widget, GtkStateFlags)
		{
			on_state_flags_changed(widget);
		});

	g_signal_connect(G_OBJECT(m_window), "delete-event", G_CALLBACK(&gtk_widget_hide_on_delete), nullptr);

	// Create the border of the window
	GtkWidget* frame = gtk_frame_new(nullptr);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
	gtk_container_add(GTK_CONTAINER(m_window), frame);
	m_frame = frame;

	// Create window contents stack
	m_window_stack = GTK_STACK(gtk_stack_new());
	gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(m_window_stack));

	// Create loading message
	m_window_load_spinner = GTK_SPINNER(gtk_spinner_new());
	gtk_widget_set_halign(GTK_WIDGET(m_window_load_spinner), GTK_ALIGN_CENTER);
	gtk_widget_set_valign(GTK_WIDGET(m_window_load_spinner), GTK_ALIGN_CENTER);
	gtk_stack_add_named(m_window_stack, GTK_WIDGET(m_window_load_spinner), "load");

	// Create resizers — only the top edge is draggable (the menu sits on a bottom panel);
	// the other edges did not work right (left moved the menu, right did nothing)
	m_resize[Resizer::TopLeft] = nullptr;
	m_resize[Resizer::Top] = new Resizer(Resizer::Top, this);
	m_resize[Resizer::TopRight] = nullptr;
	m_resize[Resizer::Left] = nullptr;
	m_resize[Resizer::Right] = nullptr;
	m_resize[Resizer::BottomLeft] = nullptr;
	m_resize[Resizer::Bottom] = nullptr;
	m_resize[Resizer::BottomRight] = nullptr;

	// Create the profile picture and username label
	m_profile = new Profile(this);

	// Create action buttons
	for (int i = 0; i < 9; ++i)
	{
		m_commands_button[i] = wm_settings->command[i]->get_button();
		m_command_slots[i] = connect(m_commands_button[i], "clicked",
			[this](GtkButton*)
			{
				hide();
			});
		// Header button icons = the favorites icon size (GTK_ICON_SIZE_DIALOG
		// depends on the theme and does not match launcher-icon-size)
		gtk_container_foreach(GTK_CONTAINER(m_commands_button[i]),
			+[](GtkWidget* w, gpointer)
			{
				if (GTK_IS_IMAGE(w))
				{
					gtk_image_set_pixel_size(GTK_IMAGE(w), wm_settings->launcher_icon_size.get_size());
				}
			}, nullptr);
	}

	// Create search entry
	m_search_entry = GTK_ENTRY(gtk_search_entry_new());
	gtk_window_set_focus(m_window, GTK_WIDGET(m_search_entry));

	connect(m_search_entry, "changed",
		[this](GtkEditable*)
		{
			search();
		});

	connect(m_search_entry, "populate-popup",
		[this](GtkEntry*, GtkWidget*)
		{
			set_child_has_focus();
		});

	// Create favorites
	m_favorites = new FavoritesPage(this);

	CategoryButton* favorites_button = m_favorites->get_button();
	connect(favorites_button->get_widget(), "toggled",
		[this](GtkToggleButton*)
		{
			favorites_toggled();
		});

	// Create recent
	m_recent = new RecentPage(this);

	CategoryButton* recent_button = m_recent->get_button();
	recent_button->join_group(favorites_button);
	connect(recent_button->get_widget(), "toggled",
		[this](GtkToggleButton*)
		{
			recent_toggled();
		});

	// Create applications
	m_applications = new ApplicationsPage(this);

	CategoryButton* applications_button = m_applications->get_button();
	applications_button->join_group(recent_button);
	connect(applications_button->get_widget(), "toggled",
		[this](GtkToggleButton*)
		{
			category_toggled();
		});

	// Create search results
	m_search_results = new SearchPage(this);

	GtkBox* search_results = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
	gtk_box_pack_start(search_results, m_search_results->get_message(), false, false, 0);
	gtk_box_pack_start(search_results, m_search_results->get_widget(), true, true, 0);
	gtk_container_set_border_width(GTK_CONTAINER(search_results), 0);

	// The top resizer lies over the top gap of the contents (an overlay, not
	// a row of its own), so the gap can shrink where the header icons bring
	// their own transparent border
	GtkOverlay* overlay = GTK_OVERLAY(gtk_overlay_new());
	GtkWidget* top_grip = m_resize[Resizer::Top]->get_widget();
	gtk_widget_set_valign(top_grip, GTK_ALIGN_START);
	gtk_widget_set_hexpand(top_grip, true);
	gtk_stack_add_named(m_window_stack, GTK_WIDGET(overlay), "contents");

	// Create box for packing children
	m_vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
	gtk_container_set_border_width(GTK_CONTAINER(m_vbox), 0);
	gtk_widget_set_margin_bottom(GTK_WIDGET(m_vbox), 6);
	gtk_widget_set_margin_top(GTK_WIDGET(m_vbox), 6);
	gtk_container_add(GTK_CONTAINER(overlay), GTK_WIDGET(m_vbox));
	gtk_overlay_add_overlay(overlay, top_grip);

	// Create box for packing commands
	m_commands_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
	m_commands_spacer = gtk_label_new(nullptr);
	gtk_box_pack_start(m_commands_box, m_commands_spacer, true, true, 0);
	for (auto command : m_commands_button)
	{
		gtk_box_pack_start(m_commands_box, command, false, false, 0);
	}

	// Create box for packing username and commands
	m_title_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6));
	// Fully fixed header: the theme padding of the buttons is zeroed so every gap is 6px
	// (left of avatar = between buttons = right of logout = 6; independent of theme and icon size)
	{
		GtkCssProvider* provider = gtk_css_provider_new();
		gtk_css_provider_load_from_data(provider,
				".command-button { padding: 0px; min-width: 0px; min-height: 0px; }", -1, nullptr);
		gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
				GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_unref(provider);
		gtk_box_set_spacing(m_commands_box, 6);
	}
	gtk_box_pack_start(m_vbox, GTK_WIDGET(m_title_box), false, false, 0);
	gtk_box_pack_start(m_title_box, m_profile->get_picture(), false, false, 0);
	gtk_box_pack_start(m_title_box, m_profile->get_username(), true, true, 0);
	gtk_box_pack_start(m_title_box, GTK_WIDGET(m_commands_box), false, false, 0);
	// the avatar is drawn with a 2px inset (profile.cpp): 4 + 2 = 6 visible
	gtk_widget_set_margin_start(m_profile->get_picture(), 4);
	gtk_widget_set_margin_end(m_profile->get_picture(), 6);
	// Both margins: in an RTL session margin_start flips sides. The icon
	// glyphs carry their own transparent border inside the button, so the
	// margin is smaller than 6 for the visible gap to the edge to be 6px
	// (adjusted in show() for the transparent border of the icons)
	gtk_widget_set_margin_start(GTK_WIDGET(m_commands_box), 6);
	gtk_widget_set_margin_end(GTK_WIDGET(m_commands_box), 6);

	// Add search to layout
	m_search_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6));
	gtk_box_pack_start(m_vbox, GTK_WIDGET(m_search_box), false, true, 0);
	gtk_box_pack_start(m_search_box, GTK_WIDGET(m_search_entry), true, true, 0);

	// Create box for packing launcher pages and sidebar
	m_contents_stack = GTK_STACK(gtk_stack_new());
	m_contents_box = GTK_GRID(gtk_grid_new());
	gtk_grid_set_column_spacing(m_contents_box, 6);
	gtk_grid_set_row_spacing(m_contents_box, 0);
	gtk_stack_add_named(m_contents_stack, GTK_WIDGET(m_contents_box), "contents");
	gtk_stack_add_named(m_contents_stack, GTK_WIDGET(search_results), "search");
	gtk_box_pack_start(m_vbox, GTK_WIDGET(m_contents_stack), true, true, 0);
	// side gap equal to the top resize grip and the inner gaps (6px); the
	// title area keeps its own 6px margins, so it is not included here
	for (GtkWidget* widget : {GTK_WIDGET(m_contents_stack), GTK_WIDGET(m_search_box)})
	{
		gtk_widget_set_margin_start(widget, 6);
		gtk_widget_set_margin_end(widget, 6);
	}

	// Create box for packing categories horizontally
	m_categories_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
	gtk_grid_attach(m_contents_box, GTK_WIDGET(m_categories_box), 0, 0, 2, 1);

	// Create box for packing launcher pages
	m_panels_stack = GTK_STACK(gtk_stack_new());
	// The window width comes from the active page, not the maximum of all pages
	gtk_stack_set_hhomogeneous(m_panels_stack, FALSE);
	gtk_grid_attach(m_contents_box, GTK_WIDGET(m_panels_stack), 0, 1, 1, 1);
	gtk_widget_set_hexpand(GTK_WIDGET(m_panels_stack), true);
	gtk_widget_set_vexpand(GTK_WIDGET(m_panels_stack), true);
	gtk_stack_add_named(m_panels_stack, m_favorites->get_widget(), "favorites");
	gtk_stack_add_named(m_panels_stack, m_recent->get_widget(), "recent");
	gtk_stack_add_named(m_panels_stack, m_applications->get_widget(), "applications");

	// Menu width = width of the active page's content; update it on page change (or search)
	auto on_page_switch = +[](GObject*, GParamSpec*, gpointer user_data) -> void
	{
		g_idle_add(+[](gpointer data) -> gint
		{
			static_cast<Window*>(data)->set_size(wm_settings->menu_width, wm_settings->menu_height);
			return G_SOURCE_REMOVE;
		}, user_data);
	};
	g_signal_connect(m_panels_stack, "notify::visible-child", G_CALLBACK(on_page_switch), this);
	g_signal_connect(m_contents_stack, "notify::visible-child", G_CALLBACK(on_page_switch), this);

	// Create box for packing sidebar
	m_category_buttons = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
	gtk_box_pack_start(m_category_buttons, favorites_button->get_widget(), false, false, 0);
	gtk_box_pack_start(m_category_buttons, recent_button->get_widget(), false, false, 0);
	gtk_box_pack_start(m_category_buttons, applications_button->get_widget(), false, false, 0);
	gtk_box_pack_start(m_category_buttons, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), false, false, 4);

	m_sidebar = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(nullptr, nullptr));
	gtk_grid_attach(m_contents_box, GTK_WIDGET(m_sidebar), 1, 1, 1, 1);
	gtk_scrolled_window_set_propagate_natural_height(m_sidebar, true);
	gtk_scrolled_window_set_shadow_type(m_sidebar, GTK_SHADOW_NONE);
	gtk_scrolled_window_set_policy(m_sidebar, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(m_sidebar), GTK_WIDGET(m_category_buttons));

	// Handle default page
	reset_default_button();

	// Add CSS classes
	gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(m_window)), "whiskermenu");
	gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(m_search_box)), "search-area");
	gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(m_title_box)), "title-area");
	gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(m_commands_box)), "commands-area");
	gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(m_contents_stack)), "contents");

	GtkStyleContext* context = gtk_widget_get_style_context(GTK_WIDGET(m_category_buttons));
	gtk_style_context_add_class(context, "categories");
	gtk_style_context_add_class(context, "right");

	// Show widgets
	gtk_widget_show_all(frame);
	m_default_button->set_active(true);

	// Handle transparency
	gtk_widget_set_app_paintable(GTK_WIDGET(m_window), true);

	connect(m_window, "draw",
		[this](GtkWidget* widget, cairo_t* cr) -> gboolean
		{
			return on_draw_event(widget, cr);
		});

	connect(m_window, "screen-changed",
		[this](GtkWidget* widget, GdkScreen*)
		{
			on_screen_changed(widget);
		});
	on_screen_changed(GTK_WIDGET(m_window));

	// Load applications
	m_applications->load();

	// DnD from any page (applications/recent/favorites) sets the global flag
	// so category hover-activate does not switch pages during the drag (the old guard
	// covered only favorites, not drags started from All Applications)
	for (Page* page : { static_cast<Page*>(m_search_results), static_cast<Page*>(m_recent),
			static_cast<Page*>(m_applications), static_cast<Page*>(m_favorites) })
	{
		g_signal_connect(page->get_view()->get_widget(), "drag-begin",
			G_CALLBACK(+[](GtkWidget*, GdkDragContext*, gpointer) -> void
			{
				FavoritesPage::drag_in_progress = true;
			}), nullptr);
		g_signal_connect(page->get_view()->get_widget(), "drag-end",
			G_CALLBACK(+[](GtkWidget*, GdkDragContext*, gpointer) -> void
			{
				FavoritesPage::drag_in_progress = false;
			}), nullptr);
	}

	g_object_ref_sink(m_window);
}

//-----------------------------------------------------------------------------

WhiskerMenu::Window::~Window()
{
	if (m_last_category)
	{
		g_object_remove_weak_pointer(G_OBJECT(m_last_category), reinterpret_cast<gpointer*>(&m_last_category));
	}
	if (m_page_width_id)
	{
		g_source_remove(m_page_width_id);
	}

	for (int i = 0; i < 9; ++i)
	{
		g_signal_handler_disconnect(m_commands_button[i], m_command_slots[i]);
		gtk_container_remove(GTK_CONTAINER(m_commands_box), m_commands_button[i]);
	}

	delete m_applications;
	delete m_search_results;
	delete m_recent;
	delete m_favorites;

	delete m_profile;

	for (Resizer* resizer : m_resize)
	{
		delete resizer;
	}

	gtk_widget_destroy(GTK_WIDGET(m_window));
	g_object_unref(m_window);
}

//-----------------------------------------------------------------------------

Page* WhiskerMenu::Window::get_active_page()
{
	Page* page = nullptr;
	if (g_strcmp0(gtk_stack_get_visible_child_name(m_contents_stack), "search") == 0)
	{
		page = m_search_results;
	}
	else if (m_favorites->get_button()->get_active())
	{
		page = m_favorites;
	}
	else if (m_recent->get_button()->get_active())
	{
		page = m_recent;
	}
	else
	{
		page = m_applications;
	}
	return page;
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::hide(bool lost_focus)
{
	// Save settings
	wm_settings->favorites.save();
	wm_settings->recent.save();

	// Scroll categories to top
	GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(m_sidebar);
	gtk_adjustment_set_value(adjustment, gtk_adjustment_get_lower(adjustment));

	// Hide command buttons to remove active border
	for (auto command : m_commands_button)
	{
		gtk_widget_set_visible(command, false);
	}

	// Hide window
	gtk_widget_hide(GTK_WIDGET(m_window));
	remember_category();

	// Switch back to default page
	show_default_page();

	// Inform plugin that window is hidden
	if (!lost_focus)
	{
		m_plugin->menu_hidden();
	}
}

//-----------------------------------------------------------------------------

static void wm_dump_walk(GtkWidget* w, int depth, FILE* f)
{
	gint mn = 0, nt = 0;
	gtk_widget_get_preferred_width(w, &mn, &nt);
	for (int i = 0; i < depth; ++i) fprintf(f, "  ");
	fprintf(f, "%s [%s] alloc=%d min=%d nat=%d\n", G_OBJECT_TYPE_NAME(w),
			gtk_widget_get_name(w) ? gtk_widget_get_name(w) : "-", gtk_widget_get_allocated_width(w), (int)mn, (int)nt);
	if (GTK_IS_CONTAINER(w))
	{
		GList* ch = gtk_container_get_children(GTK_CONTAINER(w));
		for (GList* l = ch; l; l = l->next)
		{
			wm_dump_walk(GTK_WIDGET(l->data), depth + 1, f);
		}
		g_list_free(ch);
	}
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::show(const Position position)
{
	m_position = position;

	// Handle switching view types
	m_search_results->update_view();
	m_favorites->update_view();
	m_recent->update_view();
	m_applications->update_view();

	// Handle showing tooltips
	if (wm_settings->launcher_show_tooltip)
	{
		m_search_results->get_view()->show_tooltips();
		m_favorites->get_view()->show_tooltips();
		m_recent->get_view()->show_tooltips();
		m_applications->get_view()->show_tooltips();
	}
	else
	{
		m_search_results->get_view()->hide_tooltips();
		m_favorites->get_view()->hide_tooltips();
		m_recent->get_view()->hide_tooltips();
		m_applications->get_view()->hide_tooltips();
	}
	m_profile->reset_tooltip();

	// Make sure commands are valid and visible
	for (auto command : wm_settings->command)
	{
		command->check();
	}

	// Make sure recent item count is within max
	m_recent->enforce_item_count();

	// Make sure recent button is only visible when tracked
	gtk_widget_set_visible(m_recent->get_button()->get_widget(), wm_settings->recent_items_max);

	// Make sure applications list is current; does nothing unless list has changed
	if (m_applications->load())
	{
		set_loaded();
	}
	else
	{
		m_plugin->set_loaded(false);
		gtk_stack_set_visible_child_name(m_window_stack, "load");
		gtk_spinner_start(m_window_load_spinner);
	}

	// Update default page
	reset_default_button();
	show_default_page();

	// Clear any previous selection
	m_favorites->reset_selection();
	m_recent->reset_selection();
	m_applications->reset_selection();

	// Make sure icon sizes are correct
	m_favorites->get_button()->reload_icon_size();
	m_recent->get_button()->reload_icon_size();
	m_applications->get_button()->reload_icon_size();

	m_applications->reload_category_icon_size();

	m_search_results->get_view()->reload_icon_size();
	m_favorites->get_view()->reload_icon_size();
	m_recent->get_view()->reload_icon_size();
	m_applications->get_view()->reload_icon_size();

	GdkMonitor* monitor_gdk = nullptr;
	if (position == PositionAtButton)
	{
		// Wait up to half a second for auto-hidden panels to be shown
		int parent_x = 0, parent_y = 0;
		clock_t end = clock() + (CLOCKS_PER_SEC / 2);
		GtkWidget* parent = m_plugin->get_button();
		GtkWindow* parent_window = GTK_WINDOW(gtk_widget_get_toplevel(parent));
		gtk_window_get_position(parent_window, &parent_x, &parent_y);
		while ((parent_x == -9999) && (parent_y == -9999) && (clock() < end))
		{
			while (gtk_events_pending())
			{
				gtk_main_iteration();
			}
			gtk_window_get_position(parent_window, &parent_x, &parent_y);
		}

		// Fetch position
		menu_position(&m_geometry.x, &m_geometry.y);
		monitor_gdk = gdk_display_get_monitor_at_window(gtk_widget_get_display(parent), gtk_widget_get_window(parent));
	}
	else
	{
		// Fetch cursor position
		GdkDisplay* display = gdk_display_get_default();
		GdkSeat* seat = gdk_display_get_default_seat(display);
		GdkDevice* device = gdk_seat_get_pointer(seat);
		gdk_device_get_position(device, nullptr, &m_geometry.x, &m_geometry.y);
		monitor_gdk = gdk_display_get_monitor_at_point(display, m_geometry.x, m_geometry.y);
	}

	// Resize window if necessary, and also prevent it from being larger than screen
#ifdef HAVE_GTK_LAYER_SHELL
	if (gtk_layer_is_supported())
	{
		gtk_layer_set_monitor(m_window, monitor_gdk);
	}
#endif
	gdk_monitor_get_geometry(monitor_gdk, &m_monitor);
	// workarea = the area without docks/panels; the menu must never cover the panel
	gdk_monitor_get_workarea(monitor_gdk, &m_workarea);
	m_max_bottom = m_workarea.y + m_workarea.height;
	tall_update_mode(position == PositionAtButton);
	const bool resized = set_size(wm_settings->menu_width, wm_settings->menu_height);

	// Center window if requested
	if (position == PositionAtCenter)
	{
		center_window();
	}

	// Move window
	move_window();

	// Relayout window if necessary
	const bool layout_ltr = gtk_widget_get_default_direction() != GTK_TEXT_DIR_RTL;
	if ((m_layout_ltr != layout_ltr)
			|| (m_layout_categories_horizontal != wm_settings->position_categories_horizontal)
			|| (m_layout_categories_alternate != wm_settings->position_categories_alternate)
			|| (m_layout_search_alternate != wm_settings->position_search_alternate)
			|| (m_layout_commands_alternate != wm_settings->position_commands_alternate)
			|| (m_layout_profile_alternate != wm_settings->position_profile_alternate)
			|| (m_profile_shape != wm_settings->profile_shape))
	{
		m_layout_ltr = layout_ltr;
		m_layout_categories_horizontal = wm_settings->position_categories_horizontal;
		m_layout_categories_alternate = wm_settings->position_categories_alternate;
		m_layout_search_alternate = wm_settings->position_search_alternate;
		m_layout_commands_alternate = wm_settings->position_commands_alternate;
		m_layout_profile_alternate = wm_settings->position_profile_alternate;
		m_profile->update_picture();
		m_profile_shape = wm_settings->profile_shape;
		update_layout();
		// the width was computed above with the old layout (e.g. categories
		// still horizontal) — recompute it from the new one
		sync_width();
	}

	// Without a profile, the title area holds only the command buttons;
	// drop it entirely when none of them is shown
	if ((m_profile_shape == Settings::ProfileHidden) && !m_layout_categories_alternate)
	{
		bool any_command = false;
		for (int i = 0; i < 9; ++i)
		{
			any_command |= wm_settings->command[i]->get_shown();
		}
		const bool visible = any_command && !m_layout_commands_alternate;
		if (visible != gtk_widget_get_visible(GTK_WIDGET(m_title_box)))
		{
			gtk_widget_set_visible(GTK_WIDGET(m_title_box), visible);
			sync_width();
		}
	}

	update_header_gaps();

	// Show window — but not before the layout reaches its final size:
	// realize while hidden so the size-allocate cycle completes before it is seen,
	// then pump events; the menu maps in one piece at its final size
	gtk_widget_realize(GTK_WIDGET(m_window));
	int pumped = 0;
	while (gtk_events_pending() && (pumped++ < 25))
	{
		gtk_main_iteration_do(FALSE);
	}

	// Column capacity from the final allocation — still hidden; any fix here is invisible
	m_favorites->sync_capacity();

	gtk_window_present(m_window);

	if (resized)
	{
		check_scrollbar_needed();
	}

	// Fetch actual window size (the menu, not the tall window around it)
	GtkRequisition size;
	if (m_tall)
	{
		gtk_widget_set_margin_top(m_frame, 0);
		gtk_widget_get_preferred_size(m_frame, &size, nullptr);
		gtk_widget_set_margin_top(m_frame, std::max(0, m_geometry.y - m_tall_top));
	}
	else
	{
		gtk_widget_get_preferred_size(GTK_WIDGET(m_window), &size, nullptr);
	}
	m_geometry.width = std::max(size.width, m_geometry.width);
	m_geometry.height = std::max(size.height, m_geometry.height);

	// Fetch position again to make sure window does not overlap panel
	if (position == PositionAtButton)
	{
		menu_position(&m_geometry.x, &m_geometry.y);
	}
	else if (position == PositionAtCenter)
	{
		center_window();
	}

	// Move window
	move_window();

	// Final guarantee: the menu bottom never enters the panel;
	// if the stored height does not fit, the whole window moves up (bottom on the panel)
	if ((m_max_bottom > 0) && (m_geometry.y + m_geometry.height > m_max_bottom))
	{
		m_geometry.y = m_max_bottom - m_geometry.height;
		move_window();
	}
}

//-----------------------------------------------------------------------------

int WhiskerMenu::Window::favorites_capacity_rows() const
{
	// Column capacity — computed when the content changes, without needing a show:
	// pitch = tallest cell content (icon or text) + row-spacing; all computed in memory
	// from pango and our own settings, not from the rendered widget.
	// Exception: while dragging, the real allocation is the measure (the drag is the height change).
	GtkWidget* v = m_favorites->get_view()->get_widget();
	const gint icon = wm_settings->launcher_icon_size.get_size();

	// text height is only needed for the pre-first-show estimate
	int text_h = 0;
	GtkTreeModel* model = (FavoritesPage::s_measured_pitch > 0) ? nullptr : gtk_icon_view_get_model(GTK_ICON_VIEW(v));
	if (model)
	{
		PangoLayout* layout = gtk_widget_create_pango_layout(v, nullptr);
		GtkTreeIter iter;
		if (gtk_tree_model_get_iter_first(model, &iter))
		{
			do
			{
				gchar* text = nullptr;
				gtk_tree_model_get(model, &iter, LauncherView::COLUMN_TEXT, &text, -1);
				if (text)
				{
					pango_layout_set_markup(layout, text, -1);
					int h = 0;
					pango_layout_get_pixel_size(layout, nullptr, &h);
					if (h > text_h)
					{
						text_h = h;
					}
					g_free(text);
				}
			} while (gtk_tree_model_iter_next(model, &iter));
		}
		g_object_unref(layout);
	}
	// pitch: a previous real measurement (sync_capacity) wins; the formula is used only
	// before the first show, when no cell has been drawn
	int pitch = FavoritesPage::s_measured_pitch;
	if (pitch <= 0)
	{
		gint rs = 0, item_pad = 0;
		g_object_get(G_OBJECT(v), "row-spacing", &rs, "item-padding", &item_pad, nullptr);
		pitch = std::max(icon, text_h) + 2 * item_pad + rs;
	}

	// Available height: the view allocation is valid only when it matches the
	// current requested geometry. When it does, the real chrome (win - view) is stored
	// so estimates for inconsistent frames (between request and allocation) give exactly
	// the same number — an estimate/allocation mismatch shifted the capacity by one and the
	// columns flipped 1<->2 (width 584<->596 and items jumping = flicker).
	// While dragging use requested geometry - stored chrome, not the allocation: the
	// allocation lags one frame behind and produces the transient state "last item moved
	// to the first column but still two columns" (one jump, not two)
	// frame = the menu itself (window minus the tall-mode margin)
	const int win_alloc = gtk_widget_get_allocated_height(m_frame ? m_frame : GTK_WIDGET(m_window));
	const int alloc_h = gtk_widget_get_allocated_height(v);
	int avail = 0;
	if ((win_alloc > 1) && (alloc_h > 1) && (win_alloc == m_geometry.height))
	{
		m_last_chrome = win_alloc - alloc_h;
	}
	if (m_last_chrome > 0)
	{
		avail = m_geometry.height - m_last_chrome;
	}
	else if (alloc_h > 1)
	{
		avail = alloc_h;
	}
	else
	{
		gint title_h = 0, search_h = 0;
		gtk_widget_get_preferred_height(GTK_WIDGET(m_title_box), nullptr, &title_h);
		gtk_widget_get_preferred_height(GTK_WIDGET(m_search_box), nullptr, &search_h);
		// Calibrated with a real probe: window frame + gaps = 10 (beyond the two vbox gaps)
		avail = wm_settings->menu_height - title_h - search_h - 12 - 10 - 6;
	}
	// GtkIconView's margin on both ends (default 6x2) reduces the usable height —
	// without it the capacity was one too high and a borderline scrollbar appeared
	gint iv_margin = 0;
	g_object_get(G_OBJECT(v), "margin", &iv_margin, nullptr);
	avail -= 2 * iv_margin;

	if ((avail <= 0) || (pitch <= 0))
	{
		return 1;
	}
	return std::max(1, avail / pitch);
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::resize(int delta_x, int delta_y, int delta_width, int delta_height)
{
	// Solve the target under three constraints: top limit (workarea), minimum content height,
	// bottom on the panel. y and h are solved together, otherwise the edges leave an empty gap or cross the panel.
	int target_y = m_geometry.y + delta_y;
	int target_h = m_geometry.height + delta_height;
	if (target_y < m_workarea.y)
	{
		target_h += target_y - m_workarea.y; // top of the screen: grow less, never detach the bottom
		target_y = m_workarea.y;
	}
	if (target_h < m_min_h)
	{
		// Height floor: the height stays at the minimum — y must stay put too
		// (otherwise the whole window slides and the bottom edge moves = flicker)
		target_h = m_min_h;
		target_y = m_geometry.y;
	}
	if ((m_max_bottom > 0) && (target_y + target_h > m_max_bottom))
	{
		target_y = m_max_bottom - target_h;
		if (target_y < m_workarea.y)
		{
			target_y = m_workarea.y;
			target_h = std::max(m_min_h, m_max_bottom - target_y);
		}
	}
	if (m_tall && (m_tall_bottom > 0) && (delta_height != 0))
	{
		// tall mode: the bottom edge is the window's own bottom and never
		// moves; only the top follows the pointer. Without this the min-height
		// floor kept y and lifted the bottom edge (2061 -> 2025 on :97).
		target_h = std::max(m_min_h, std::min(target_h, m_tall_bottom - m_tall_top));
		target_y = m_tall_bottom - target_h;
	}
	// Update the target; apply once per main-loop iteration (coalesce fast motions) —
	// applying re-entrantly inside the motion handler flickers on fast moves
	m_target_x = m_geometry.x + delta_x;
	m_target_y = target_y;
	m_target_w = m_geometry.width + delta_width;
	m_target_h = target_h;
	if (m_resizing)
	{
		if (m_size_idle_id == 0)
		{
			m_size_idle_id = g_idle_add(+[](gpointer data) -> gint
			{
				auto window = static_cast<Window*>(data);
				window->m_size_idle_id = 0;
				window->apply_target_size();
				return G_SOURCE_REMOVE;
			}, this);
		}
	}
	else
	{
		apply_target_size();
	}
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::schedule_page_width()
{
	// the visible page or its rows changed: recompute its column count and the
	// menu width once the models have settled
	if (m_page_width_id)
	{
		return;
	}
	m_page_width_id = g_idle_add(+[](gpointer data) -> gboolean
		{
			Window* window = static_cast<Window*>(data);
			window->m_page_width_id = 0;
			window->sync_width();
			return G_SOURCE_REMOVE;
		}, this);
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::sync_width()
{
	// After any favorites relayout, menu width = width of the new content (height untouched);
	// set_size takes the width from the widgets' natural size, so the input is only a cap —
	// do not let a stale menu_width keep the menu from shrinking
	set_size(10, m_geometry.height);
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::update_min_h()
{
	// During a live drag, refresh the minimum content height after each relayout — otherwise
	// the previous tall (one-column) layout clamps min_h and makes dragging down feel stuck
	gint min_h = 0;
	gtk_widget_get_preferred_height(GTK_WIDGET(m_vbox), &min_h, nullptr);
	m_min_h = std::max(10, static_cast<int>(min_h) + 10);
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::apply_target_size()
{
	m_geometry.x = m_target_x;
	m_geometry.y = m_target_y;
	if (set_size(m_target_w, m_target_h))
	{
		check_scrollbar_needed();
	}
	move_window();
	if (m_tall)
	{
		// the X window does not follow the menu in tall mode — nothing to sync
		return;
	}
	// Sync with reality: the WM may clamp the request (e.g. keep the menu above the
	// panel) — without this m_geometry lags behind and every later frame is computed
	// from a stale value = a 97px jump (seen with xtrace: a request for bottom=2160
	// was corrected by the WM to 2063)
	GdkWindow* win = gtk_widget_get_window(GTK_WIDGET(m_window));
	if (win)
	{
		gint ax = 0, ay = 0;
		gdk_window_get_position(win, &ax, &ay);
		if (ax > -9000 && ay > -9000)
		{
			if (m_geometry.x != ax || m_geometry.y != ay)
			{
				m_geometry.x = ax;
				m_geometry.y = ay;
			}
		}
	}
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::resize_start()
{
	m_resizing = true;
	pin_fixed_edge();
	// The favorites scrollbar policy is set in view_created and not touched again,
	// so its width never jumps during a drag.
	// Real minimum content height — from the content widget; not from the window, since
	// our set_size_request overrides its minimum and it always equals the current height
	gint min_h = 0;
	gtk_widget_get_preferred_height(GTK_WIDGET(m_vbox), &min_h, nullptr);
	m_min_h = std::max(10, static_cast<int>(min_h) + 10);
	set_child_has_focus();
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::resize_end()
{
	// If the last motion has not been applied yet, sync now so the stored size is right
	if (m_size_idle_id)
	{
		g_source_remove(m_size_idle_id);
		m_size_idle_id = 0;
		apply_target_size();
	}

	// Store new size
	wm_settings->menu_width = m_geometry.width;
	wm_settings->menu_height = m_geometry.height;

	// Do not move the window: an edge drag moved only that edge and y stayed —
	// calling get_menu_position here made the whole menu jump after release
	// tall mode: the X window is already final (constant during the drag)
	move_window();

	// Allow menu to hide
	m_resizing = false;
	m_child_has_focus = false;
	// One final relayout with the definite allocation (only a state fix, not
	// compensation — the real work was done during the drag)
	m_favorites->on_height_changed();
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::set_child_has_focus()
{
	m_child_has_focus = true;
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::set_categories(const std::vector<CategoryButton*>& categories)
{
	CategoryButton* last_button = m_applications->get_button();
	for (auto button : categories)
	{
		button->join_group(last_button);
		last_button = button;
		gtk_box_pack_start(m_category_buttons, button->get_widget(), false, false, 0);
		connect(button->get_widget(), "toggled",
			[this](GtkToggleButton*)
			{
				category_toggled();
			});
	}

	show_default_page();
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::set_items()
{
	m_search_results->set_menu_items();
	m_favorites->set_menu_items();
	m_recent->set_menu_items();
	// new items / layout (columns, fill direction): the width follows
	sync_width();

	// Handle switching to favorites are added
	connect(m_favorites->get_view()->get_model(), "row-inserted",
		[this](GtkTreeModel*, GtkTreePath*, GtkTreeIter*)
		{
			show_favorites();
		});
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::set_loaded()
{
	// Hide loading spinner
	gtk_spinner_stop(m_window_load_spinner);
	gtk_stack_set_visible_child_name(m_window_stack, "contents");

	// Focus search entry
	gtk_widget_grab_focus(GTK_WIDGET(m_search_entry));

	// Show panel button
	m_plugin->set_loaded(true);

	// Check in case of plugin reload
	check_scrollbar_needed();
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::unset_items()
{
	m_search_results->unset_menu_items();
	m_favorites->unset_menu_items();
	m_recent->unset_menu_items();
}

//-----------------------------------------------------------------------------

GtkWidget* WhiskerMenu::Window::get_active_category_button()
{
	GtkWidget* widget = m_default_button->get_widget();

	GList* children = gtk_container_get_children(GTK_CONTAINER(m_category_buttons));
	for (GList* li = children; li; li = li->next)
	{
		GtkToggleButton* button = GTK_TOGGLE_BUTTON(li->data);
		if (button && gtk_toggle_button_get_active(button))
		{
			widget = GTK_WIDGET(button);
			break;
		}
	}
	g_list_free(children);

	return widget;
}

//-----------------------------------------------------------------------------

gboolean WhiskerMenu::Window::on_key_press_event(GtkWidget* widget, GdkEventKey* key_event)
{
	if (key_event->keyval == GDK_KEY_Escape)
	{
		// Cancel resize
		if (m_resizing)
		{
			for (Resizer* resizer : m_resize)
			{
				if (resizer)
				{
					resizer->cancel();
				}
			}
			if (m_size_idle_id)
			{
				g_source_remove(m_size_idle_id);
				m_size_idle_id = 0;
			}
			set_size(wm_settings->menu_width, wm_settings->menu_height);
			resize_end();
		}
		// Hide if escape is pressed and there is no text in search entry
		else if (xfce_str_is_empty(gtk_entry_get_text(m_search_entry)))
		{
			hide();
		}
		// Clear search entry of text if escape is pressed
		else
		{
			gtk_entry_set_text(m_search_entry, "");
		}
		return GDK_EVENT_STOP;
	}

	Page* page = get_active_page();
	GtkWidget* view = page->get_view()->get_widget();
	GtkWidget* search = GTK_WIDGET(m_search_entry);

	switch (key_event->keyval)
	{
	case GDK_KEY_Left:
	case GDK_KEY_KP_Left:
	case GDK_KEY_Right:
	case GDK_KEY_KP_Right:
		// Allow keyboard navigation out of treeview
		if (GTK_IS_TREE_VIEW(view) && ((widget == view) || (gtk_window_get_focus(m_window) == view)))
		{
			gtk_widget_grab_focus(get_active_category_button());
		}
		// Allow keyboard navigation out of search into iconview
		else if (GTK_IS_ICON_VIEW(view) && ((widget == search) || (gtk_window_get_focus(m_window) == search)))
		{
			const auto length = gtk_entry_get_text_length(m_search_entry);
			const bool at_end = length && (gtk_editable_get_position(GTK_EDITABLE(m_search_entry)) == length);
			const bool move_next = (gtk_widget_get_default_direction() != GTK_TEXT_DIR_RTL)
					? (key_event->keyval == GDK_KEY_Right) : (key_event->keyval == GDK_KEY_Left);
			if (at_end && move_next)
			{
				gtk_widget_grab_focus(view);
			}
		}
		break;

	// Make up and down keys scroll current list of applications from search
	case GDK_KEY_Up:
	case GDK_KEY_KP_Up:
	case GDK_KEY_Down:
	case GDK_KEY_KP_Down:
	{
		// Determine if there is a selected item
		bool reset = page != m_search_results;
		if (reset)
		{
			GtkTreePath* path = page->get_view()->get_selected_path();
			if (path)
			{
				reset = false;
				gtk_tree_path_free(path);
			}
		}
		// Allow keyboard navigation out of search into view
		if ((widget == search) || (gtk_window_get_focus(m_window) == search))
		{
			gtk_widget_grab_focus(view);
		}
		// Only select first item if there is no selected item
		if ((gtk_window_get_focus(m_window) == view) && reset)
		{
			page->select_first();
			return GDK_EVENT_STOP;
		}
		break;
	}

	// Pass PageUp and PageDown keys to current view
	case GDK_KEY_Page_Up:
	case GDK_KEY_KP_Page_Up:
	case GDK_KEY_Page_Down:
	case GDK_KEY_KP_Page_Down:
		if ((widget == search) || (gtk_window_get_focus(m_window) == search))
		{
			gtk_widget_grab_focus(view);
		}
		break;

	default:
		break;
	}

	return GDK_EVENT_PROPAGATE;
}

//-----------------------------------------------------------------------------

gboolean WhiskerMenu::Window::on_key_press_event_after(GtkWidget* widget, GdkEventKey* key_event)
{
	// Pass unhandled key presses to search entry
	GtkWidget* search_entry = GTK_WIDGET(m_search_entry);
	if ((widget != search_entry) && (gtk_window_get_focus(m_window) != search_entry))
	{
		if (key_event->is_modifier)
		{
			return GDK_EVENT_PROPAGATE;
		}
		gtk_entry_grab_focus_without_selecting(m_search_entry);
		gtk_window_propagate_key_event(m_window, key_event);
		return GDK_EVENT_STOP;
	}
	return GDK_EVENT_PROPAGATE;
}

//-----------------------------------------------------------------------------

gboolean WhiskerMenu::Window::on_map_event()
{
	gtk_window_set_keep_above(m_window, true);

	// after mapping, i.e. after the window manager has reparented us
	pin_fixed_edge();

	return GDK_EVENT_PROPAGATE;
}

//-----------------------------------------------------------------------------

bool WhiskerMenu::Window::tall_supported() const
{
	if (!m_supports_alpha || !m_frame || g_getenv("VIBRIS_NO_TALL"))
	{
		return false;
	}
	GdkScreen* screen = gtk_widget_get_screen(GTK_WIDGET(m_window));
	if (!gdk_screen_is_composited(screen))
	{
		return false;
	}
	// only for a menu above a bottom panel: decided from the panel button,
	// not from the menu (whose height is not known yet on the first show)
	GtkWidget* button = m_plugin->get_button();
	GdkWindow* bw = button ? gtk_widget_get_window(button) : nullptr;
	if (!bw)
	{
		return false;
	}
	int bx = 0, by = 0;
	gdk_window_get_origin(bw, &bx, &by);
	if (!gtk_widget_get_has_window(button))
	{
		GtkAllocation alloc;
		gtk_widget_get_allocation(button, &alloc);
		by += alloc.y;
	}
	return by >= (m_monitor.y + (m_monitor.height / 2));
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::menu_position(int* x, int* y)
{
	// xfce_panel_plugin_position_widget() places the window by its size
	// request; in tall mode that is the tall window, so let it see the menu
	if (!m_tall)
	{
		m_plugin->get_menu_position(x, y);
		return;
	}
	const int margin = gtk_widget_get_margin_top(m_frame);
	gtk_widget_set_margin_top(m_frame, 0);
	gtk_widget_set_size_request(GTK_WIDGET(m_window), m_geometry.width, m_geometry.height);
	m_plugin->get_menu_position(x, y);
	gtk_widget_set_margin_top(m_frame, margin);
	gtk_widget_set_size_request(GTK_WIDGET(m_window), m_tall_w > 0 ? m_tall_w : m_geometry.width,
			m_tall_bottom > 0 ? m_tall_bottom - m_tall_top : m_geometry.height);
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::tall_apply()
{
	// While the menu is shown (composited, above a bottom panel) the X window
	// spans from the top of the workarea to the menu's bottom edge. The menu
	// is drawn in its bottom part (frame margin-top); everything above is
	// transparent and, through the input shape, not even part of the window
	// for the pointer — clicks there reach the windows below (focus-out
	// still closes the menu). A top-edge drag changes only the margin:
	// no X configure at press, during the drag or at release.
	const int bottom = m_geometry.y + m_geometry.height;
	const int tall_h = bottom - m_tall_top;
	const int margin = std::max(0, m_geometry.y - m_tall_top);
	gtk_widget_set_margin_top(m_frame, margin);

	GdkWindow* win = gtk_widget_get_window(GTK_WIDGET(m_window));
	if ((m_tall_w != m_geometry.width) || (m_tall_bottom != bottom) || (m_tall_x != m_geometry.x))
	{
		m_tall_w = m_geometry.width;
		m_tall_bottom = bottom;
		m_tall_x = m_geometry.x;
		// Only GTK's own API here: direct gdk configures raced with GTK's
		// (stale size hints, remembered size) and left the window misplaced.
		// gtk_window_resize also stops GTK from re-sending the size it last
		// saw in a ConfigureNotify (the width that would not go back to one
		// column). gtk_window_move takes the top-left corner even with a
		// gravity set (the gravity only tells the WM how to treat its frame);
		// GTK sends move+resize as one configure.
		gtk_widget_set_size_request(GTK_WIDGET(m_window), m_geometry.width, tall_h);
		gtk_window_resize(m_window, m_geometry.width, tall_h);
		gtk_window_move(m_window, m_geometry.x, m_tall_top);
		// During show() several provisional geometries are requested before
		// and right after the first map, and xfwm4 placed the window from one
		// of them. Once GTK's layout pass has run (size hints current), check
		// the real X geometry once and correct it with a single configure.
		if (!m_tall_settle_id)
		{
			m_tall_settle_id = g_idle_add_full(G_PRIORITY_LOW, +[](gpointer data) -> gboolean
			{
				auto window = static_cast<Window*>(data);
				window->m_tall_settle_id = 0;
				window->tall_settle();
				return G_SOURCE_REMOVE;
			}, this, nullptr);
		}
	}
	if (win)
	{
		const cairo_rectangle_int_t menu_rect = {0, margin, m_geometry.width, m_geometry.height};
		cairo_region_t* region = cairo_region_create_rectangle(&menu_rect);
		gdk_window_input_shape_combine_region(win, region, 0, 0);
		cairo_region_destroy(region);
	}
	gtk_widget_queue_draw(GTK_WIDGET(m_window));
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::tall_settle()
{
	GdkWindow* win = gtk_widget_get_window(GTK_WIDGET(m_window));
	if (!m_tall || !win || !gtk_widget_get_mapped(GTK_WIDGET(m_window)) || (m_tall_bottom < 0))
	{
		return;
	}
	const int tall_h = m_tall_bottom - m_tall_top;
	int x = 0, y = 0;
	gdk_window_get_root_origin(win, &x, &y);
	if ((x != m_tall_x) || (y != m_tall_top) || (gdk_window_get_width(win) != m_tall_w)
			|| (gdk_window_get_height(win) != tall_h))
	{
		gdk_window_move_resize(win, m_tall_x, m_tall_top, m_tall_w, tall_h);
	}
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::tall_update_mode(bool at_button)
{
	// decided on every show (compositor / panel position may have changed)
	const bool tall = at_button && tall_supported();
	if (tall == m_tall)
	{
		if (tall)
		{
			m_tall_top = m_workarea.y;
		}
		return;
	}
	m_tall = tall;
	m_tall_w = m_tall_bottom = m_tall_x = -1;
	m_tall_top = m_workarea.y;
	// tall window: its TOP edge is the fixed one (workarea top) and it is
	// only ever resized in width — NorthWest, or xfwm4 interprets every
	// requested position relative to the bottom edge (window landed at y=99
	// on :97). Normal window above a bottom panel: SouthWest as before.
	gtk_window_set_gravity(m_window, tall ? GDK_GRAVITY_NORTH_WEST : GDK_GRAVITY_SOUTH_WEST);
	if (!tall)
	{
		gtk_widget_set_margin_top(m_frame, 0);
		GdkWindow* win = gtk_widget_get_window(GTK_WIDGET(m_window));
		if (win)
		{
			gdk_window_input_shape_combine_region(win, nullptr, 0, 0);
		}
		gtk_widget_set_size_request(GTK_WIDGET(m_window), m_geometry.width, m_geometry.height);
		gtk_window_resize(m_window, m_geometry.width, m_geometry.height);
	}
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::pin_fixed_edge()
{
	// Root cause of the bottom-edge jitter while dragging the top edge.
	//
	// gtk_window_set_gravity() only sets the WM_NORMAL_HINTS win_gravity hint
	// (where the WM places the frame). The X *server* attributes were left at
	// GDK's defaults (gdkwindow-x11.c, create_window_impl):
	//   bit_gravity = NorthWest  -> on resize the server keeps the old pixels
	//                               glued to the TOP-left: when the window
	//                               grows upward the old bottom part is
	//                               shown shifted up until GTK repaints;
	//   win_gravity = NorthWest  -> when xfwm4 resizes its frame (our parent)
	//                               the client stays glued to the frame's top:
	//                               frame grows upward, client moves up with it,
	//                               a gap opens above the panel, then the client
	//                               resize arrives and closes it;
	//   background  = black      -> newly exposed area is painted black first.
	// None of that can be fixed by ordering GTK calls. The fix is to tell the
	// server which edge is fixed, and to not paint a background at all:
	// exposed pixels stay as they were until GTK draws the real content.
	// Result: the fixed edge never moves on screen; only the dragged edge
	// changes.
#ifdef GDK_WINDOWING_X11
	GdkWindow* win = gtk_widget_get_window(GTK_WIDGET(m_window));
	if (!win || !GDK_IS_X11_WINDOW(win))
	{
		return;
	}

	// menu above a bottom panel -> bottom edge fixed; below a top panel -> top
	const int centre_y = m_geometry.y + (m_geometry.height / 2);
	const bool bottom_fixed = centre_y >= (m_monitor.y + (m_monitor.height / 2));
	const int gravity = bottom_fixed ? SouthWestGravity : NorthWestGravity;

	// Only bit_gravity. win_gravity on the client must stay NorthWest: xfwm4
	// already keeps the frame's bottom fixed (WM_NORMAL_HINTS gravity) and
	// places the client at the frame's top-left. With win_gravity SouthWest
	// the server shifted the client inside the shrinking frame a second time,
	// so the whole menu ran upward ~2x the drag distance (tested on :97).
	XSetWindowAttributes attributes;
	attributes.bit_gravity = gravity;
	attributes.win_gravity = NorthWestGravity;
	XChangeWindowAttributes(GDK_WINDOW_XDISPLAY(win), GDK_WINDOW_XID(win),
			CWBitGravity | CWWinGravity, &attributes);

	// NULL pattern = X background None; GDK keeps it that way when it
	// temporarily unsets/resets backgrounds (tmp_reset_bg)
	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	gdk_window_set_background_pattern(win, nullptr);
	G_GNUC_END_IGNORE_DEPRECATIONS
#endif
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::on_state_flags_changed(GtkWidget* widget)
{
	// Refocus and raise window if visible
	if (gtk_widget_get_visible(widget))
	{
		gtk_window_present(m_window);
	}
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::on_screen_changed(GtkWidget* widget)
{
	GdkScreen* screen = gtk_widget_get_screen(widget);
	GdkVisual* visual = gdk_screen_get_rgba_visual(screen);
	// RGBA even at 100% opacity: tall mode needs transparent pixels above the
	// menu while dragging (on_draw_event paints with alpha 1.0 otherwise)
	if (!visual)
	{
		visual = gdk_screen_get_system_visual(screen);
		m_supports_alpha = false;
	}
	else
	{
		m_supports_alpha = true;
	}
	gtk_widget_set_visual(widget, visual);
}

//-----------------------------------------------------------------------------

gboolean WhiskerMenu::Window::on_draw_event(GtkWidget* widget, cairo_t* cr)
{
	if (!gtk_widget_get_realized(widget))
	{
		gtk_widget_realize(widget);
	}

	GtkStyleContext* context = gtk_widget_get_style_context(widget);
	const double width = gtk_widget_get_allocated_width(widget);
	const double height = gtk_widget_get_allocated_height(widget);

	GdkScreen* screen = gtk_widget_get_screen(widget);
	const bool enabled = gdk_screen_is_composited(screen);

	if (m_tall && enabled && m_supports_alpha)
	{
		// transparent above the menu; menu background only in its own rect
		cairo_save(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
		cairo_paint(cr);
		cairo_restore(cr);
		const double top = gtk_widget_get_margin_top(m_frame);
		cairo_push_group(cr);
		gtk_render_background(context, cr, 0.0, top, width, height - top);
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, wm_settings->menu_opacity / 100.0);
	}
	else if (enabled && m_supports_alpha)
	{
		cairo_surface_t* background = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
		cairo_t* cr_background = cairo_create(background);
		cairo_set_operator(cr_background, CAIRO_OPERATOR_SOURCE);
		gtk_render_background(context, cr_background, 0.0, 0.0, width, height);
		cairo_destroy(cr_background);

		cairo_set_source_surface(cr, background, 0.0, 0.0);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_paint_with_alpha(cr, wm_settings->menu_opacity / 100.0);

		cairo_surface_destroy(background);
	}
	else
	{
		gtk_render_background(context, cr, 0.0, 0.0, width, height);
	}

	return GDK_EVENT_PROPAGATE;
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::check_scrollbar_needed()
{
	// Find height of sidebar and buttons
	int buttons_height = 0;
	gtk_widget_get_preferred_height(GTK_WIDGET(m_category_buttons), nullptr, &buttons_height);

	int sidebar_height = 0;
	gtk_widget_get_preferred_height(GTK_WIDGET(m_sidebar), nullptr, &sidebar_height);

	// Always show scrollbar if sidebar is shorter than buttons
	if (sidebar_height >= buttons_height)
	{
		gtk_scrolled_window_set_policy(m_sidebar, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	}
	else
	{
		gtk_scrolled_window_set_policy(m_sidebar, GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	}
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::favorites_toggled()
{
	m_favorites->reset_selection();
	gtk_stack_set_visible_child_name(m_panels_stack, "favorites");
	schedule_page_width();
	gtk_widget_grab_focus(GTK_WIDGET(m_search_entry));
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::recent_toggled()
{
	m_recent->reset_selection();
	gtk_stack_set_visible_child_name(m_panels_stack, "recent");
	schedule_page_width();
	gtk_widget_grab_focus(GTK_WIDGET(m_search_entry));
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::category_toggled()
{
	m_applications->reset_selection();
	gtk_stack_set_visible_child_name(m_panels_stack, "applications");
	schedule_page_width();
	gtk_widget_grab_focus(GTK_WIDGET(m_search_entry));
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::center_window()
{
	m_geometry.x = (m_monitor.width - m_geometry.width) / 2;
	m_geometry.y = (m_monitor.height - m_geometry.height) / 2;
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::move_window()
{
	// Prevent window from leaving screen
	m_geometry.x = CLAMP(m_geometry.x, m_monitor.x, m_monitor.x + m_monitor.width - m_geometry.width);
	m_geometry.y = CLAMP(m_geometry.y, m_monitor.y, m_monitor.y + m_monitor.height - m_geometry.height);

	// Move window
#ifdef HAVE_GTK_LAYER_SHELL
	if (gtk_layer_is_supported())
	{
		gtk_layer_set_margin(m_window, GTK_LAYER_SHELL_EDGE_LEFT, m_geometry.x - m_monitor.x);
		gtk_layer_set_margin(m_window, GTK_LAYER_SHELL_EDGE_TOP, m_geometry.y - m_monitor.y);
	}
	else
#endif
	{
		// xtrace evidence: gtk_window_move and gtk_window_resize each send XMoveWindow
		// and XResizeWindow separately as soon as the window is mapped —
		// so the whole window is first seen moved, then the resize arrives and the bottom
		// edge jumps and returns (the bottom flicker). Fix: one merged request with all
		// four values; GTK's internal state converges from the next
		// ConfigureNotify
		GdkWindow* win = gtk_widget_get_window(GTK_WIDGET(m_window));
		if (m_tall)
		{
			tall_apply();
		}
		else if (m_resizing && win)
		{
			// While dragging: resize only — south-west gravity adjusts y in the same request
			// and the bottom edge stays put pixel for pixel
			gdk_window_resize(win, m_geometry.width, m_geometry.height);
		}
		else if (win)
		{
			gdk_window_move_resize(win, m_geometry.x, m_geometry.y,
					m_geometry.width, m_geometry.height);
		}
		else
		{
			gtk_window_resize(m_window, m_geometry.width, m_geometry.height);
			gtk_window_move(m_window, m_geometry.x, m_geometry.y);
		}
	}
}

//-----------------------------------------------------------------------------

bool WhiskerMenu::Window::set_size(int width, int height)
{
	bool resized = false;
	width = CLAMP(width, 10, m_monitor.width);
	height = CLAMP(height, 10, m_monitor.height);
	// Width from the content — always, even while dragging (all the work happens
	// during the drag; release does nothing). It is cheap: the item width is
	// cached on content change and the formula is just arithmetic.
	{
		Page* page = get_active_page();
		if (!page)
		{
			page = m_favorites;
		}
		gint lav_mn = 0, lav_nt = 0, cat_mn = 0, cat_nt = 0;
		if (page == m_favorites)
		{
			// Exact formula: number of used columns x width
			// of the widest item + margins + gaps between columns. The configured column count
			// is a maximum; an empty column takes no space. item_width is computed once per
			// content change and stored in s_item_width.
			// Exactly what GtkIconView lays out (gtk_icon_view_layout):
			//   2*margin + cols*(item_width + 2*item_padding) + (cols-1)*column_spacing
			// item_width is the stored measurement (taken only when the
			// favorites content changed); cols = columns that hold items.
			// Plus the scrollbar's reserved width when it does not overlay.
			GtkWidget* v = m_favorites->get_view()->get_widget();
			const int iw = LauncherIconView::s_item_width;
			if (iw > 0)
			{
				gint margin = 0, pad = 0, col_sp = 0;
				g_object_get(G_OBJECT(v), "margin", &margin, "item-padding", &pad, "column-spacing", &col_sp, nullptr);
				const int used = std::max(1, m_favorites->get_used_columns());
				lav_nt = 2 * margin + used * (iw + 2 * pad) + (used - 1) * col_sp;
			}
			else
			{
				gtk_widget_get_preferred_width(v, &lav_mn, &lav_nt);
			}
			lav_nt += m_favorites->get_scrollbar_reserve();
		}
		else
		{
			// Other pages use the favorites column setting as a maximum: only
			// as many columns as their rows need at the current menu height
			GtkWidget* v = page->get_view()->get_widget();
			if (GTK_IS_ICON_VIEW(v))
			{
				const int max_cols = (wm_settings->favorites_columns > 0) ? wm_settings->favorites_columns : 2;
				const int rows = std::max(1, favorites_capacity_rows());
				// the real model: the displayed one also holds the empty cells
				GtkTreeModel* model = page->get_view()->get_model();
				const int n = model ? gtk_tree_model_iter_n_children(model, nullptr) : 0;
				const int cols = CLAMP((n + rows - 1) / rows, 1, max_cols);
				if (auto icon_view = dynamic_cast<LauncherIconView*>(page->get_view()))
				{
					icon_view->set_column_major(cols, rows);
				}
			}
			gtk_widget_get_preferred_width(v, &lav_mn, &lav_nt);
		}
		gtk_widget_get_preferred_width(GTK_WIDGET(m_category_buttons), &cat_mn, &cat_nt);
		int content_w = (int)lav_nt + (int)cat_nt + 26 + 12;
		if (content_w > 300)
		{
			width = content_w;
		}
	}
	if ((m_geometry.width != width) || (m_geometry.height != height))
	{
		m_geometry.width = width;
		m_geometry.height = height;
		// Always set_size_request — it is the toplevel minimum for GTK; if it is not updated
		// during a drag, GTK clamps every gtk_window_resize to the stale minimum
		// and the height does not change while y moved (the whole menu slid).
		// GTK merges all of this into a single ConfigureRequest.
		if (m_tall)
		{
			tall_apply();
		}
		else
		{
			gtk_widget_set_size_request(GTK_WIDGET(m_window), m_geometry.width, m_geometry.height);
		}
		resized = true;
	}
	return resized;
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::reset_default_button()
{
	switch (wm_settings->default_category)
	{
	case Settings::CategoryRecent:
		m_default_button = m_recent->get_button();
		gtk_box_reorder_child(m_category_buttons, m_recent->get_button()->get_widget(), 0);
		gtk_box_reorder_child(m_category_buttons, m_favorites->get_button()->get_widget(), 1);
		gtk_box_reorder_child(m_category_buttons, m_applications->get_button()->get_widget(), 2);
		break;

	case Settings::CategoryAll:
		m_default_button = m_applications->get_button();
		gtk_box_reorder_child(m_category_buttons, m_applications->get_button()->get_widget(), 0);
		gtk_box_reorder_child(m_category_buttons, m_favorites->get_button()->get_widget(), 1);
		gtk_box_reorder_child(m_category_buttons, m_recent->get_button()->get_widget(), 2);
		break;

	default:
		m_default_button = m_favorites->get_button();
		gtk_box_reorder_child(m_category_buttons, m_favorites->get_button()->get_widget(), 0);
		gtk_box_reorder_child(m_category_buttons, m_recent->get_button()->get_widget(), 1);
		gtk_box_reorder_child(m_category_buttons, m_applications->get_button()->get_widget(), 2);
		break;
	}
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::show_favorites()
{
	// Switch to favorites panel
	m_favorites->get_button()->set_active(true);

	// Clear search entry
	gtk_entry_set_text(m_search_entry, "");
	gtk_widget_grab_focus(GTK_WIDGET(m_search_entry));
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::remember_category()
{
	if (m_last_category)
	{
		g_object_remove_weak_pointer(G_OBJECT(m_last_category), reinterpret_cast<gpointer*>(&m_last_category));
		m_last_category = nullptr;
	}
	GList* children = gtk_container_get_children(GTK_CONTAINER(m_category_buttons));
	for (GList* i = children; i; i = i->next)
	{
		GtkWidget* button = GTK_WIDGET(i->data);
		if (GTK_IS_TOGGLE_BUTTON(button) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
		{
			m_last_category = button;
			g_object_add_weak_pointer(G_OBJECT(m_last_category), reinterpret_cast<gpointer*>(&m_last_category));
			break;
		}
	}
	g_list_free(children);
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::show_default_page()
{
	// Switch to the default panel, or back to the last one used
	if ((wm_settings->default_category == Settings::CategoryLast)
			&& m_last_category
			&& (gtk_widget_get_parent(m_last_category) == GTK_WIDGET(m_category_buttons)))
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_last_category), true);
	}
	else
	{
		m_default_button->set_active(true);
	}

	// Clear search entry
	gtk_entry_set_text(m_search_entry, "");
	gtk_widget_grab_focus(GTK_WIDGET(m_search_entry));
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::search()
{
	// Fetch search string
	const gchar* text = gtk_entry_get_text(m_search_entry);
	if (xfce_str_is_empty(text))
	{
		text = nullptr;
	}

	if (text)
	{
		// Show search results
		gtk_stack_set_visible_child_name(m_contents_stack, "search");
	}
	else
	{
		// Show active panel
		gtk_stack_set_visible_child_name(m_contents_stack, "contents");
	}

	// Apply filter
	m_search_results->set_filter(text);
	schedule_page_width();
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::update_layout()
{
	// Set vertical position of commands
	g_object_ref(m_commands_box);
	gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(GTK_WIDGET(m_commands_box))), GTK_WIDGET(m_commands_box));
	if (m_layout_commands_alternate)
	{
		gtk_box_pack_start(m_search_box, GTK_WIDGET(m_commands_box), false, false, 0);

		if (!m_layout_categories_horizontal)
		{
			if (m_layout_ltr == m_layout_categories_alternate)
			{
				gtk_box_reorder_child(m_search_box, GTK_WIDGET(m_commands_box), 0);
				gtk_box_reorder_child(m_search_box, GTK_WIDGET(m_search_entry), 1);
			}
		}
		else
		{
			if (!m_layout_ltr)
			{
				gtk_box_reorder_child(m_search_box, GTK_WIDGET(m_commands_box), 0);
				gtk_box_reorder_child(m_search_box, GTK_WIDGET(m_search_entry), 1);
			}
		}
	}
	else
	{
		gtk_box_pack_start(m_title_box, GTK_WIDGET(m_commands_box), false, false, 0);
	}
	g_object_unref(m_commands_box);

	// Set horizontal position of categories
	g_object_ref(m_category_buttons);
	if (m_layout_categories_horizontal)
	{
		if (gtk_orientable_get_orientation(GTK_ORIENTABLE(m_category_buttons)) == GTK_ORIENTATION_VERTICAL)
		{
			gtk_orientable_set_orientation(GTK_ORIENTABLE(m_category_buttons), GTK_ORIENTATION_HORIZONTAL);
			gtk_container_remove(GTK_CONTAINER(m_sidebar), GTK_WIDGET(m_category_buttons));
			gtk_widget_set_visible(GTK_WIDGET(m_sidebar), false);
			gtk_widget_set_visible(GTK_WIDGET(m_categories_box), true);
			gtk_box_set_center_widget(m_categories_box, GTK_WIDGET(m_category_buttons));
		}
	}
	else
	{
		if (gtk_orientable_get_orientation(GTK_ORIENTABLE(m_category_buttons)) == GTK_ORIENTATION_HORIZONTAL)
		{
			gtk_orientable_set_orientation(GTK_ORIENTABLE(m_category_buttons), GTK_ORIENTATION_VERTICAL);
			gtk_container_remove(GTK_CONTAINER(m_categories_box), GTK_WIDGET(m_category_buttons));
			gtk_widget_set_visible(GTK_WIDGET(m_categories_box), false);
			gtk_widget_set_visible(GTK_WIDGET(m_sidebar), true);
			gtk_container_add(GTK_CONTAINER(m_sidebar), GTK_WIDGET(m_category_buttons));
		}
	}
	g_object_unref(m_category_buttons);

	// Handle showing username and profile
	if (m_profile_shape != Settings::ProfileHidden)
	{
		gtk_widget_set_visible(m_profile->get_picture(), true);
		gtk_widget_set_visible(m_profile->get_username(), true);
		gtk_widget_set_visible(GTK_WIDGET(m_title_box), true);
	}
	else
	{
		gtk_widget_set_visible(m_profile->get_picture(), false);
		gtk_widget_set_visible(m_profile->get_username(), false);
		gtk_widget_set_visible(GTK_WIDGET(m_title_box), !m_layout_categories_alternate);
	}

	// Arrange horizontal order of profile picture, username, and commands
	if (m_layout_ltr && m_layout_commands_alternate)
	{
		gtk_widget_set_halign(m_profile->get_username(), GTK_ALIGN_START);

		gtk_box_reorder_child(m_title_box, m_profile->get_picture(), 0);
		gtk_box_reorder_child(m_title_box, m_profile->get_username(), 1);

		for (int i = 0; i < 9; ++i)
		{
			gtk_box_reorder_child(m_commands_box, m_commands_button[i], i);
		}
	}
	else if (m_layout_commands_alternate)
	{
		gtk_widget_set_halign(m_profile->get_username(), GTK_ALIGN_END);

		gtk_box_reorder_child(m_title_box, m_profile->get_picture(), 1);
		gtk_box_reorder_child(m_title_box, m_profile->get_username(), 0);

		for (int i = 0; i < 9; ++i)
		{
			gtk_box_reorder_child(m_commands_box, m_commands_button[i], 8 - i);
		}
	}
	else if (m_layout_ltr)
	{
		gtk_widget_set_halign(m_profile->get_username(), GTK_ALIGN_START);

		gtk_box_reorder_child(m_title_box, m_profile->get_picture(), 0);
		gtk_box_reorder_child(m_title_box, m_profile->get_username(), 1);
		gtk_box_reorder_child(m_title_box, GTK_WIDGET(m_commands_box), 2);

		for (int i = 0; i < 9; ++i)
		{
			gtk_box_reorder_child(m_commands_box, m_commands_button[i], i);
		}
	}
	else
	{
		gtk_widget_set_halign(m_profile->get_username(), GTK_ALIGN_END);

		gtk_box_reorder_child(m_title_box, m_profile->get_picture(), 2);
		gtk_box_reorder_child(m_title_box, m_profile->get_username(), 1);
		gtk_box_reorder_child(m_title_box, GTK_WIDGET(m_commands_box), 0);

		for (int i = 0; i < 9; ++i)
		{
			gtk_box_reorder_child(m_commands_box, m_commands_button[i], 8 - i);
		}
	}

	// Arrange horizontal order of applications and sidebar
	g_object_ref(m_categories_box);
	g_object_ref(m_panels_stack);
	g_object_ref(m_sidebar);

	GtkStyleContext* context = gtk_widget_get_style_context(GTK_WIDGET(m_category_buttons));
	if (gtk_style_context_has_class(context, "left"))
	{
		gtk_style_context_remove_class(context, "left");
	}
	else if (gtk_style_context_has_class(context, "right"))
	{
		gtk_style_context_remove_class(context, "right");
	}
	else if (gtk_style_context_has_class(context, "top"))
	{
		gtk_style_context_remove_class(context, "top");
	}
	else if (gtk_style_context_has_class(context, "bottom"))
	{
		gtk_style_context_remove_class(context, "bottom");
	}

	gtk_grid_remove_row(m_contents_box, 1);
	gtk_grid_remove_row(m_contents_box, 0);
	if (m_layout_categories_horizontal)
	{
		gtk_grid_set_column_spacing(m_contents_box, 0);
		gtk_grid_set_row_spacing(m_contents_box, 6);

		gtk_style_context_add_class(context, m_layout_categories_alternate ? "bottom" : "top");
	}
	else
	{
		gtk_grid_set_column_spacing(m_contents_box, 6);
		gtk_grid_set_row_spacing(m_contents_box, 0);

		gtk_style_context_add_class(context, (m_layout_ltr == m_layout_categories_alternate) ? "left" : "right");
	}

	if (m_layout_ltr != m_layout_categories_alternate)
	{
		gtk_grid_attach(m_contents_box, GTK_WIDGET(m_panels_stack), 0, 0, 1, 1);
		gtk_grid_attach(m_contents_box, GTK_WIDGET(m_sidebar), 1, 0, 1, 1);

		gtk_box_reorder_child(m_commands_box, m_commands_spacer, 0);
	}
	else
	{
		gtk_grid_attach(m_contents_box, GTK_WIDGET(m_sidebar), 0, 0, 1, 1);
		gtk_grid_attach(m_contents_box, GTK_WIDGET(m_panels_stack), 1, 0, 1, 1);

		gtk_box_reorder_child(m_commands_box, m_commands_spacer, 9);
	}

	if (!m_layout_categories_alternate)
	{
		gtk_grid_insert_row(m_contents_box, 0);
		gtk_grid_attach(m_contents_box, GTK_WIDGET(m_categories_box), 0, 0, 2, 1);
	}
	else
	{
		gtk_grid_attach(m_contents_box, GTK_WIDGET(m_categories_box), 0, 1, 2, 1);
	}

	g_object_unref(m_sidebar);
	g_object_unref(m_panels_stack);
	g_object_unref(m_categories_box);

	// Arrange vertical order of header, applications, and search
	if (m_layout_search_alternate && m_layout_profile_alternate)
	{
		gtk_box_reorder_child(m_vbox, GTK_WIDGET(m_contents_stack), 0);
		gtk_box_reorder_child(m_vbox, GTK_WIDGET(m_search_box), 1);
		gtk_box_reorder_child(m_vbox, GTK_WIDGET(m_title_box), 2);
	}
	else if (m_layout_profile_alternate)
	{
		gtk_box_reorder_child(m_vbox, GTK_WIDGET(m_search_box), 0);
		gtk_box_reorder_child(m_vbox, GTK_WIDGET(m_contents_stack), 1);
		gtk_box_reorder_child(m_vbox, GTK_WIDGET(m_title_box), 2);
	}
	else if (m_layout_search_alternate)
	{
		gtk_box_reorder_child(m_vbox, GTK_WIDGET(m_title_box), 0);
		gtk_box_reorder_child(m_vbox, GTK_WIDGET(m_contents_stack), 1);
		gtk_box_reorder_child(m_vbox, GTK_WIDGET(m_search_box), 2);
	}
	else
	{
		gtk_box_reorder_child(m_vbox, GTK_WIDGET(m_title_box), 0);
		gtk_box_reorder_child(m_vbox, GTK_WIDGET(m_search_box), 1);
		gtk_box_reorder_child(m_vbox, GTK_WIDGET(m_contents_stack), 2);
	}

	// Handle size group to category buttons
	const bool category_show_name = wm_settings->category_show_name && !wm_settings->position_categories_horizontal;
	if (!m_sidebar_size_group && m_layout_commands_alternate && category_show_name)
	{
		m_sidebar_size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
		gtk_size_group_add_widget(m_sidebar_size_group, GTK_WIDGET(m_sidebar));
		gtk_size_group_add_widget(m_sidebar_size_group, GTK_WIDGET(m_commands_box));
	}
	else if (m_sidebar_size_group && (!m_layout_commands_alternate || !category_show_name))
	{
		gtk_size_group_remove_widget(m_sidebar_size_group, GTK_WIDGET(m_sidebar));
		gtk_size_group_remove_widget(m_sidebar_size_group, GTK_WIDGET(m_commands_box));
		g_object_unref(m_sidebar_size_group);
		m_sidebar_size_group = nullptr;
	}
}

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

namespace
{

// Empty space between the edge of a command button and the visible pixels
// of its icon: the button's CSS border plus the transparent border that the
// icon theme draws around the glyph. Read from the icon itself, so every
// icon and theme ends up with the same visible gap.
struct Insets
{
	int left = 0, top = 0, right = 0;
};

Insets command_insets(GtkWidget* button)
{
	Insets insets;
	GtkStyleContext* context = gtk_widget_get_style_context(button);
	GtkBorder border = {0, 0, 0, 0};
	gtk_style_context_get_border(context, gtk_style_context_get_state(context), &border);
	insets.left = border.left;
	insets.top = border.top;
	insets.right = border.right;

	GtkWidget* image = gtk_bin_get_child(GTK_BIN(button));
	if (!GTK_IS_IMAGE(image) || (gtk_image_get_storage_type(GTK_IMAGE(image)) != GTK_IMAGE_ICON_NAME))
	{
		return insets;
	}
	const gchar* name = nullptr;
	gtk_image_get_icon_name(GTK_IMAGE(image), &name, nullptr);
	const int size = gtk_image_get_pixel_size(GTK_IMAGE(image));
	const int scale = gtk_widget_get_scale_factor(button);
	GdkPixbuf* pixbuf = name && (size > 0)
			? gtk_icon_theme_load_icon_for_scale(gtk_icon_theme_get_default(), name, size, scale, GTK_ICON_LOOKUP_FORCE_SIZE, nullptr)
			: nullptr;
	if (!pixbuf)
	{
		return insets;
	}
	if (gdk_pixbuf_get_has_alpha(pixbuf) && (gdk_pixbuf_get_n_channels(pixbuf) == 4))
	{
		const int w = gdk_pixbuf_get_width(pixbuf);
		const int h = gdk_pixbuf_get_height(pixbuf);
		const int stride = gdk_pixbuf_get_rowstride(pixbuf);
		const guchar* pixels = gdk_pixbuf_read_pixels(pixbuf);
		int x0 = w, y0 = h, x1 = -1;
		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				if (pixels[y * stride + x * 4 + 3] > 64)
				{
					x0 = std::min(x0, x);
					x1 = std::max(x1, x);
					y0 = std::min(y0, y);
				}
			}
		}
		if (x1 >= 0)
		{
			insets.left += x0 / scale;
			insets.top += y0 / scale;
			insets.right += (w - 1 - x1) / scale;
		}
	}
	g_object_unref(pixbuf);
	return insets;
}

}

void WhiskerMenu::Window::update_header_gaps()
{
	const int gap = 6;

	// Visible command buttons in the title area, in visual left-to-right order
	std::vector<GtkWidget*> buttons;
	if (gtk_widget_get_parent(GTK_WIDGET(m_commands_box)) == GTK_WIDGET(m_title_box))
	{
		GList* children = gtk_container_get_children(GTK_CONTAINER(m_commands_box));
		for (GList* i = children; i; i = i->next)
		{
			GtkWidget* child = GTK_WIDGET(i->data);
			for (int c = 0; c < 9; ++c)
			{
				if ((m_commands_button[c] == child) && wm_settings->command[c]->get_shown())
				{
					buttons.push_back(child);
				}
			}
		}
		g_list_free(children);
		if (gtk_widget_get_direction(GTK_WIDGET(m_commands_box)) == GTK_TEXT_DIR_RTL)
		{
			std::reverse(buttons.begin(), buttons.end());
		}
	}

	// Side gaps: the outer button's icon edge sits 6px from the menu edge
	int start_inset = 0, end_inset = 0, top_inset = gap;
	if (!buttons.empty())
	{
		const Insets first = command_insets(buttons.front());
		const Insets last = command_insets(buttons.back());
		const bool rtl = gtk_widget_get_direction(GTK_WIDGET(m_commands_box)) == GTK_TEXT_DIR_RTL;
		// margin_start is the visual left in LTR and the visual right in RTL
		start_inset = rtl ? last.right : first.left;
		end_inset = rtl ? first.left : last.right;
		for (GtkWidget* button : buttons)
		{
			top_inset = std::min(top_inset, command_insets(button).top);
		}
	}
	gtk_widget_set_margin_start(GTK_WIDGET(m_commands_box), std::max(0, gap - start_inset));
	gtk_widget_set_margin_end(GTK_WIDGET(m_commands_box), std::max(0, gap - end_inset));

	// Top gap: 6px of empty space under the resize grip; when the header is
	// the first row, the icons' own empty border is part of it
	gint position = -1;
	gtk_container_child_get(GTK_CONTAINER(m_vbox), GTK_WIDGET(m_title_box), "position", &position, nullptr);
	const bool header_on_top = gtk_widget_get_visible(GTK_WIDGET(m_title_box)) && (position == 0);
	const int top_margin = header_on_top && !buttons.empty() ? std::max(0, gap - top_inset) : (header_on_top ? 0 : gap);
	gtk_widget_set_margin_top(GTK_WIDGET(m_vbox), top_margin);
	// the avatar is drawn with a 2px inset (profile.cpp)
	const int picture_top = header_on_top ? std::max(0, gap - 2 - top_margin) : 0;
	gtk_widget_set_margin_top(m_profile->get_picture(), picture_top);
	// Center the name's visible letters (top of the ink down to the baseline;
	// descenders ignored) on the avatar. Done with yalign, not margins: a
	// margin would make the label taller and push the whole header down.
	GtkWidget* name = m_profile->get_username();
	gtk_widget_set_margin_top(name, 0);
	if (GTK_IS_LABEL(name) && gtk_widget_get_visible(name))
	{
		PangoLayout* layout = gtk_label_get_layout(GTK_LABEL(name));
		PangoRectangle ink, logical;
		pango_layout_get_pixel_extents(layout, &ink, &logical);
		const int baseline = PANGO_PIXELS(pango_layout_get_baseline(layout));
		int row_h = 0, label_h = 0;
		gtk_widget_get_preferred_height(GTK_WIDGET(m_title_box), nullptr, &row_h);
		gtk_widget_get_preferred_height(name, nullptr, &label_h);
		const double avatar_center = picture_top + (row_h - picture_top) / 2.0;
		const double letters_center = (label_h - logical.height) / 2.0 + (ink.y + baseline) / 2.0;
		float yalign = 0.5f;
		if ((ink.height > 0) && (row_h > label_h))
		{
			yalign = std::min(1.0f, std::max(0.0f, float((avatar_center - letters_center) / (row_h - label_h))));
		}
		gtk_label_set_yalign(GTK_LABEL(name), yalign);
	}
}
