/* -*- mode: c; style: linux -*- */

/* background-properties-capplet.c
 * Copyright (C) 2000-2001 Ximian, Inc.
 *
 * Written by: Bradford Hovinen <hovinen@ximian.com>,
 *             Rachel Hestilow <hestilow@ximian.com>
 *             Seth Nickell     <snickell@stanford.edu>
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

#include <string.h>

#include <gnome.h>
#include <gconf/gconf-client.h>
#include <glade/glade.h>
#include <libgnomevfs/gnome-vfs.h>

#include "capplet-util.h"
#include "gconf-property-editor.h"
#include "applier.h"
#include "preview-file-selection.h"
#include "activate-settings-daemon.h"

enum
{
	TARGET_URI_LIST,
	TARGET_COLOR,
	TARGET_BGIMAGE
/*	TARGET_BACKGROUND_RESET*/  
};

static GtkTargetEntry drop_types[] =
{
	{"text/uri-list", 0, TARGET_URI_LIST},
	{ "application/x-color", 0, TARGET_COLOR },
	{ "property/bgimage", 0, TARGET_BGIMAGE }
/*	{ "x-special/gnome-reset-background", 0, TARGET_BACKGROUND_RESET }*/
};

static gint n_drop_types = sizeof (drop_types) / sizeof (GtkTargetEntry);

static const int n_enum_vals = WPTYPE_NONE + 1;

typedef struct
{
	BGApplier** appliers;
	BGPreferences *prefs;
} ApplierSet;

/* Create a new set of appliers, and load the default preferences */

static ApplierSet*
applier_set_new (void)
{
	int i;
	ApplierSet *set = g_new0 (ApplierSet, 1);

	set->appliers = g_new0 (BGApplier*, n_enum_vals);
	for (i = 0; i < n_enum_vals; i++)
		set->appliers[i] = BG_APPLIER (bg_applier_new (BG_APPLIER_PREVIEW));
	set->prefs = BG_PREFERENCES (bg_preferences_new ());
	bg_preferences_load (set->prefs);

	return set;
}

/* Destroy all prefs/appliers in set, and free structure */

static void
applier_set_free (ApplierSet *set)
{
	int i;

	g_return_if_fail (set != NULL);

	for (i = 0; i < n_enum_vals; i++)
		g_object_unref (G_OBJECT (set->appliers[i]));
	g_free (set->appliers);
	g_object_unref (G_OBJECT (set->prefs));
}

/* Trigger a redraw in each applier in the set */

static void
applier_set_redraw (ApplierSet *set)
{
	int i;

	g_return_if_fail (set != NULL);
	
	for (i = 0; i < n_enum_vals; i++)
	{
		set->prefs->wallpaper_enabled = TRUE;
		set->prefs->wallpaper_type = i;
		bg_applier_apply_prefs (set->appliers[i], set->prefs);
	}
}

/* Retrieve legacy gnome_config settings and store them in the GConf
 * database. This involves some translation of the settings' meanings.
 */

static void
get_legacy_settings (void) 
{
	int          val_int;
	char        *val_string;
	gboolean     val_boolean;
	gboolean     def;
	gchar       *val_filename;
	gchar	    *path;
	gchar	    *prefix;

	GConfClient *client;

	/* gnome_config needs to be told to use the Gnome1 prefix */
	path = g_build_filename (g_get_home_dir (), ".gnome", "Background", NULL);
	prefix = g_strconcat ("=", path, "=", "/Default/", NULL);
	gnome_config_push_prefix (prefix);
	g_free (prefix);
	g_free (path);

	client = gconf_client_get_default ();

	gconf_client_set_bool (client, BG_PREFERENCES_DRAW_BACKGROUND,
			       gnome_config_get_bool ("Enabled=true"), NULL);

	val_filename = gnome_config_get_string ("wallpaper=(none)");

	if (val_filename != NULL && strcmp (val_filename, "(none)"))
	{
		gconf_client_set_string (client, BG_PREFERENCES_PICTURE_FILENAME,
					 val_filename, NULL);
		gconf_client_set_string (client, BG_PREFERENCES_PICTURE_OPTIONS,
			 bg_preferences_get_wptype_as_string (gnome_config_get_int ("wallpaperAlign=0")), NULL);
	}
	else
		gconf_client_set_string (client, BG_PREFERENCES_PICTURE_OPTIONS, "none", NULL);

	g_free (val_filename);


	gconf_client_set_string (client, BG_PREFERENCES_PRIMARY_COLOR,
				 gnome_config_get_string ("color1"), NULL);
	gconf_client_set_string (client, BG_PREFERENCES_SECONDARY_COLOR,
				 gnome_config_get_string ("color2"), NULL);

	/* Code to deal with new enum - messy */
	val_int = -1;
	val_string = gnome_config_get_string_with_default ("simple=solid", &def);
	if (!def) {
		if (!strcmp (val_string, "solid")) {
			val_int = ORIENTATION_SOLID;
		} else {
			g_free (val_string);
			val_string = gnome_config_get_string_with_default ("gradient=vertical", &def);
			if (!def)
				val_int = (!strcmp (val_string, "vertical")) ? ORIENTATION_VERT : ORIENTATION_HORIZ;
		}
	}

	g_free (val_string);

	if (val_int != -1)
		gconf_client_set_string (client, BG_PREFERENCES_COLOR_SHADING_TYPE, bg_preferences_get_orientation_as_string (val_int), NULL);

	val_boolean = gnome_config_get_bool_with_default ("adjustOpacity=true", &def);

	if (!def && val_boolean)
		gconf_client_set_int (client, BG_PREFERENCES_PICTURE_OPACITY,
				      gnome_config_get_int ("opacity=100"), NULL);
	else
		gconf_client_set_int (client, BG_PREFERENCES_PICTURE_OPACITY, 100, NULL);

	gnome_config_pop_prefix ();
}

static orientation_t
string_to_orientation (const gchar *string)
{
        orientation_t type = ORIENTATION_SOLID;

	if (string) {
		if (!strncmp (string, "vertical-gradient", sizeof ("vertical-gradient"))) {
			type = ORIENTATION_VERT;
		} else if (!strncmp (string, "horizontal-gradient", sizeof ("horizontal-gradient"))) {
			type = ORIENTATION_HORIZ;
		}
	}
	   
	return type;
}

static void
update_color_widget_labels_and_visibility (ApplierSet *set, const gchar *value_str)
{
	gboolean two_colors = TRUE;
	char *color1_string = NULL; 
	char *color2_string = NULL;

	GtkWidget *color1_label;
	GtkWidget *color2_label;
	GtkWidget *color2_box;

	orientation_t orientation = string_to_orientation (value_str);

	switch (orientation) {
	case ORIENTATION_SOLID: /* solid */ 
		color1_string = _("C_olor:"); 
		two_colors = FALSE; 
		break;
	case ORIENTATION_HORIZ: /* horiz */ 
		color1_string = _("_Left Color:"); 
		color2_string = _("_Right Color:"); 
		break;
	case ORIENTATION_VERT: /* vert  */
		color1_string = _("_Top Color:");
		color2_string = _("_Bottom Color:"); 
		break;
	default:
		break;
	}

	color1_label = g_object_get_data (G_OBJECT (set->prefs), "color1-label");
	color2_label = g_object_get_data (G_OBJECT (set->prefs), "color2-label");
	color2_box =   g_object_get_data (G_OBJECT (set->prefs), "color2-box");

	g_assert (color1_label);
	gtk_label_set_text_with_mnemonic (GTK_LABEL(color1_label), color1_string);

	if (two_colors) {
		gtk_widget_show (color2_box);

		g_assert (color2_label);
		gtk_label_set_text_with_mnemonic (GTK_LABEL(color2_label), color2_string);
	} else {
		gtk_widget_hide (color2_box);
	}
}


/* Initial apply to the preview, and setting of the color frame's sensitivity.
 *
 * We use a double-delay mechanism: first waiting 100 ms, then working in an
 * idle handler so that the preview gets rendered after the rest of the dialog
 * is displayed. This prevents the program from appearing to stall on startup,
 * making it feel more natural to the user.
 */

static gboolean
real_realize_cb (ApplierSet *set)
{
	GtkWidget *color_frame;

	g_return_val_if_fail (set != NULL, TRUE);

	if (G_OBJECT (set->prefs)->ref_count == 0)
		return FALSE;

	color_frame = g_object_get_data (G_OBJECT (set->prefs), "color-frame");

	applier_set_redraw (set);
	gtk_widget_set_sensitive (color_frame, bg_applier_render_color_p (set->appliers[0], set->prefs));
	
	return FALSE;
}

static gboolean
realize_2_cb (ApplierSet *set) 
{
	gtk_idle_add ((GtkFunction) real_realize_cb, set);
	return FALSE;
}

static void
realize_cb (GtkWidget *widget, ApplierSet *set)
{
	gtk_idle_add ((GtkFunction) real_realize_cb, set);
	gtk_timeout_add (100, (GtkFunction) realize_2_cb, set);
}

/* Callback issued when some value changes in a property editor. This merges the
 * value with the preferences object and applies the settings to the preview. It
 * also sets the sensitivity of the color frame depending on whether the base
 * colors are visible through the wallpaper (or whether the wallpaper is
 * enabled). This cannot be done with a guard as it depends on a much more
 * complex criterion than a simple boolean configuration property.
 */

static void
peditor_value_changed (GConfPropertyEditor *peditor, const gchar *key, const GConfValue *value, ApplierSet *set) 
{
	GConfEntry *entry;
	GtkWidget *color_frame;

	entry = gconf_entry_new (key, value);
	bg_preferences_merge_entry (set->prefs, entry);
	gconf_entry_free (entry);

	if (GTK_WIDGET_REALIZED (bg_applier_get_preview_widget (set->appliers[n_enum_vals - 1])))
		applier_set_redraw (set);

	if (!strcmp (key, BG_PREFERENCES_PICTURE_FILENAME) ||
	    !strcmp (key, BG_PREFERENCES_PICTURE_OPTIONS))
	{
		color_frame = g_object_get_data (G_OBJECT (set->prefs), "color-frame");
		gtk_widget_set_sensitive (color_frame, bg_applier_render_color_p (set->appliers[0], set->prefs));
	}
	else if (!strcmp (key, BG_PREFERENCES_COLOR_SHADING_TYPE))
	{
		update_color_widget_labels_and_visibility (set, gconf_value_get_string (value));
	}
}

/* Set up the property editors in the dialog. This also loads the preferences
 * and sets up the callbacks.
 */

static void
setup_dialog (GladeXML *dialog, GConfChangeSet *changeset, ApplierSet *set)
{
	GObject     *peditor;
	GConfClient *client;
	gchar *color_option;

	/* Override the enabled setting to make sure background is enabled */
	client = gconf_client_get_default ();
	gconf_client_set_bool (client, BG_PREFERENCES_DRAW_BACKGROUND, TRUE, NULL);

	/* We need to be able to retrieve the color frame in
	   callbacks */
	g_object_set_data (G_OBJECT (set->prefs), "color-frame", WID ("color_vbox"));
	g_object_set_data (G_OBJECT (set->prefs), "color2-box", WID ("color2_box"));
	g_object_set_data (G_OBJECT (set->prefs), "color1-label", WID("color1_label"));
	g_object_set_data (G_OBJECT (set->prefs), "color2-label", WID("color2_label"));

	peditor = gconf_peditor_new_select_menu_with_enum
		(changeset, BG_PREFERENCES_COLOR_SHADING_TYPE, WID ("border_shading"), bg_preferences_orientation_get_type (), TRUE, NULL);
	g_signal_connect (peditor, "value-changed", (GCallback) peditor_value_changed, set);

	peditor = gconf_peditor_new_color
		(changeset, BG_PREFERENCES_PRIMARY_COLOR, WID ("color1"), NULL);
	g_signal_connect (peditor, "value-changed", (GCallback) peditor_value_changed, set);

	peditor = gconf_peditor_new_color
		(changeset, BG_PREFERENCES_SECONDARY_COLOR, WID ("color2"), NULL);
	g_signal_connect (peditor, "value-changed", (GCallback) peditor_value_changed, set);
	peditor = gconf_peditor_new_image
		(changeset, BG_PREFERENCES_PICTURE_FILENAME, WID ("background_image_button"), NULL);
	g_signal_connect (peditor, "value-changed", (GCallback) peditor_value_changed, set);
	
	peditor = gconf_peditor_new_select_radio_with_enum
		(changeset, BG_PREFERENCES_PICTURE_OPTIONS, gtk_radio_button_get_group (GTK_RADIO_BUTTON (WID ("radiobutton1"))), bg_preferences_wptype_get_type (), TRUE, NULL);
	g_signal_connect (peditor, "value-changed", (GCallback) peditor_value_changed, set);

	/* Make sure preferences get applied to the preview */
	if (GTK_WIDGET_REALIZED (bg_applier_get_preview_widget (set->appliers[n_enum_vals - 1])))
		applier_set_redraw (set);
	else
		g_signal_connect_after (G_OBJECT (bg_applier_get_preview_widget (set->appliers[n_enum_vals - 1])),
					"realize", (GCallback) realize_cb, set);
	
	color_option = gconf_client_get_string (gconf_client_get_default (),
						BG_PREFERENCES_COLOR_SHADING_TYPE,
						NULL);
	update_color_widget_labels_and_visibility (set, color_option);
	g_free (color_option);
}

/* Construct the dialog */

static GladeXML *
create_dialog (ApplierSet *set) 
{
	GtkWidget *widget;
	GladeXML  *dialog;
	GSList *group;
	int i;
	const gchar *labels[] = { N_("_Wallpaper"), N_("C_entered"), N_("_Scaled"), N_("Stretc_hed"), N_("_No Picture") };
	GtkWidget *label;

	/* FIXME: What the hell is domain? */
	dialog = glade_xml_new (GNOMECC_DATA_DIR "/interfaces/background-properties.glade", "prefs_widget", NULL);
	widget = glade_xml_get_widget (dialog, "prefs_widget");

	g_object_weak_ref (G_OBJECT (widget), (GWeakNotify) g_object_unref, dialog);

	/* Set up the applier buttons */
	group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (WID ("radiobutton1")));
	group = g_slist_copy (group);
	group = g_slist_reverse (group);
	
	for (i = 0; group && i < n_enum_vals; i++, group = group->next)
	{
		GtkWidget *w = GTK_WIDGET (group->data);
		GtkWidget *vbox = gtk_vbox_new (FALSE, 0);
		
		gtk_container_set_border_width (GTK_CONTAINER (vbox), 0);
		
		gtk_widget_destroy (GTK_BIN (w)->child);
		gtk_container_add (GTK_CONTAINER (w), vbox);
		
		gtk_box_pack_start (GTK_BOX (vbox),
				    bg_applier_get_preview_widget (set->appliers[i]),
				    TRUE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (vbox),
				    gtk_label_new_with_mnemonic (gettext (labels[i])),
				    FALSE, FALSE, 0);
		gtk_widget_show_all (vbox);
	}

	g_slist_free (group);

	label = gtk_label_new_with_mnemonic (_("Select _picture:"));
	gtk_frame_set_label_widget (GTK_FRAME (WID ("picture_frame")),
				    label);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label),
			  	       WID ("background_image_button"));
	gtk_widget_show (label);

	gtk_label_set_mnemonic_widget (GTK_LABEL (WID ("border_shading_label")),
				       WID ("border_shading"));
	gtk_label_set_mnemonic_widget (GTK_LABEL (WID ("color1_label")),
				       WID ("color1"));
	gtk_label_set_mnemonic_widget (GTK_LABEL (WID ("color2_label")),
				       WID ("color2"));

	return dialog;
}


static void
cb_dialog_response (GtkDialog *dialog, gint response_id)
{
	if (response_id == GTK_RESPONSE_HELP)
		capplet_help (GTK_WINDOW (dialog),
			"wgoscustdesk.xml",
			"goscustdesk-7");
	else
		gtk_main_quit ();
}
/* Callback issued during drag movements */

static gboolean
drag_motion_cb (GtkWidget *widget, GdkDragContext *context,
		gint x, gint y, guint time, gpointer data)
{
	return FALSE;
}

/* Callback issued during drag leaves */

static void
drag_leave_cb (GtkWidget *widget, GdkDragContext *context,
	       guint time, gpointer data)
{
	gtk_widget_queue_draw (widget);
}

/* Callback issued on actual drops. Attempts to load the file dropped. */

static void
drag_data_received_cb (GtkWidget *widget, GdkDragContext *context,
		       gint x, gint y,
		       GtkSelectionData *selection_data,
		       guint info, guint time, gpointer data)
{
	GConfClient *client = gconf_client_get_default ();
	ApplierSet *set = (ApplierSet*) data; 
	gchar *picture_option;    

	if (info == TARGET_URI_LIST ||
	    info == TARGET_BGIMAGE)
	{
		GList *uris;

		g_print ("got %s\n", selection_data->data);
		uris = gnome_vfs_uri_list_parse ((gchar *) selection_data->data);
		/* Arbitrarily pick the first item */
		if (uris != NULL && uris->data != NULL)
		{
			GnomeVFSURI *uri = (GnomeVFSURI *) uris->data;
			GConfEntry *entry;
			GConfValue *value = gconf_value_new (GCONF_VALUE_STRING);
			gchar *base;

			base = gnome_vfs_unescape_string (gnome_vfs_uri_get_path (uri),
							  G_DIR_SEPARATOR_S);
			gconf_value_set_string (value, base);
			g_free (base);

			/* Hmm, should we bother with changeset here? */
			gconf_client_set (client, BG_PREFERENCES_PICTURE_FILENAME, value, NULL);
			gconf_client_suggest_sync (client, NULL);

			/* this isn't emitted by the peditors,
			 * so we have to manually update */
			/* FIXME: this can't be right!!!! -jrb */
			entry = gconf_entry_new (BG_PREFERENCES_PICTURE_FILENAME, value);
			bg_preferences_merge_entry (set->prefs, entry);
			gconf_entry_free (entry);
			gconf_value_free (value);

			picture_option = gconf_client_get_string (client, BG_PREFERENCES_PICTURE_OPTIONS, NULL);
			if ((picture_option != NULL) && !strcmp (picture_option, "none"))
				gconf_client_set_string (client, BG_PREFERENCES_PICTURE_OPTIONS, "wallpaper", NULL);

			g_free (picture_option);

		}
		gnome_vfs_uri_list_free (uris);
	} else if (info == TARGET_COLOR) {
		guint16 *channels;
		char *color_spec;
		
		if (selection_data->length != 8 || selection_data->format != 16) {
			return;
		}
        
		channels = (guint16 *) selection_data->data;
		color_spec = g_strdup_printf ("#%02X%02X%02X", channels[0] >> 8, channels[1] >> 8, channels[2] >> 8);
		gconf_client_set_string (client, BG_PREFERENCES_PICTURE_OPTIONS, "none", NULL);
		gconf_client_set_string (client, BG_PREFERENCES_COLOR_SHADING_TYPE, "solid", NULL);
		gconf_client_set_string (client, BG_PREFERENCES_PRIMARY_COLOR, color_spec, NULL);
		g_free (color_spec);
	} else if (info == TARGET_BGIMAGE) {

	} else if (info == TARGET_URI_LIST) {

	}
	
	if (GTK_WIDGET_REALIZED (bg_applier_get_preview_widget (set->appliers[n_enum_vals - 1])))
		applier_set_redraw (set);

}

int
main (int argc, char **argv) 
{
	GConfClient    *client;
	GladeXML       *dialog;
	GtkWidget      *dialog_win;
	ApplierSet     *set;

	static gboolean get_legacy;
	static struct poptOption cap_options[] = {
		{ "get-legacy", '\0', POPT_ARG_NONE, &get_legacy, 0,
		  N_("Retrieve and store legacy settings"), NULL },
		{ NULL, '\0', 0, NULL, 0, NULL, NULL }
	};

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gnome_program_init ("gnome-background-properties", VERSION,
			    LIBGNOMEUI_MODULE, argc, argv,
			    GNOME_PARAM_POPT_TABLE, cap_options,
			    NULL);

	activate_settings_daemon ();

	client = gconf_client_get_default ();
	gconf_client_add_dir (client, "/desktop/gnome/background", GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

	if (get_legacy) {
		get_legacy_settings ();
	} else {
		set = applier_set_new ();
		dialog = create_dialog (set);
		setup_dialog (dialog, NULL, set);

		dialog_win = gtk_dialog_new_with_buttons
			(_("Background Preferences"), NULL, 0,
			 GTK_STOCK_HELP, GTK_RESPONSE_HELP,
			 GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
			 NULL);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog_win),
			GTK_RESPONSE_CLOSE);

		g_signal_connect (G_OBJECT (dialog_win),
				    "response",
				    G_CALLBACK (cb_dialog_response), NULL);

		gtk_drag_dest_set (dialog_win, GTK_DEST_DEFAULT_ALL,
				   drop_types, n_drop_types,
				   GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_MOVE);
		g_signal_connect (G_OBJECT (dialog_win), "drag-motion",
				  G_CALLBACK (drag_motion_cb), NULL);
		g_signal_connect (G_OBJECT (dialog_win), "drag-leave",
				  G_CALLBACK (drag_leave_cb), NULL);
		g_signal_connect (G_OBJECT (dialog_win), "drag-data-received",
				  G_CALLBACK (drag_data_received_cb),
				  set);

		g_object_weak_ref (G_OBJECT (dialog_win), (GWeakNotify) applier_set_free, set);
		gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog_win)->vbox), WID ("prefs_widget"), TRUE, TRUE, GNOME_PAD_SMALL);
		capplet_set_icon (dialog_win, "background-capplet.png");
		gtk_widget_show (dialog_win);

		gtk_main ();
	}

	return 0;
}
