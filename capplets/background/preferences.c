/* -*- mode: c; style: linux -*- */

/* preferences.c
 * Copyright (C) 2000 Helix Code, Inc.
 *
 * Written by Bradford Hovinen <hovinen@helixcode.com>
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
# include "config.h"
#endif

#include <stdlib.h>

#include <gnome.h>
#include <gdk-pixbuf/gdk-pixbuf-xlibrgb.h>
#include <capplet-widget.h>

#include "preferences.h"
#include "applier.h"

static GtkObjectClass *parent_class;
static Applier *applier = NULL;

static void preferences_init             (Preferences *prefs);
static void preferences_class_init       (PreferencesClass *class);

static gint       xml_read_int           (xmlNodePtr node,
					  gchar *propname);
static xmlNodePtr xml_write_int          (gchar *name, 
					  gchar *propname, 
					  gint number);

static gint apply_timeout_cb             (Preferences *prefs);

static GdkColor  *read_color_from_string (gchar *string);

guint
preferences_get_type (void)
{
	static guint preferences_type = 0;

	if (!preferences_type) {
		GtkTypeInfo preferences_info = {
			"Preferences",
			sizeof (Preferences),
			sizeof (PreferencesClass),
			(GtkClassInitFunc) preferences_class_init,
			(GtkObjectInitFunc) preferences_init,
			(GtkArgSetFunc) NULL,
			(GtkArgGetFunc) NULL
		};

		preferences_type = 
			gtk_type_unique (gtk_object_get_type (), 
					 &preferences_info);
	}

	return preferences_type;
}

static void
preferences_init (Preferences *prefs)
{
	prefs->frozen = FALSE;

	prefs->color1 = NULL;
	prefs->color2 = NULL;
	prefs->wallpaper_filename = NULL;
	prefs->wallpaper_sel_path = NULL;
}

static void
preferences_class_init (PreferencesClass *class) 
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass *) class;
	object_class->destroy = preferences_destroy;

	parent_class = 
		GTK_OBJECT_CLASS (gtk_type_class (gtk_object_get_type ()));

	if (applier == NULL)
		applier = APPLIER (applier_new ());
}

GtkObject *
preferences_new (void) 
{
	GtkObject *object;

	object = gtk_type_new (preferences_get_type ());

	return object;
}

GtkObject *
preferences_clone (Preferences *prefs)
{
	GtkObject *object;
	Preferences *new_prefs;

	g_return_val_if_fail (prefs != NULL, NULL);
	g_return_val_if_fail (IS_PREFERENCES (prefs), NULL);

	object = preferences_new ();

	new_prefs = PREFERENCES (object);

	new_prefs->enabled           = prefs->enabled;
	new_prefs->gradient_enabled  = prefs->gradient_enabled;
	new_prefs->wallpaper_enabled = prefs->wallpaper_enabled;
	new_prefs->orientation       = prefs->orientation;
	new_prefs->wallpaper_type    = prefs->wallpaper_type;

	if (prefs->color1)
		new_prefs->color1 = gdk_color_copy (prefs->color1);
	if (prefs->color2)
		new_prefs->color2 = gdk_color_copy (prefs->color2);

	new_prefs->wallpaper_filename = g_strdup (prefs->wallpaper_filename);
	new_prefs->wallpaper_sel_path = g_strdup (prefs->wallpaper_sel_path);;

	new_prefs->auto_apply        = prefs->auto_apply;
	new_prefs->adjust_opacity = prefs->adjust_opacity;
	new_prefs->opacity  = prefs->opacity;

	return object;
}

void
preferences_destroy (GtkObject *object) 
{
	Preferences *prefs;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_PREFERENCES (object));

	prefs = PREFERENCES (object);

	g_free (prefs->wallpaper_filename);
	g_free (prefs->wallpaper_sel_path);

	parent_class->destroy (object);
}

void
preferences_load (Preferences *prefs) 
{
	gchar *string, *wp, *wp1;
	int i, wps;

	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_PREFERENCES (prefs));

	if (prefs->color1) g_free (prefs->color1);
	string = gnome_config_get_string
		("/Background/Default/color1=#39374b");
	prefs->color1 = read_color_from_string (string);
	g_free (string);

	if (prefs->color2) g_free (prefs->color2);
	string = gnome_config_get_string
		("/Background/Default/color2=#42528f");
	prefs->color2 = read_color_from_string (string);
	g_free (string);

	string = gnome_config_get_string ("/Background/Default/Enabled=True");
	prefs->enabled = !(g_strcasecmp (string, "True"));
	g_free (string);

	string = gnome_config_get_string ("/Background/Default/type=simple");
	if (!g_strcasecmp (string, "wallpaper"))
		prefs->wallpaper_enabled = TRUE;
	else if (g_strcasecmp (string, "simple"))
		prefs->wallpaper_enabled = FALSE;
	g_free (string);

	string = gnome_config_get_string ("/Background/Default/simple=gradent");
	if (!g_strcasecmp (string, "gradient"))
		prefs->gradient_enabled = TRUE;
	else if (g_strcasecmp (string, "solid"))
		prefs->gradient_enabled = FALSE;
	g_free (string);

	string = gnome_config_get_string
		("/Background/Default/gradient=vertical");
	if (!g_strcasecmp (string, "vertical"))
		prefs->orientation = ORIENTATION_VERT;
	else if (!g_strcasecmp (string, "horizontal"))
		prefs->orientation = ORIENTATION_HORIZ;
	g_free (string);

	prefs->wallpaper_type = 
		gnome_config_get_int ("/Background/Default/wallpaperAlign=0");

	prefs->wallpaper_filename = 
		gnome_config_get_string
		("/Background/Default/wallpaper=(None)");
	prefs->wallpaper_sel_path = 
		gnome_config_get_string 
		("/Background/Default/wallpapers_dir=./");

	prefs->auto_apply =
		gnome_config_get_bool ("/Background/Default/autoApply=true");

	if (!g_strcasecmp (prefs->wallpaper_filename, "(None)")) {
		g_free(prefs->wallpaper_filename);
		prefs->wallpaper_filename = NULL;
		prefs->wallpaper_enabled = FALSE;
	} else {
		prefs->wallpaper_enabled = TRUE;
	}
	
	wps = gnome_config_get_int ("/Background/Default/wallpapers=0");

	for (i = 0; i < wps; i++) {
		wp = g_strdup_printf ("/Background/Default/wallpaper%d", i+1);
		wp1 = gnome_config_get_string (wp);
		g_free (wp);

		if (wp1 == NULL) continue;			

		prefs->wallpapers = g_slist_prepend (prefs->wallpapers, wp1);
	}

	prefs->wallpapers = g_slist_reverse (prefs->wallpapers);

	prefs->adjust_opacity =
		gnome_config_get_bool
		("/Background/Default/adjustOpacity=true");

	prefs->opacity =
		gnome_config_get_int ("/Background/Default/opacity=255");
}

void 
preferences_save (Preferences *prefs) 
{
	char buffer[16];
	char *wp;
	GSList *item;
	int i;

	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_PREFERENCES (prefs));

	snprintf (buffer, sizeof(buffer), "#%02x%02x%02x",
		  prefs->color1->red >> 8,
		  prefs->color1->green >> 8,
		  prefs->color1->blue >> 8);
	gnome_config_set_string ("/Background/Default/color1", buffer);
	snprintf (buffer, sizeof(buffer), "#%02x%02x%02x",
		  prefs->color2->red >> 8,
		  prefs->color2->green >> 8,
		  prefs->color2->blue >> 8);
	gnome_config_set_string ("/Background/Default/color2", buffer);

	gnome_config_set_string ("/Background/Default/Enabled",
				 (prefs->enabled) ? "True" : "False");

	gnome_config_set_string ("/Background/Default/simple",
				 (prefs->gradient_enabled) ? 
				 "gradient" : "solid");
	gnome_config_set_string ("/Background/Default/gradient",
				 (prefs->orientation == ORIENTATION_VERT) ?
				 "vertical" : "horizontal");
    
	gnome_config_set_string ("/Background/Default/wallpaper",
				 (prefs->wallpaper_enabled) ? 
				 prefs->wallpaper_filename : "none");
	gnome_config_set_int ("/Background/Default/wallpaperAlign", 
			      prefs->wallpaper_type);

	gnome_config_set_int ("/Background/Default/wallpapers",
			      g_slist_length (prefs->wallpapers));

	for (i = 1, item = prefs->wallpapers; item; i++, item = item->next) {
		wp = g_strdup_printf ("/Background/Default/wallpaper%d", i);
		gnome_config_set_string (wp, (char *)item->data);
		g_free (wp);
	}

	gnome_config_set_bool ("/Background/Default/autoApply", prefs->auto_apply);
	gnome_config_set_bool ("/Background/Default/adjustOpacity", prefs->adjust_opacity);
	gnome_config_set_int ("/Background/Default/opacity", 
			      prefs->opacity);

	gnome_config_sync ();
}

void
preferences_changed (Preferences *prefs) 
{
	/* FIXME: This is a really horrible kludge... */
	if (prefs->frozen > 1) return;

	if (prefs->frozen == 0) {
		if (prefs->timeout_id)
			gtk_timeout_remove (prefs->timeout_id);

		if (prefs->auto_apply)
			prefs->timeout_id = 
				gtk_timeout_add
				(2000, (GtkFunction) apply_timeout_cb, prefs);
	}

	applier_apply_prefs (applier, prefs, FALSE, TRUE);
}

void
preferences_apply_now (Preferences *prefs)
{
	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_PREFERENCES (prefs));

	if (prefs->timeout_id)
		gtk_timeout_remove (prefs->timeout_id);

	prefs->timeout_id = 0;

	applier_apply_prefs (applier, prefs, TRUE, FALSE);
}

void
preferences_apply_preview (Preferences *prefs)
{
	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_PREFERENCES (prefs));

	applier_apply_prefs (applier, prefs, FALSE, TRUE);
}

void 
preferences_freeze (Preferences *prefs) 
{
	prefs->frozen++;
}

void 
preferences_thaw (Preferences *prefs) 
{
	if (prefs->frozen > 0) prefs->frozen--;
}

Preferences *
preferences_read_xml (xmlDocPtr xml_doc) 
{
	Preferences *prefs;
	xmlNodePtr root_node, node;
	char *str;

	prefs = PREFERENCES (preferences_new ());

	root_node = xmlDocGetRootElement (xml_doc);

	if (strcmp (root_node->name, "background-properties"))
		return NULL;

	prefs->wallpaper_enabled = FALSE;
	prefs->gradient_enabled = FALSE;
	prefs->orientation = ORIENTATION_VERT;

	if (prefs->color1) {
		gdk_color_free (prefs->color1);
		prefs->color1 = NULL;
	}

	if (prefs->color2) {
		gdk_color_free (prefs->color2);
		prefs->color2 = NULL;
	}

	for (node = root_node->childs; node; node = node->next) {
		if (!strcmp (node->name, "bg-color1"))
			prefs->color1 = read_color_from_string
				(xmlNodeGetContent (node));
		else if (!strcmp (node->name, "bg-color2"))
			prefs->color2 = read_color_from_string
				(xmlNodeGetContent (node));
		else if (!strcmp (node->name, "enabled"))
			prefs->enabled = TRUE;
		else if (!strcmp (node->name, "wallpaper"))
			prefs->wallpaper_enabled = TRUE;
		else if (!strcmp (node->name, "gradient"))
			prefs->gradient_enabled = TRUE;
		else if (!strcmp (node->name, "orientation")) {
			str = xmlNodeGetContent (node);

			if (!g_strcasecmp (str, "horizontal"))
				prefs->orientation = ORIENTATION_HORIZ;
			else if (!g_strcasecmp (str, "vertical"))
				prefs->orientation = ORIENTATION_VERT;
		}
		else if (!strcmp (node->name, "wallpaper-type"))
			prefs->wallpaper_type = xml_read_int (node, NULL);
		else if (!strcmp (node->name, "wallpaper-filename"))
			prefs->wallpaper_filename = 
				g_strdup (xmlNodeGetContent (node));
		else if (!strcmp (node->name, "wallpaper-sel-path"))
			prefs->wallpaper_sel_path = 
				g_strdup (xmlNodeGetContent (node));
		else if (!strcmp (node->name, "auto-apply"))
			prefs->auto_apply = TRUE;
		else if (!strcmp (node->name, "adjust-opacity"))
			prefs->adjust_opacity = TRUE;
		else if (!strcmp (node->name, "opacity"))
			prefs->opacity = xml_read_int (node, NULL);
	}

	return prefs;
}

xmlDocPtr 
preferences_write_xml (Preferences *prefs) 
{
	xmlDocPtr doc;
	xmlNodePtr node;
	char tmp[16];

	doc = xmlNewDoc ("1.0");

	node = xmlNewDocNode (doc, NULL, "background-properties", NULL);

	snprintf (tmp, sizeof (tmp), "#%02x%02x%02x", 
		  prefs->color1->red >> 8,
		  prefs->color1->green >> 8,
		  prefs->color1->blue >> 8);
	xmlNewChild (node, NULL, "bg-color1", tmp);

	snprintf (tmp, sizeof (tmp), "#%02x%02x%02x", 
		  prefs->color2->red >> 8,
		  prefs->color2->green >> 8,
		  prefs->color2->blue >> 8);
	xmlNewChild (node, NULL, "bg-color2", tmp);

	if (prefs->enabled)
		xmlNewChild (node, NULL, "enabled", NULL);

	if (prefs->wallpaper_enabled)	
		xmlNewChild (node, NULL, "wallpaper", NULL);

	if (prefs->gradient_enabled)
		xmlNewChild (node, NULL, "gradient", NULL);

	xmlNewChild (node, NULL, "orientation", 
		     (prefs->orientation == ORIENTATION_VERT) ?
		     "vertical" : "horizontal");

	xmlAddChild (node, xml_write_int ("wallpaper-type", NULL,
					  prefs->wallpaper_type));

	xmlNewChild (node, NULL, "wallpaper-filename", 
		     prefs->wallpaper_filename);
	xmlNewChild (node, NULL, "wallpaper-sel-path", 
		     prefs->wallpaper_sel_path);

	if (prefs->auto_apply)
		xmlNewChild (node, NULL, "auto-apply", NULL);

	if (prefs->adjust_opacity)
		xmlNewChild (node, NULL, "adjust-opacity", NULL);
	xmlAddChild (node, xml_write_int ("opacity", NULL,
					  prefs->opacity));

	xmlDocSetRootElement (doc, node);

	return doc;
}

/* Read a numeric value from a node */

static gint
xml_read_int (xmlNodePtr node, char *propname) 
{
	char *text;

	if (propname == NULL)
		text = xmlNodeGetContent (node);
	else
		text = xmlGetProp (node, propname);

	if (text == NULL) 
		return 0;
	else
		return atoi (text);
}

/* Write out a numeric value in a node */

static xmlNodePtr
xml_write_int (gchar *name, gchar *propname, gint number) 
{
	xmlNodePtr node;
	gchar *str;

	g_return_val_if_fail (name != NULL, NULL);

	str = g_strdup_printf ("%d", number);

	node = xmlNewNode (NULL, name);

	if (propname == NULL)
		xmlNodeSetContent (node, str);
	else
		xmlSetProp (node, propname, str);

	g_free (str);

	return node;
}

static gint 
apply_timeout_cb (Preferences *prefs) 
{
	preferences_apply_now (prefs);

	return TRUE;
}

static GdkColor *
read_color_from_string (gchar *string) 
{
	GdkColor *color;
	gint32 rgb;

	color = g_new0 (GdkColor, 1);

	gdk_color_parse (string, color);
	rgb = ((color->red >> 8) << 16) ||
		((color->green >> 8) << 8) ||
		(color->blue >> 8);
	color->pixel = xlib_rgb_xpixel_from_rgb (rgb);

	return color;
}
