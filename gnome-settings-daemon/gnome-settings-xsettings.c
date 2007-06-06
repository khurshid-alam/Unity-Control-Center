/*
 * Copyright (C) 2007 The GNOME Foundation
 *
 * Authors:  Rodrigo Moya
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <config.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>
#include <glib/gi18n.h>
#include <gdk/gdkx.h>
#include <gconf/gconf-client.h>

#include "gnome-settings-module.h"
#include "xsettings-manager.h"
#include "utils.h"

extern XSettingsManager **managers;

#ifdef HAVE_XFT2
#define FONT_RENDER_DIR "/desktop/gnome/font_rendering"
#define FONT_ANTIALIASING_KEY FONT_RENDER_DIR "/antialiasing"
#define FONT_HINTING_KEY      FONT_RENDER_DIR "/hinting"
#define FONT_RGBA_ORDER_KEY   FONT_RENDER_DIR "/rgba_order"
#define FONT_DPI_KEY          FONT_RENDER_DIR "/dpi"
#endif /* HAVE_XFT2 */

typedef struct _TranslationEntry TranslationEntry;
typedef void (* TranslationFunc) (TranslationEntry *trans,
                                  GConfValue       *value);

struct _TranslationEntry {
	const char *gconf_key;
	const char *xsetting_name;
  
	GConfValueType gconf_type;
	TranslationFunc translate;
};

typedef struct {
	GnomeSettingsModule parent;
} GnomeSettingsModuleXSettings;

typedef struct {
	GnomeSettingsModuleClass parent_class;
} GnomeSettingsModuleXSettingsClass;

static GnomeSettingsModuleRunlevel gnome_settings_module_xsettings_get_runlevel (GnomeSettingsModule *module);
static gboolean gnome_settings_module_xsettings_initialize (GnomeSettingsModule *module, GConfClient *config_client);
static gboolean gnome_settings_module_xsettings_start (GnomeSettingsModule *module);
static gboolean gnome_settings_module_xsettings_stop (GnomeSettingsModule *module);

#ifdef HAVE_XFT2 
/* X servers sometimes lie about the screen's physical dimensions, so we cannot
 * compute an accurate DPI value.  When this happens, the user gets fonts that
 * are too huge or too tiny.  So, we see what the server returns:  if it reports
 * something outside of the range [DPI_LOW_REASONABLE_VALUE,
 * DPI_HIGH_REASONABLE_VALUE], then we assume that it is lying and we use
 * DPI_FALLBACK instead.
 *
 * See get_dpi_from_gconf_or_server() below, and also
 * https://bugzilla.novell.com/show_bug.cgi?id=217790
 */
#define DPI_FALLBACK 96
#define DPI_LOW_REASONABLE_VALUE 50
#define DPI_HIGH_REASONABLE_VALUE 500

static void gnome_settings_update_xft (GConfClient *client);
static void xft_callback              (GConfEntry  *entry);
#endif /* HAVE_XFT2 */

static void
gnome_settings_module_xsettings_class_init (GnomeSettingsModuleXSettingsClass *klass)
{
	GnomeSettingsModuleClass *module_class;

	module_class = (GnomeSettingsModuleClass *) klass;
	module_class->get_runlevel = gnome_settings_module_xsettings_get_runlevel;
	module_class->initialize = gnome_settings_module_xsettings_initialize;
	module_class->start = gnome_settings_module_xsettings_start;
	module_class->stop = gnome_settings_module_xsettings_stop;
}

static void
gnome_settings_module_xsettings_init (GnomeSettingsModuleXSettings *module)
{
}

GType
gnome_settings_module_xsettings_get_type (void)
{
	static GType module_type = 0;
  
	if (!module_type) {
		static const GTypeInfo module_info = {
			sizeof (GnomeSettingsModuleXSettingsClass),
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			(GClassInitFunc) gnome_settings_module_xsettings_class_init,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof (GnomeSettingsModuleXSettings),
			0,		/* n_preallocs */
			(GInstanceInitFunc) gnome_settings_module_xsettings_init,
		};
      
		module_type = g_type_register_static (GNOME_SETTINGS_TYPE_MODULE,
						      "GnomeSettingsModuleXSettings",
						      &module_info, 0);
	}
  
	return module_type;
}

static GnomeSettingsModuleRunlevel
gnome_settings_module_xsettings_get_runlevel (GnomeSettingsModule *module)
{
	return GNOME_SETTINGS_MODULE_RUNLEVEL_SERVICES;
}

static void
translate_bool_int (TranslationEntry *trans,
		    GConfValue       *value)
{
	int i;

	g_assert (value->type == trans->gconf_type);
  
	for (i = 0; managers [i]; i++)  
		xsettings_manager_set_int (managers [i], trans->xsetting_name,
					   gconf_value_get_bool (value));
}

static void
translate_int_int (TranslationEntry *trans,
                   GConfValue       *value)
{
	int i;

	g_assert (value->type == trans->gconf_type);

	for (i = 0; managers [i]; i++)  
		xsettings_manager_set_int (managers [i], trans->xsetting_name,
					   gconf_value_get_int (value));
}

static void
translate_string_string (TranslationEntry *trans,
                         GConfValue       *value)
{
	int i;

	g_assert (value->type == trans->gconf_type);

	for (i = 0; managers [i]; i++)  
		xsettings_manager_set_string (managers [i],
					      trans->xsetting_name,
					      gconf_value_get_string (value));
}

static void
translate_string_string_toolbar (TranslationEntry *trans,
				 GConfValue       *value)
{
	int i;
	const char *tmp;
  
	g_assert (value->type == trans->gconf_type);

	/* This is kind of a workaround since GNOME expects the key value to be
	 * "both_horiz" and gtk+ wants the XSetting to be "both-horiz".
	 */
	tmp = gconf_value_get_string (value);
	if (tmp && strcmp (tmp, "both_horiz") == 0)
		tmp = "both-horiz";

	for (i = 0; managers [i]; i++) 
		xsettings_manager_set_string (managers [i],
                                  trans->xsetting_name,
                                  tmp);
}

static TranslationEntry translations [] = {
	{ "/desktop/gnome/peripherals/mouse/double_click",	"Net/DoubleClickTime",
	  GCONF_VALUE_INT,		translate_int_int },
	{ "/desktop/gnome/peripherals/mouse/drag_threshold",  "Net/DndDragThreshold",
	  GCONF_VALUE_INT,          translate_int_int },
	{ "/desktop/gnome/gtk-color-palette",			"Gtk/ColorPalette",
	  GCONF_VALUE_STRING,	translate_string_string },
	{ "/desktop/gnome/interface/font_name",		"Gtk/FontName",
	  GCONF_VALUE_STRING,	translate_string_string },
	{ "/desktop/gnome/interface/gtk_key_theme",		"Gtk/KeyThemeName",
	  GCONF_VALUE_STRING,	translate_string_string },
	{ "/desktop/gnome/interface/toolbar_style",			"Gtk/ToolbarStyle",
	  GCONF_VALUE_STRING,	translate_string_string_toolbar },
	{ "/desktop/gnome/interface/toolbar_icon_size",		"Gtk/ToolbarIconSize",
	  GCONF_VALUE_STRING,	translate_string_string },
	{ "/desktop/gnome/interface/can_change_accels",		"Gtk/CanChangeAccels",
	  GCONF_VALUE_BOOL,		translate_bool_int },
	{ "/desktop/gnome/interface/cursor_blink",		"Net/CursorBlink",
	  GCONF_VALUE_BOOL,		translate_bool_int },
	{ "/desktop/gnome/interface/cursor_blink_time",	"Net/CursorBlinkTime",
	  GCONF_VALUE_INT,		translate_int_int },
	{ "/desktop/gnome/interface/gtk_theme",		"Net/ThemeName",
	  GCONF_VALUE_STRING,	translate_string_string },
	{ "/desktop/gnome/interface/gtk_color_scheme",	"Gtk/ColorScheme",
	  GCONF_VALUE_STRING,	translate_string_string },
	{ "/desktop/gnome/interface/gtk-im-preedit-style",	"Gtk/IMPreeditStyle",
	  GCONF_VALUE_STRING,	translate_string_string },
	{ "/desktop/gnome/interface/gtk-im-status-style",	"Gtk/IMStatusStyle",
	  GCONF_VALUE_STRING,	translate_string_string },
	{ "/desktop/gnome/interface/icon_theme",		"Net/IconThemeName",
	  GCONF_VALUE_STRING,	translate_string_string },
	{ "/desktop/gnome/interface/file_chooser_backend",	"Gtk/FileChooserBackend",
	  GCONF_VALUE_STRING,	translate_string_string },
	{ "/desktop/gnome/interface/menus_have_icons",	"Gtk/MenuImages",
	  GCONF_VALUE_BOOL,		translate_bool_int },
	{ "/desktop/gnome/interface/menubar_accel",  "Gtk/MenuBarAccel",
	  GCONF_VALUE_STRING,      translate_string_string },
	{ "/desktop/gnome/peripherals/mouse/cursor_theme",	"Gtk/CursorThemeName",
	  GCONF_VALUE_STRING,	translate_string_string },
	{ "/desktop/gnome/peripherals/mouse/cursor_size",	"Gtk/CursorThemeSize",
	  GCONF_VALUE_INT,		translate_int_int },
	{ "/desktop/gnome/interface/show_input_method_menu",  "Gtk/ShowInputMethodMenu",
	  GCONF_VALUE_BOOL,		translate_bool_int },
	{ "/desktop/gnome/interface/show_unicode_menu",  "Gtk/ShowUnicodeMenu",
	  GCONF_VALUE_BOOL,		translate_bool_int },
};

static TranslationEntry*
find_translation_entry (const char *gconf_key)
{
	int i;

	i = 0;
	while (i < G_N_ELEMENTS (translations)) {
		if (strcmp (translations[i].gconf_key, gconf_key) == 0)
			return &translations[i];

		++i;
	}

	return NULL;
}

static const gchar* 
type_to_string (GConfValueType type)
{
	switch (type) {
	case GCONF_VALUE_INT:
		return "int";
	case GCONF_VALUE_STRING:
		return "string";
	case GCONF_VALUE_FLOAT:
		return "float";
	case GCONF_VALUE_BOOL:
		return "bool";
	case GCONF_VALUE_SCHEMA:
		return "schema";
	case GCONF_VALUE_LIST:
		return "list";
	case GCONF_VALUE_PAIR:
		return "pair";
	case GCONF_VALUE_INVALID:
		return "*invalid*";
	default:
		g_assert_not_reached();
		return NULL; /* for warnings */
	}
}

static void
process_value (TranslationEntry *trans,
               GConfValue       *val)
{  
	if (val == NULL) {
		int i;

		for (i = 0; managers [i]; i++)  
			xsettings_manager_delete_setting (managers [i], trans->xsetting_name);
	} else {
		if (val->type == trans->gconf_type)
			(* trans->translate) (trans, val);
		else {
			g_warning (_("GConf key %s set to type %s but its expected type was %s\n"),
				   trans->gconf_key,
				   type_to_string (val->type),
				   type_to_string (trans->gconf_type));
		}
	}
}

static void
xsettings_callback (GConfEntry *entry)
{
	TranslationEntry *trans;
	int i;

	trans = find_translation_entry (entry->key);
	if (trans == NULL)
		return;

	process_value (trans, entry->value);
  
	for (i = 0; managers [i]; i++)
		xsettings_manager_set_string (managers [i], "Net/FallbackIconTheme",
					      "gnome");

	for (i = 0; managers [i]; i++)  
		xsettings_manager_notify (managers [i]);
}

static gboolean
gnome_settings_module_xsettings_initialize (GnomeSettingsModule *module, GConfClient *config_client)
{
	gnome_settings_register_config_callback ("/desktop/gnome/peripherals/mouse", xsettings_callback);
	gnome_settings_register_config_callback ("/desktop/gtk", xsettings_callback);
	gnome_settings_register_config_callback ("/desktop/gnome/interface", xsettings_callback);

	return TRUE;

#ifdef HAVE_XFT2  
	gnome_settings_register_config_callback (FONT_RENDER_DIR, xft_callback);
#endif /* HAVE_XFT2 */  
}

#ifdef HAVE_XFT2
static void
xft_callback (GConfEntry *entry)
{
	GConfClient *client;
	int i;

	client = gnome_settings_get_config_client ();

	gnome_settings_update_xft (client);

	for (i = 0; managers [i]; i++)  
		xsettings_manager_notify (managers [i]);
}

static double
dpi_from_pixels_and_mm (int pixels, int mm)
{
	double dpi;

	if (mm >= 1)
		dpi = pixels / (mm / 25.4);
	else
		dpi = 0;

	return dpi;
}

static double
get_dpi_from_x_server (void)
{
	GdkScreen *screen;
	double dpi;

	screen = gdk_screen_get_default ();
	if (screen) {
		double width_dpi, height_dpi;

		width_dpi = dpi_from_pixels_and_mm (gdk_screen_get_width (screen), gdk_screen_get_width_mm (screen));
		height_dpi = dpi_from_pixels_and_mm (gdk_screen_get_height (screen), gdk_screen_get_height_mm (screen));

		if (width_dpi < DPI_LOW_REASONABLE_VALUE || width_dpi > DPI_HIGH_REASONABLE_VALUE
		    || height_dpi < DPI_LOW_REASONABLE_VALUE || height_dpi > DPI_HIGH_REASONABLE_VALUE)
			dpi = DPI_FALLBACK;
		else
			dpi = (width_dpi + height_dpi) / 2.0;
	} else {
		/* Huh!?  No screen? */

		dpi = DPI_FALLBACK;
	}

	return dpi;
}

static double
get_dpi_from_gconf_or_x_server (GConfClient *client)
{
	GConfValue *value;
	double dpi;

	value = gconf_client_get_without_default (client, FONT_DPI_KEY, NULL);

	/* If the user has ever set the DPI preference in GConf, we use that.
	 * Otherwise, we see if the X server reports a reasonable DPI value:  some X
	 * servers report completely bogus values, and the user gets huge or tiny
	 * fonts which are unusable.
	 */

	if (value) {
		dpi = gconf_value_get_float (value);
		gconf_value_free (value);
	} else
		dpi = get_dpi_from_x_server ();

	return dpi;
}

typedef struct
{
	gboolean antialias;
	gboolean hinting;
	int dpi;
	const char *rgba;
	const char *hintstyle;
} GnomeXftSettings;

static const char *rgba_types[] = { "rgb", "bgr", "vbgr", "vrgb" };

/* Read GConf settings and determine the appropriate Xft settings based on them
 * This probably could be done a bit more cleanly with gconf_string_to_enum
 */
static void
gnome_xft_settings_get (GConfClient      *client,
			GnomeXftSettings *settings)
{
	char *antialiasing = gconf_client_get_string (client, FONT_ANTIALIASING_KEY, NULL);
	char *hinting = gconf_client_get_string (client, FONT_HINTING_KEY, NULL);
	char *rgba_order = gconf_client_get_string (client, FONT_RGBA_ORDER_KEY, NULL);
	double dpi = get_dpi_from_gconf_or_x_server (client);

	settings->antialias = TRUE;
	settings->hinting = TRUE;
	settings->hintstyle = "hintfull";
	settings->dpi = dpi * 1024; /* Xft wants 1/1024ths of an inch */
	settings->rgba = "rgb";

	if (rgba_order) {
		int i;
		gboolean found = FALSE;

		for (i = 0; i < G_N_ELEMENTS (rgba_types) && !found; i++)
			if (strcmp (rgba_order, rgba_types[i]) == 0) {
				settings->rgba = rgba_types[i];
				found = TRUE;
			}

		if (!found)
			g_warning ("Invalid value for " FONT_RGBA_ORDER_KEY ": '%s'",
				   rgba_order);
	}
  
	if (hinting) {
		if (strcmp (hinting, "none") == 0) {
			settings->hinting = 0;
			settings->hintstyle = "hintnone";
		} else if (strcmp (hinting, "slight") == 0) {
			settings->hinting = 1;
			settings->hintstyle = "hintslight";
		} else if (strcmp (hinting, "medium") == 0) {
			settings->hinting = 1;
			settings->hintstyle = "hintmedium";
		} else if (strcmp (hinting, "full") == 0) {
			settings->hinting = 1;
			settings->hintstyle = "hintfull";
		} else
			g_warning ("Invalid value for " FONT_HINTING_KEY ": '%s'",
				   hinting);
	}
  
	if (antialiasing) {
		gboolean use_rgba = FALSE;
      
		if (strcmp (antialiasing, "none") == 0)
			settings->antialias = 0;
		else if (strcmp (antialiasing, "grayscale") == 0)
			settings->antialias = 1;
		else if (strcmp (antialiasing, "rgba") == 0) {
			settings->antialias = 1;
			use_rgba = TRUE;
		} else
			g_warning ("Invalid value for " FONT_ANTIALIASING_KEY " : '%s'",
				   antialiasing);

		if (!use_rgba)
			settings->rgba = "none";
	}

	g_free (rgba_order);
	g_free (hinting);
	g_free (antialiasing);
}

static void
gnome_xft_settings_set_xsettings (GnomeXftSettings *settings)
{
	int i;
	for (i = 0; managers [i]; i++) {
		xsettings_manager_set_int (managers [i], "Xft/Antialias", settings->antialias);
		xsettings_manager_set_int (managers [i], "Xft/Hinting", settings->hinting);
		xsettings_manager_set_string (managers [i], "Xft/HintStyle", settings->hintstyle);
		xsettings_manager_set_int (managers [i], "Xft/DPI", settings->dpi);
		xsettings_manager_set_string (managers [i], "Xft/RGBA", settings->rgba);
	}
}

static void
gnome_xft_settings_set_xresources (GnomeXftSettings *settings)
{
	char *add[] = { "xrdb", "-nocpp", "-merge", NULL };
	GString *add_string = g_string_new (NULL);
	char *old_locale = g_strdup (setlocale (LC_NUMERIC, NULL));

	setlocale (LC_NUMERIC, "C");
	g_string_append_printf (add_string,
				"Xft.dpi: %f\n", settings->dpi / 1024.);
	g_string_append_printf (add_string,
				"Xft.antialias: %d\n", settings->antialias);
	g_string_append_printf (add_string,
				"Xft.hinting: %d\n", settings->hinting);
	g_string_append_printf (add_string,
				"Xft.hintstyle: %s\n", settings->hintstyle);
	g_string_append_printf (add_string,
				"Xft.rgba: %s\n", settings->rgba);

	gnome_settings_spawn_with_input (add, add_string->str);

	g_string_free (add_string, TRUE);
	setlocale (LC_NUMERIC, old_locale);
	g_free (old_locale);
}

/* We mirror the Xft properties both through XSETTINGS and through
 * X resources
 */
static void
gnome_settings_update_xft (GConfClient *client)
{
	GnomeXftSettings settings;

	gnome_xft_settings_get (client, &settings);
	gnome_xft_settings_set_xsettings (&settings);
	gnome_xft_settings_set_xresources (&settings);
}
#endif /* HAVE_XFT2 */

static gboolean
gnome_settings_module_xsettings_start (GnomeSettingsModule *module)
{
	GConfClient *client = gnome_settings_module_get_config_client (module);
	int i;

	i = 0;
	while (i < G_N_ELEMENTS (translations)) {
		GConfValue *val;
		GError *err;

		err = NULL;
		val = gconf_client_get (client,
                              translations[i].gconf_key,
                              &err);

		if (err != NULL) {
			fprintf (stderr, "Error getting value for %s: %s\n",
				 translations[i].gconf_key, err->message);
			g_error_free (err);
		} else {
			process_value (&translations[i], val);
			if (val != NULL)
				gconf_value_free (val);
		}
      
		++i;
	}

#ifdef HAVE_XFT2  
	gnome_settings_update_xft (client);
#endif /* HAVE_XFT */

	for (i = 0; managers [i]; i++)
		xsettings_manager_set_string (managers [i], "Net/FallbackIconTheme",
					      "gnome");

	for (i = 0; managers [i]; i++)
		xsettings_manager_notify (managers [i]);

	return TRUE;
}

static gboolean
gnome_settings_module_xsettings_stop (GnomeSettingsModule *module)
{
	return TRUE;
}
