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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <libgnomekbd/gkbd-keyboard-config.h>
#include "cc-common-language.h"
#include "gdm-languages.h"
#include "gnome-region-panel-system.h"
#include "gnome-region-panel-xkb.h"

static GSettings *locale_settings, *xkb_settings;

static void
locale_settings_changed (GSettings *settings,
			 const gchar *key,
			 gpointer user_data)
{
	gchar *language, *display_language;
	GtkBuilder *builder = GTK_BUILDER (user_data);

	if (g_str_equal (key, "region")) {
		language = g_settings_get_string (locale_settings, "region");
		display_language = gdm_get_language_from_name (language, NULL);
		gtk_label_set_text (GTK_LABEL (gtk_builder_get_object (builder, "user_format")),
				    display_language);
		g_free (language);
		g_free (display_language);
	}
}

static void
xkb_settings_changed (GSettings *settings,
		      const gchar *key,
		      gpointer user_data)
{
	GtkBuilder *builder = GTK_BUILDER (user_data);

	if (g_str_equal (key, "layouts")) {
		gint i;
		GString *str = NULL;
		gchar **layouts = g_settings_get_strv (settings, "layouts");

		for (i = 0; i < G_N_ELEMENTS (layouts); i++) {
			gchar *utf_visible = xkb_layout_description_utf8 (layouts[i]);

			if (utf_visible != NULL) {
				if (str != NULL) {
					str = g_string_append (str, ", ");
					str = g_string_append (str, utf_visible);
				} else
					str = g_string_new (utf_visible);

				g_free (utf_visible);
			}
		}

		g_strfreev (layouts);

		gtk_label_set_text (GTK_LABEL (gtk_builder_get_object (builder, "user_input_source")), str->str);
		g_string_free (str, TRUE);
	}
}

void
setup_system (GtkBuilder *builder)
{
	gchar *language, *display_language;

	locale_settings = g_settings_new ("org.gnome.system.locale");
	g_signal_connect (locale_settings, "changed",
			  G_CALLBACK (locale_settings_changed), builder);
	g_object_weak_ref (G_OBJECT (builder), (GWeakNotify) g_object_unref, locale_settings);

	xkb_settings = g_settings_new (GKBD_KEYBOARD_SCHEMA);
	g_signal_connect (xkb_settings, "changed",
			  G_CALLBACK (xkb_settings_changed), builder);
	g_object_weak_ref (G_OBJECT (builder), (GWeakNotify) g_object_unref, xkb_settings);

	/* Display user settings */
	language = cc_common_language_get_current_language ();
	display_language = gdm_get_language_from_name (language, NULL);
	gtk_label_set_text (GTK_LABEL (gtk_builder_get_object (builder, "user_display_language")),
			   display_language);
	g_free (language);
	g_free (display_language);

	language = g_settings_get_string (locale_settings, "region");
	if (language && language[0])
		display_language = gdm_get_language_from_name (language, NULL);
	else {
		language = cc_common_language_get_current_language ();
		display_language = gdm_get_language_from_name (language, NULL);
	}

	gtk_label_set_text (GTK_LABEL (gtk_builder_get_object (builder, "user_format")),
			    display_language);
	g_free (language);
	g_free (display_language);

	xkb_settings_changed (xkb_settings, "layouts", builder);
}
