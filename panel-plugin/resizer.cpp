/*
 * Copyright (C) 2021-2025 Graeme Gott <graeme@gottcode.org>
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

#include "resizer.h"

#include "settings.h"
#include "slot.h"
#include "window.h"

#include <algorithm>

using namespace VibrisMenu;

//-----------------------------------------------------------------------------

Resizer::Resizer(Edge edge, Window* window) :
	m_window(window),
	m_cursor(nullptr),
	m_x(0),
	m_y(0),
	m_start{0, 0, 0, 0},
	m_delta_x(0),
	m_delta_y(0),
	m_delta_width(0),
	m_delta_height(0),
	m_pressed(false)
{
	m_drawing = gtk_drawing_area_new();
	update_size();
	gtk_widget_add_events(m_drawing, GDK_BUTTON_PRESS_MASK
			| GDK_BUTTON_RELEASE_MASK
			| GDK_POINTER_MOTION_MASK
			| GDK_ENTER_NOTIFY_MASK
			| GDK_LEAVE_NOTIFY_MASK);

	// Every motion is delivered as it arrives. GDK otherwise keeps the latest
	// motion back until the next frame is drawn, and when no further frame
	// came (the pointer stopped during a long step) it stayed undelivered:
	// the menu rested a step short of the pointer until the release. Nothing
	// is lost by this: a motion only replaces the drag target.
	g_signal_connect(m_drawing, "realize", G_CALLBACK(+[](GtkWidget* widget, gpointer)
		{
			gdk_window_set_event_compression(gtk_widget_get_window(widget), FALSE);
		}), nullptr);

	connect(m_drawing, "button-press-event",
		[this](GtkWidget*, GdkEvent* event) -> gboolean
		{
			GdkEventButton* event_button = reinterpret_cast<GdkEventButton*>(event);
			if ((event_button->button != 1) || wm_settings->lock_size)
			{
				// locked: the size the user settled on cannot be dragged away
				return GDK_EVENT_STOP;
			}
			m_pressed = true;

			// Screen coordinates, and the menu as it is now: every motion is
			// then measured from this one point. Coordinates relative to the
			// grip are relative to something that moves with each step; a
			// motion made while a step was still being laid out was measured
			// against the grip's old place, and the menu stopped short of
			// the pointer until the next motion or the release.
			// (Wayland has no screen coordinates: there the motions are
			// measured on the grip itself, as they come.)
			m_relative = m_window->is_layer_surface();
			m_x = m_relative ? event_button->x : event_button->x_root;
			m_y = m_relative ? event_button->y : event_button->y_root;

			m_window->resize_start();
			m_start = m_window->get_geometry();

			return GDK_EVENT_STOP;
		});

	connect(m_drawing, "button-release-event",
		[this](GtkWidget*, GdkEvent* event) -> gboolean
		{
			GdkEventButton* event_button = reinterpret_cast<GdkEventButton*>(event);
			if ((event_button->button != 1) || !m_pressed)
			{
				return GDK_EVENT_STOP;
			}
			m_pressed = false;

			m_window->resize_end();

			return GDK_EVENT_STOP;
		});

	connect(m_drawing, "motion-notify-event",
		[this](GtkWidget*, GdkEvent* event) -> gboolean
		{
			if (!m_pressed)
			{
				return GDK_EVENT_STOP;
			}

			GdkEventMotion* event_motion = reinterpret_cast<GdkEventMotion*>(event);
			if (m_relative)
			{
				const int delta_x = event_motion->x - m_x;
				const int delta_y = event_motion->y - m_y;
				m_window->resize(delta_x * m_delta_x, delta_y * m_delta_y,
						delta_x * m_delta_width, delta_y * m_delta_height);
				return GDK_EVENT_STOP;
			}
			follow(event_motion->x_root, event_motion->y_root);

			return GDK_EVENT_STOP;
		});

	connect(m_drawing, "enter-notify-event",
		[this](GtkWidget* widget, GdkEvent*) -> gboolean
		{
			// no resize cursor over an edge that cannot be dragged
			gdk_window_set_cursor(gtk_widget_get_window(widget), wm_settings->lock_size ? nullptr : m_cursor);
			return GDK_EVENT_PROPAGATE;
		});

	connect(m_drawing, "leave-notify-event",
		[](GtkWidget* widget, GdkEvent*) -> gboolean
		{
			gdk_window_set_cursor(gtk_widget_get_window(widget), nullptr);
			return GDK_EVENT_PROPAGATE;
		});

	const char* type = nullptr;
	switch (edge)
	{
	case BottomLeft:
		m_delta_x = 1;
		m_delta_width = -1;
		m_delta_height = 1;
		type = "nesw-resize";
		break;

	case Bottom:
		m_delta_height = 1;
		type = "ns-resize";
		break;

	case BottomRight:
		m_delta_width = 1;
		m_delta_height = 1;
		type = "nwse-resize";
		break;

	case Left:
		m_delta_x = 1;
		m_delta_width = -1;
		type = "ew-resize";
		break;

	case Right:
		m_delta_width = 1;
		type = "ew-resize";
		break;

	case TopLeft:
		m_delta_x = 1;
		m_delta_width = -1;
		m_delta_y = 1;
		m_delta_height = -1;
		type = "nwse-resize";
		break;

	case Top:
		m_delta_y = 1;
		m_delta_height = -1;
		type = "ns-resize";
		break;

	case TopRight:
	default:
		m_delta_y = 1;
		m_delta_height = -1;
		m_delta_width = 1;
		type = "nesw-resize";
		break;
	}
	m_cursor = gdk_cursor_new_from_name(gtk_widget_get_display(m_drawing), type);
}

//-----------------------------------------------------------------------------

void Resizer::update_size()
{
	const int size = std::max(4, Settings::scaled(3));
	gtk_widget_set_size_request(m_drawing, size, size);
}

//-----------------------------------------------------------------------------

void Resizer::follow(int x_root, int y_root)
{
	const int delta_x = x_root - m_x;
	const int delta_y = y_root - m_y;

	// where the pointer asks the menu to be, as a change from where it is
	const GdkRectangle& now = m_window->get_geometry();
	m_window->resize(m_start.x + delta_x * m_delta_x - now.x,
			m_start.y + delta_y * m_delta_y - now.y,
			m_start.width + delta_x * m_delta_width - now.width,
			m_start.height + delta_y * m_delta_height - now.height);
}

//-----------------------------------------------------------------------------

Resizer::~Resizer()
{
	if (m_cursor)
	{
		g_object_unref(G_OBJECT(m_cursor));
	}
}

//-----------------------------------------------------------------------------
