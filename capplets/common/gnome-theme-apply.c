/* -*- mode: C; c-basic-offset: 4 -*-
 * themus - utilities for GNOME themes
 * Copyright (C) 2002 Jonathan Blandford <aes@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <string.h>
#include <gconf/gconf-client.h>
#include <gnome-wm-manager.h>
#include "gnome-theme-apply.h"

#define GTK_THEME_KEY      "/desktop/gnome/interface/gtk_theme"
#define COLOR_SCHEME_KEY   "/desktop/gnome/interface/gtk_color_scheme"
#define ICON_THEME_KEY     "/desktop/gnome/interface/icon_theme"
#define FONT_KEY	         "/desktop/gnome/interface/font_name"
#define CURSOR_THEME_KEY   "/desktop/gnome/peripherals/mouse/cursor_theme"
#define CURSOR_SIZE_KEY    "/desktop/gnome/peripherals/mouse/cursor_size"

#define compare(x,y) (!x && y) || (x && !y) || (x && y && strcmp (x, y))

void
gnome_meta_theme_set (GnomeThemeMetaInfo *meta_theme_info)
{
  GConfClient *client;
  gchar *old_key;
  GnomeWindowManager *window_manager;
  GnomeWMSettings wm_settings;

  gnome_wm_manager_init ();

  window_manager = gnome_wm_manager_get_current (gdk_display_get_default_screen (gdk_display_get_default ()));

  client = gconf_client_get_default ();

  /* Set the gtk+ key */
  old_key = gconf_client_get_string (client, GTK_THEME_KEY, NULL);
  if (compare (old_key, meta_theme_info->gtk_theme_name))
    {
      gconf_client_set_string (client, GTK_THEME_KEY, meta_theme_info->gtk_theme_name, NULL);
    }
  g_free (old_key);

  /* Set the color scheme key */
  old_key = gconf_client_get_string (client, COLOR_SCHEME_KEY, NULL);
  if (compare (old_key, meta_theme_info->gtk_color_scheme))
    {
      gchar *new_value = (meta_theme_info->gtk_color_scheme) ? meta_theme_info->gtk_color_scheme : "";
      gconf_client_set_string (client, COLOR_SCHEME_KEY, new_value, NULL);
    }
  g_free (old_key);

  /* Set the wm key */
  wm_settings.flags = GNOME_WM_SETTING_THEME;
  wm_settings.theme = meta_theme_info->metacity_theme_name;
  if (window_manager)
    gnome_window_manager_change_settings (window_manager, &wm_settings);

  /* set the icon theme */
  old_key = gconf_client_get_string (client, ICON_THEME_KEY, NULL);
  if (compare (old_key, meta_theme_info->icon_theme_name))
    {
      gconf_client_set_string (client, ICON_THEME_KEY, meta_theme_info->icon_theme_name, NULL);
    }
  g_free (old_key);

  /* Set the cursor theme key */
  old_key = gconf_client_get_string (client, CURSOR_THEME_KEY, NULL);
  if (compare (old_key, meta_theme_info->cursor_theme_name))
    {
      gconf_client_set_string (client, CURSOR_THEME_KEY, meta_theme_info->cursor_theme_name, NULL);
      gconf_client_set_int (client, CURSOR_SIZE_KEY, meta_theme_info->cursor_size, NULL);
    }
  g_free (old_key);
  g_object_unref (client);
}
