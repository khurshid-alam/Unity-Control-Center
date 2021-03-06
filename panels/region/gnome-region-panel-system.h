/*
 * Copyright (C) 2011 Rodrigo Moya
 *
 * Written by: Rodrigo Moya <rodrigo@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef __GNOME_REGION_PANEL_SYSTEM_H
#define __GNOME_REGION_PANEL_SYSTEM_H

#include <gtk/gtk.h>

void setup_system           (GtkBuilder *builder);
void system_update_language (GtkBuilder  *builder,
                             const gchar *language);
void locale_settings_changed (GSettings   *settings,
                              const gchar *key,
                              GtkBuilder  *dialog);

#endif
