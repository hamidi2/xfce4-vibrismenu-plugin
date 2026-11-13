/*
 * Copyright (C) 2020 Graeme Gott <graeme@gottcode.org>
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

#include "icon-renderer.h"

#include <algorithm>

#include <gio/gio.h>

#include <map>
#include <tuple>

//-----------------------------------------------------------------------------

struct _VibrisMenuIconRenderer
{
	GtkCellRenderer parent;

	gpointer launcher;
	GIcon* gicon;
	gint size;
	bool stretch;
};

#define VIBRISMENU_TYPE_ICON_RENDERER vibrismenu_icon_renderer_get_type()
G_DECLARE_FINAL_TYPE(VibrisMenuIconRenderer, vibrismenu_icon_renderer, VIBRISMENU, ICON_RENDERER, GtkCellRenderer)

G_DEFINE_TYPE(VibrisMenuIconRenderer, vibrismenu_icon_renderer, GTK_TYPE_CELL_RENDERER)

enum
{
	PROP_0,
	PROP_LAUNCHER,
	PROP_GICON,
	PROP_SIZE,
	PROP_STRETCH
};

//-----------------------------------------------------------------------------

static void vibrismenu_icon_renderer_get_preferred_width(GtkCellRenderer* renderer, GtkWidget*, gint* minimum, gint* natural)
{
	VibrisMenuIconRenderer* icon_renderer = VIBRISMENU_ICON_RENDERER(renderer);

	gint pad;
	gtk_cell_renderer_get_padding(renderer, &pad, nullptr);
	gint width = (pad * 2) + icon_renderer->size;

	if (minimum)
	{
		*minimum = width;
	}
	if (natural)
	{
		*natural = width;
	}
}

//-----------------------------------------------------------------------------

static void vibrismenu_icon_renderer_get_preferred_height(GtkCellRenderer* renderer, GtkWidget*, gint* minimum, gint* natural)
{
	VibrisMenuIconRenderer* icon_renderer = VIBRISMENU_ICON_RENDERER(renderer);

	gint pad;
	gtk_cell_renderer_get_padding(renderer, nullptr, &pad);
	gint height = (pad * 2) + icon_renderer->size;

	if (minimum)
	{
		*minimum = height;
	}
	if (natural)
	{
		*natural = height;
	}
}

//-----------------------------------------------------------------------------

// Rendered icons, kept for the life of the icon theme. Without this every
// paint looked up and loaded every visible icon again, which made painting a
// menu with many favorites slow enough to hold back dragging its edge.
// Each key holds a reference to its GIcon, so its address cannot be reused.

namespace
{

typedef std::tuple<GtkIconTheme*, GIcon*, bool, gint, gint> IconKey;
std::map<IconKey, cairo_surface_t*> icon_cache;

void icon_cache_clear()
{
	for (auto& entry : icon_cache)
	{
		g_object_unref(std::get<1>(entry.first));
		if (entry.second)
		{
			cairo_surface_destroy(entry.second);
		}
	}
	icon_cache.clear();
}

// The box of an icon's visible pixels. Icon artwork leaves an empty border
// of its own, different for every icon and theme; shown as it is, that
// border adds to every gap around the icon and makes equal gaps look unequal.
bool ink_box(cairo_surface_t* surface, int& x0, int& y0, int& x1, int& y1)
{
	if (cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE
			|| cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32)
	{
		return false;
	}
	cairo_surface_flush(surface);
	const int width = cairo_image_surface_get_width(surface);
	const int height = cairo_image_surface_get_height(surface);
	const int stride = cairo_image_surface_get_stride(surface);
	const unsigned char* data = cairo_image_surface_get_data(surface);
	x0 = width; y0 = height; x1 = -1; y1 = -1;
	for (int y = 0; y < height; ++y)
	{
		const guint32* row = reinterpret_cast<const guint32*>(data + y * stride);
		for (int x = 0; x < width; ++x)
		{
			if ((row[x] >> 24) > 24)
			{
				x0 = std::min(x0, x); x1 = std::max(x1, x);
				y0 = std::min(y0, y); y1 = std::max(y1, y);
			}
		}
	}
	return x1 >= x0;
}

cairo_surface_t* load_icon_as_is(GtkIconTheme* icon_theme, GdkWindow* window, GIcon* gicon, bool launcher, gint size, gint scale);

// The icon with its empty border removed: its visible part fills the box of
// "size". Loaded larger by as much as the border takes, so nothing is
// scaled up, then cut to its visible part and fitted to the box.
cairo_surface_t* load_icon(GtkIconTheme* icon_theme, GdkWindow* window, GIcon* gicon, bool launcher, gint size, gint scale)
{
	cairo_surface_t* surface = load_icon_as_is(icon_theme, window, gicon, launcher, size, scale);
	int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	if (!surface || !ink_box(surface, x0, y0, x1, y1))
	{
		return surface;
	}
	const int box = size * scale;
	int ink = std::max(x1 - x0 + 1, y1 - y0 + 1);
	if (ink >= box - 1)
	{
		return surface; // no border worth removing
	}
	const gint larger = std::min(size * 4, static_cast<gint>((size * static_cast<double>(box)) / ink + 1));
	if (cairo_surface_t* bigger = load_icon_as_is(icon_theme, window, gicon, launcher, larger, scale))
	{
		int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
		if (ink_box(bigger, bx0, by0, bx1, by1))
		{
			cairo_surface_destroy(surface);
			surface = bigger;
			x0 = bx0; y0 = by0; x1 = bx1; y1 = by1;
		}
		else
		{
			cairo_surface_destroy(bigger);
		}
	}
	const int ink_w = x1 - x0 + 1;
	const int ink_h = y1 - y0 + 1;
	const double fit = std::min(box / static_cast<double>(ink_w), box / static_cast<double>(ink_h));
	cairo_surface_t* trimmed = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, box, box);
	cairo_t* cr = cairo_create(trimmed);
	cairo_translate(cr, (box - ink_w * fit) / 2.0, (box - ink_h * fit) / 2.0);
	cairo_scale(cr, fit, fit);
	cairo_surface_set_device_scale(surface, 1.0, 1.0);
	cairo_set_source_surface(cr, surface, -x0, -y0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	cairo_surface_set_device_scale(trimmed, scale, scale);
	return trimmed;
}

cairo_surface_t* load_icon_as_is(GtkIconTheme* icon_theme, GdkWindow* window, GIcon* gicon, bool launcher, gint size, gint scale)
{
	const GtkIconLookupFlags flags = GtkIconLookupFlags(GTK_ICON_LOOKUP_USE_BUILTIN | GTK_ICON_LOOKUP_FORCE_SIZE);
	cairo_surface_t* surface = nullptr;
	GtkIconInfo* icon_info = gtk_icon_theme_lookup_by_gicon_for_scale(icon_theme, gicon, size, scale, flags);
	if (icon_info)
	{
		surface = gtk_icon_info_load_surface(icon_info, window, nullptr);
		g_object_unref(icon_info);
	}
	if (!surface)
	{
		icon_info = gtk_icon_theme_lookup_icon_for_scale(icon_theme,
				launcher ? "application-x-executable" : "applications-other",
				size, scale, flags);
		if (icon_info)
		{
			surface = gtk_icon_info_load_surface(icon_info, window, nullptr);
			g_object_unref(icon_info);
		}
	}
	return surface;
}

cairo_surface_t* icon_surface(GtkIconTheme* icon_theme, GdkWindow* window, GIcon* gicon, bool launcher, gint size, gint scale)
{
	static GtkIconTheme* watched = nullptr;
	if (icon_theme != watched)
	{
		// a theme change (or another screen's theme) starts over
		icon_cache_clear();
		if (watched)
		{
			g_signal_handlers_disconnect_by_func(watched, reinterpret_cast<gpointer>(&icon_cache_clear), nullptr);
			g_object_unref(watched);
		}
		watched = GTK_ICON_THEME(g_object_ref(icon_theme));
		g_signal_connect_swapped(watched, "changed", G_CALLBACK(&icon_cache_clear), nullptr);
	}

	const IconKey key(icon_theme, gicon, launcher, size, scale);
	auto found = icon_cache.find(key);
	if (found != icon_cache.end())
	{
		return found->second;
	}
	if (icon_cache.size() >= 4096)
	{
		// bound the memory held for icons no longer shown (reloaded menus get new GIcons)
		icon_cache_clear();
	}
	cairo_surface_t* surface = load_icon(icon_theme, window, gicon, launcher, size, scale);
	g_object_ref(gicon);
	icon_cache.emplace(key, surface);
	return surface;
}

}

//-----------------------------------------------------------------------------

cairo_surface_t* vibrismenu_icon_surface(GtkWidget* widget, GIcon* gicon, gint size)
{
	if (!gicon || (size < 1))
	{
		return nullptr;
	}
	return icon_surface(gtk_icon_theme_get_for_screen(gtk_widget_get_screen(widget)),
			gtk_widget_get_window(widget), gicon, false, size, gtk_widget_get_scale_factor(widget));
}

//-----------------------------------------------------------------------------

static void vibrismenu_icon_renderer_render(GtkCellRenderer* renderer, cairo_t* cr, GtkWidget* widget,
		const GdkRectangle*, const GdkRectangle* cell_area, GtkCellRendererState)
{
	VibrisMenuIconRenderer* icon_renderer = VIBRISMENU_ICON_RENDERER(renderer);

	if (!icon_renderer->gicon)
	{
		return;
	}

	GdkRectangle clip_area;
	if (!gdk_cairo_get_clip_rectangle(cr, &clip_area))
	{
		return;
	}

	const gint scale = gtk_widget_get_scale_factor(widget);
	cairo_surface_t* surface = icon_surface(gtk_icon_theme_get_for_screen(gtk_widget_get_screen(widget)),
			gtk_widget_get_window(widget), icon_renderer->gicon, icon_renderer->launcher != nullptr,
			icon_renderer->size, scale);
	if (!surface)
	{
		return;
	}

	GdkRectangle icon_area;
	icon_area.width = cairo_image_surface_get_width(surface) / scale;
	icon_area.height = cairo_image_surface_get_height(surface) / scale;
	icon_area.x = cell_area->x + (cell_area->width - icon_area.width) / 2;
	icon_area.y = cell_area->y + (cell_area->height - icon_area.height) / 2;

	GdkRectangle draw_area;
	if (gdk_rectangle_intersect(&clip_area, &icon_area, &draw_area))
	{
		cairo_set_source_surface(cr, surface, icon_area.x, icon_area.y);
		cairo_rectangle(cr, draw_area.x, draw_area.y, draw_area.width, draw_area.height);
		cairo_fill(cr);
	}
}

//-----------------------------------------------------------------------------

static void vibrismenu_icon_renderer_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec)
{
	VibrisMenuIconRenderer* icon_renderer = VIBRISMENU_ICON_RENDERER(object);

	switch(prop_id)
	{
	case PROP_LAUNCHER:
		g_value_set_pointer(value, icon_renderer->launcher);
		break;

	case PROP_GICON:
		g_value_set_object(value, icon_renderer->gicon);
		break;

	case PROP_SIZE:
		g_value_set_int(value, icon_renderer->size);
		break;

	case PROP_STRETCH:
		g_value_set_boolean(value, icon_renderer->stretch);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

//-----------------------------------------------------------------------------

static void vibrismenu_icon_renderer_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
	VibrisMenuIconRenderer* icon_renderer = VIBRISMENU_ICON_RENDERER(object);

	switch(prop_id)
	{
	case PROP_LAUNCHER:
		icon_renderer->launcher = g_value_get_pointer(value);
		break;

	case PROP_GICON:
		if (icon_renderer->gicon)
		{
			g_object_unref(icon_renderer->gicon);
		}
		icon_renderer->gicon = static_cast<GIcon*>(g_value_dup_object(value));
		break;

	case PROP_SIZE:
		icon_renderer->size = g_value_get_int(value);
		break;

	case PROP_STRETCH:
		icon_renderer->stretch = g_value_get_boolean(value);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

//-----------------------------------------------------------------------------

static void vibrismenu_icon_renderer_finalize(GObject* object)
{
	VibrisMenuIconRenderer* icon_renderer = VIBRISMENU_ICON_RENDERER(object);

	if (icon_renderer->gicon)
	{
		g_object_unref(icon_renderer->gicon);
	}

	G_OBJECT_CLASS(vibrismenu_icon_renderer_parent_class)->finalize(object);
}

//-----------------------------------------------------------------------------

static void vibrismenu_icon_renderer_class_init(VibrisMenuIconRendererClass* klass)
{
	GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
	gobject_class->finalize = &vibrismenu_icon_renderer_finalize;
	gobject_class->get_property = &vibrismenu_icon_renderer_get_property;
	gobject_class->set_property = &vibrismenu_icon_renderer_set_property;

	GtkCellRendererClass* renderer_class = GTK_CELL_RENDERER_CLASS(klass);
	renderer_class->get_preferred_width = &vibrismenu_icon_renderer_get_preferred_width;
	renderer_class->get_preferred_height = &vibrismenu_icon_renderer_get_preferred_height;
	renderer_class->render = &vibrismenu_icon_renderer_render;

	g_object_class_install_property(gobject_class,
			PROP_LAUNCHER,
			g_param_spec_pointer("launcher", "launcher", "launcher",
					GParamFlags(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

	g_object_class_install_property(gobject_class,
			PROP_GICON,
			g_param_spec_object("gicon", "gicon", "gicon",
					G_TYPE_ICON,
					GParamFlags(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

	g_object_class_install_property(gobject_class,
			PROP_SIZE,
			g_param_spec_int("size", "size", "size",
					1, G_MAXINT, 48,
					GParamFlags(G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

	g_object_class_install_property(gobject_class,
			PROP_STRETCH,
			g_param_spec_boolean("stretch", "stretch", "stretch",
					false,
					GParamFlags(G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

//-----------------------------------------------------------------------------

static void vibrismenu_icon_renderer_init(VibrisMenuIconRenderer*)
{
}

//-----------------------------------------------------------------------------

GtkCellRenderer* vibrismenu_icon_renderer_new()
{
	return GTK_CELL_RENDERER(g_object_new(VIBRISMENU_TYPE_ICON_RENDERER, nullptr));
}

//-----------------------------------------------------------------------------
