/* -*- mode: c; style: linux -*- */

/* preferences.c
 * Copyright (C) 2000 Helix Code, Inc.
 *
 * Written by Bradford Hovinen (hovinen@helixcode.com)
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

#include <glib.h>
#include <gnome.h>

#include "preferences.h"

#include <glade/glade.h>

static GnomeCCPreferences *old_prefs;

static GtkWidget *prefs_dialog;
static GladeXML *prefs_dialog_data;

enum {
	CHANGED_SIGNAL,
	LAST_SIGNAL
};

static gint gnomecc_preferences_signals[LAST_SIGNAL] = { 0 };

static void gnomecc_preferences_init (GnomeCCPreferences *prefs);
static void gnomecc_preferences_class_init (GnomeCCPreferencesClass *klass);

static void set_single_window_controls_sensitive (GladeXML *data, gboolean s);

guint
gnomecc_preferences_get_type (void) 
{
	static guint gnomecc_preferences_type;

	if (!gnomecc_preferences_type) {
		GtkTypeInfo gnomecc_preferences_info = {
			"GnomeCCPreferences",
			sizeof (GnomeCCPreferences),
			sizeof (GnomeCCPreferencesClass),
			(GtkClassInitFunc) gnomecc_preferences_class_init,
			(GtkObjectInitFunc) gnomecc_preferences_init,
			(GtkArgSetFunc) NULL,
			(GtkArgGetFunc) NULL
		};

		gnomecc_preferences_type = 
			gtk_type_unique (gtk_object_get_type (),
					 &gnomecc_preferences_info);
	}

	return gnomecc_preferences_type;
}

static void 
gnomecc_preferences_init (GnomeCCPreferences *prefs) 
{
	prefs->layout = LAYOUT_NONE;
	prefs->embed = FALSE;
	prefs->single_window = TRUE;
}

static void 
gnomecc_preferences_class_init (GnomeCCPreferencesClass *klass) 
{
	GtkObjectClass *object_class;

	object_class = GTK_OBJECT_CLASS (klass);

	gnomecc_preferences_signals[CHANGED_SIGNAL] =
		gtk_signal_new ("changed", GTK_RUN_FIRST, 
				object_class->type,
				GTK_SIGNAL_OFFSET (GnomeCCPreferencesClass, 
						   changed),
				gtk_marshal_NONE__NONE, 
				GTK_TYPE_NONE, 0);

	gtk_object_class_add_signals (object_class, 
				      gnomecc_preferences_signals,
				      LAST_SIGNAL);
}

GnomeCCPreferences *
gnomecc_preferences_new (void) 
{
	return gtk_type_new (gnomecc_preferences_get_type ());
}

GnomeCCPreferences *
gnomecc_preferences_clone (GnomeCCPreferences *prefs) 
{
	GnomeCCPreferences *new_prefs;

	new_prefs = gnomecc_preferences_new ();
	gnomecc_preferences_copy (new_prefs, prefs);

	return new_prefs;
}

void
gnomecc_preferences_copy (GnomeCCPreferences *new, GnomeCCPreferences *old) 
{
	new->layout = old->layout;
	new->single_window = old->single_window;
	new->embed = old->embed;
}

void 
gnomecc_preferences_load (GnomeCCPreferences *prefs) 
{
	g_return_if_fail (prefs != NULL);

	gnome_config_push_prefix ("/control-center/appearance");
	prefs->embed = gnome_config_get_bool ("embed=false");
	prefs->single_window = gnome_config_get_bool ("single_window=false");
	prefs->layout = gnome_config_get_int ("layout=2");
	gnome_config_pop_prefix ();
}

void 
gnomecc_preferences_save (GnomeCCPreferences *prefs) 
{
	g_return_if_fail (prefs != NULL);

	gnome_config_push_prefix ("/control-center/appearance");
	gnome_config_set_bool ("embed", prefs->embed);
	gnome_config_set_bool ("single_window", prefs->single_window);
	gnome_config_set_bool ("layout", prefs->layout);
	gnome_config_pop_prefix ();

	gnome_config_sync ();
}

static void
place_preferences (GladeXML *prefs_data, GnomeCCPreferences *prefs) 
{
	GtkWidget *widget;

	if (prefs->embed) {
		widget = glade_xml_get_widget (prefs_data, "embed_widget");
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (widget), TRUE);
	} else {
		widget = glade_xml_get_widget (prefs_data, "no_embed_widget");
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (widget), TRUE);
	}

	if (prefs->single_window) {
		widget = glade_xml_get_widget (prefs_data, "single_widget");
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (widget), TRUE);
	} else {
		widget = glade_xml_get_widget (prefs_data, "multiple_widget");
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (widget), TRUE);
	}

	if (prefs->layout == LAYOUT_TREE) {
		widget = glade_xml_get_widget (prefs_data, "tree_widget");
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (widget), TRUE);
		set_single_window_controls_sensitive
			(prefs_dialog_data, FALSE);
	} else if (prefs->layout == LAYOUT_ICON_LIST) {
		widget = glade_xml_get_widget (prefs_data, "icon_list_widget");
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (widget), TRUE);
		set_single_window_controls_sensitive
			(prefs_dialog_data, TRUE);
	}
}

static void
read_preferences (GladeXML *prefs_data, GnomeCCPreferences *prefs) 
{
	GtkWidget *widget;

	widget = glade_xml_get_widget (prefs_data, "embed_widget");
	prefs->embed = 
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

	widget = glade_xml_get_widget (prefs_data, "single_widget");
	prefs->single_window =
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

	widget = glade_xml_get_widget (prefs_data, "tree_widget");
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		prefs->layout = LAYOUT_TREE;
	else
		prefs->layout = LAYOUT_ICON_LIST;

	gnomecc_preferences_save (prefs);

	gtk_signal_emit (GTK_OBJECT (prefs),
			 gnomecc_preferences_signals[CHANGED_SIGNAL]);
}

static void
prefs_dialog_ok_cb (GtkWidget *widget, GladeXML *data) 
{
	GnomeCCPreferences *prefs;

	prefs = gtk_object_get_data (GTK_OBJECT (data), "prefs_struct");
	read_preferences (data, prefs);
	gnome_dialog_close (GNOME_DIALOG (prefs_dialog));
	prefs_dialog = NULL;
	prefs_dialog_data = NULL;
}

static void
prefs_dialog_apply_cb (GtkWidget *widget, GladeXML *data) 
{
	GnomeCCPreferences *prefs;

	prefs = gtk_object_get_data (GTK_OBJECT (data), "prefs_struct");
	read_preferences (data, prefs);
}

static void
prefs_dialog_cancel_cb (GtkWidget *widget, GladeXML *data) 
{
	GnomeCCPreferences *prefs;

	prefs = gtk_object_get_data (GTK_OBJECT (data), "prefs_struct");
	gnomecc_preferences_copy (prefs, old_prefs);
	gtk_signal_emit (GTK_OBJECT (prefs),
			 gnomecc_preferences_signals[CHANGED_SIGNAL]);

	gnome_dialog_close (GNOME_DIALOG (prefs_dialog));
	prefs_dialog = NULL;
	prefs_dialog_data = NULL;
}

static void
set_single_window_controls_sensitive (GladeXML *data, gboolean s) 
{
	GtkWidget *widget;

	widget = glade_xml_get_widget (prefs_dialog_data, "single_widget");
	gtk_widget_set_sensitive (widget, s);
	widget = glade_xml_get_widget (prefs_dialog_data, "multiple_widget");
	gtk_widget_set_sensitive (widget, s);
}

void
tree_widget_toggled_cb (GtkWidget *widget) 
{
	set_single_window_controls_sensitive
		(prefs_dialog_data,
		 !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)));
}

GtkWidget *
gnomecc_preferences_get_config_dialog (GnomeCCPreferences *prefs) 
{
	GtkWidget *widget;

	if (prefs_dialog_data) return prefs_dialog;

	old_prefs = gnomecc_preferences_clone (prefs);

	prefs_dialog_data = 
		glade_xml_new (GLADEDIR "/gnomecc-preferences.glade", NULL);

	if (!prefs_dialog_data) {
		g_warning ("Could not find data for preferences dialog");
		return NULL;
	}

	place_preferences (prefs_dialog_data, prefs);

	prefs_dialog = glade_xml_get_widget (prefs_dialog_data, 
					     "preferences_dialog");

	gnome_dialog_button_connect
		(GNOME_DIALOG (prefs_dialog), 0,
		 GTK_SIGNAL_FUNC (prefs_dialog_ok_cb),
		 prefs_dialog_data);

	gnome_dialog_button_connect
		(GNOME_DIALOG (prefs_dialog), 1,
		 GTK_SIGNAL_FUNC (prefs_dialog_apply_cb),
		 prefs_dialog_data);

	gnome_dialog_button_connect
		(GNOME_DIALOG (prefs_dialog), 2,
		 GTK_SIGNAL_FUNC (prefs_dialog_cancel_cb),
		 prefs_dialog_data);

	gtk_object_set_data (GTK_OBJECT (prefs_dialog_data), 
			     "prefs_struct", prefs);

	glade_xml_signal_connect (prefs_dialog_data,
				  "tree_widget_toggled_cb",
				  tree_widget_toggled_cb);

	return prefs_dialog;
}
