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

#ifndef VIBRISMENU_ICON_RENDERER_H
#define VIBRISMENU_ICON_RENDERER_H

#include <gtk/gtk.h>

GtkCellRenderer* vibrismenu_icon_renderer_new();

// The icon as the lists show it: without the empty border of its artwork,
// its visible part filling a box of "size". Owned by the icon cache.
cairo_surface_t* vibrismenu_icon_surface(GtkWidget* widget, GIcon* gicon, gint size);

#endif
