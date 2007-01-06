/* -*- mode: c; style: linux -*- */

/* sound-properties-capplet.c
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Written by Bradford Hovinen <hovinen@ximian.com>
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

#include <gnome.h>
#include <gconf/gconf-client.h>

#include <gdk/gdkx.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>

#include "capplet-util.h"
#include "gconf-property-editor.h"
#include "libsounds/sound-view.h"

#include <glade/glade.h>

#include <dbus/dbus-glib-lowlevel.h>
#if USE_HAL
#include <libhal.h>
#endif
#include <gst/gst.h>

/* Needed only for the sound capplet */

#include <stdlib.h>
#include <sys/types.h>

#include "activate-settings-daemon.h"
#include "pipeline-tests.h"

typedef enum {
	AUDIO_PLAYBACK,
	AUDIO_CAPTURE,
	VIDEO_PLAYBACK,
	VIDEO_CAPTURE
} device_type;

typedef struct _DeviceChooser
{
	const gchar *profile;
	int type;
	GtkWidget *combobox;
	GtkListStore *model;
	const gchar *test_pipeline;
} DeviceChooser;

#define ENABLE_ESD_KEY             "/desktop/gnome/sound/enable_esd"
#define EVENT_SOUNDS_KEY           "/desktop/gnome/sound/event_sounds"
#define VISUAL_BELL_KEY            "/apps/metacity/general/visual_bell"
#define AUDIO_BELL_KEY             "/apps/metacity/general/audible_bell"
#define VISUAL_BELL_TYPE_KEY       "/apps/metacity/general/visual_bell_type"
#define GST_GCONF_DIR              "/system/gstreamer/0.10"

#define AUDIO_TEST_SOURCE          "audiotestsrc wave=sine freq=512"
#define VIDEO_TEST_SOURCE          "videotestsrc"
#define AUDIO_TEST_IN_BETWEEN      " ! audioconvert ! audioresample ! "
#define VIDEO_TEST_IN_BETWEEN      " ! ffmpegcolorspace ! "

/* Capplet-specific prototypes */

static SoundProperties *props = NULL;
static GConfClient     *gconf_client = NULL;
static GtkWidget       *dialog_win = NULL;
static GList           *device_choosers = NULL;

static gboolean
CheckXKB (void)
{
	gboolean have_xkb = FALSE;
	Display *dpy;
	int opcode, errorBase, major, minor, xkbEventBase;

	gdk_error_trap_push ();
	dpy = GDK_DISPLAY ();
	have_xkb = XkbQueryExtension (dpy, &opcode, &xkbEventBase, 
				      &errorBase, &major, &minor)
		   && XkbUseExtension (dpy, &major, &minor);
	XSync (dpy, FALSE);
	gdk_error_trap_pop ();

	return have_xkb;
}

static void
props_changed_cb (SoundProperties *p, SoundEvent *event, gpointer data)
{
	sound_properties_user_save (p);
}



static GConfEnumStringPair bell_flash_enums[] = {
	{ 0, "frame_flash" },
	{ 1, "fullscreen" },
	{ -1, NULL }
};

static GConfValue *
bell_flash_from_widget (GConfPropertyEditor *peditor, const GConfValue *value)
{
	GConfValue *new_value;

	new_value = gconf_value_new (GCONF_VALUE_STRING);
	gconf_value_set_string (new_value,
				gconf_enum_to_string (bell_flash_enums, gconf_value_get_int (value)));

	return new_value;
}

static GConfValue *
bell_flash_to_widget (GConfPropertyEditor *peditor, const GConfValue *value)
{
	GConfValue *new_value;
	const gchar *str;
	gint val = 2;

	str = (value && (value->type == GCONF_VALUE_STRING)) ? gconf_value_get_string (value) : NULL;

	new_value = gconf_value_new (GCONF_VALUE_INT);
	if (value->type == GCONF_VALUE_STRING) {
		gconf_string_to_enum (bell_flash_enums,
				      str,
				      &val);
	}
	gconf_value_set_int (new_value, val);

	return new_value;
}

/* create_dialog
 *
 * Create the dialog box and return it as a GtkWidget
 */

static GladeXML *
create_dialog (void) 
{
	GladeXML *dialog;
	GtkWidget *widget, *box;

	dialog = glade_xml_new (GNOMECC_DATA_DIR "/interfaces/sound-properties.glade", "prefs_widget", NULL);
	widget = glade_xml_get_widget (dialog, "prefs_widget");
	g_object_set_data (G_OBJECT (widget), "glade-data", dialog);

	props = sound_properties_new ();
	sound_properties_add_defaults (props, NULL);
	g_signal_connect (G_OBJECT (props), "event_changed",
			  (GCallback) props_changed_cb, NULL);  
	box = glade_xml_get_widget (dialog, "events_vbox");
	gtk_box_pack_start (GTK_BOX (box), sound_view_new (props),
			    TRUE, TRUE, 0);

	g_signal_connect_swapped (G_OBJECT (widget), "destroy",
				  (GCallback) gtk_object_destroy, props);

	gtk_image_set_from_file (GTK_IMAGE (WID ("bell_image")),
				 GNOMECC_DATA_DIR "/pixmaps/visual-bell.png");

	gtk_widget_set_size_request (widget, 475, -1);

	if (!CheckXKB()) {
		GtkWidget *audible_bell_option = WID ("bell_audible_toggle");
		GtkWidget *visual_bell_option = WID ("bell_visual_toggle");

		gtk_widget_set_sensitive (audible_bell_option, FALSE);
		gtk_widget_set_sensitive (visual_bell_option, FALSE);
	}
		
	return dialog;
}

static gboolean
element_available (const gchar *pipeline)
{
	gboolean res = FALSE;
	gchar *p, *first_space;

	if (pipeline == NULL || *pipeline == '\0')
		return FALSE;

	p = g_strdup (pipeline);

	g_strstrip (p);

	/* skip the check and pretend all is fine if it's something that does
	 * not look like an element name (e.g. parentheses to signify a bin) */
	if (!g_ascii_isalpha (*p)) {
		g_free (p);
		return TRUE;
	}

	/* just the element name, no arguments */
	first_space = strchr (p, ' ');
	if (first_space != NULL)
		*first_space = '\0';

	/* check if element is available */
	res = gst_default_registry_check_feature_version (p, GST_VERSION_MAJOR,
		GST_VERSION_MINOR, 0);

	g_free (p);
	return res;
}
static void
add_device (int type, const gchar *pipeline, const gchar *description, const gchar *selection)
{
	GList *l;
	DeviceChooser *device_chooser;
	gchar *row_pipeline;
	gchar *description_mod;
	const gchar *suffix;
	gboolean selected, row_selected;
	gboolean found_pipeline, found_selected;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreeIter found_pipeline_iter = { };
	GtkTreeIter found_selected_iter = { };

	/* only display pipelines available on this system */	
	if (!element_available (pipeline))
		return;
	
	for (l = device_choosers; l != NULL; l = l->next) {
		device_chooser = (DeviceChooser *) l->data;
		
		if (device_chooser->type != type)
			continue;
		
		/* if we're adding/updating the currently selected device
		 * we're only interested in the DeviceChooser for the
		 * corresponding profile */
		if (selection != NULL && strcmp (selection, device_chooser->profile) != 0)
			continue;
			
		selected = (selection != NULL);
		
		found_pipeline = FALSE;
		found_selected = FALSE;
		
		model = GTK_TREE_MODEL (device_chooser->model);
		
		if (gtk_tree_model_get_iter_first (model, &iter)) {
			do {
				gtk_tree_model_get (model, &iter, 0, &row_pipeline, 2, &row_selected, -1);

				if (strcmp (row_pipeline, pipeline) == 0) {
					found_pipeline_iter = iter;
					found_pipeline = TRUE;
				}
				g_free (row_pipeline);

				if (row_selected) {
					found_selected_iter = iter;
					found_selected = TRUE;
				}

			} while (gtk_tree_model_iter_next (model, &iter));
		}
		
		if (found_pipeline) {
			/* the specified pipeline is already in the model */
			iter = found_pipeline_iter;
			if (!selected) {
				/* if the specified pipeline was only already in
				 * the model because it's currently selected,
				 * clear the corresponding flag in the model
				 * (happens when reconnecting usb audio devices) */
				gtk_list_store_set (device_chooser->model, &iter, 2, FALSE, -1);
			}
		} else if (selected && found_selected) {
			/* we only allow one custom pipeline in the model */
			iter = found_selected_iter;
		} else {
			/* no existing pipeline found that would be appropriate
			 * to reuse, so append a new row */
			gtk_list_store_append (device_chooser->model, &iter);
			gtk_list_store_set (device_chooser->model, &iter, 2, selected, -1);
		}
		
		gtk_list_store_set (device_chooser->model, &iter, 0, pipeline, -1);
		
		if (!selected || !found_pipeline) {
			/* either we're appending a new row or we're updating
			 * a row with non-appropriate description */
			 
			if (selected) {
				/* we add the specified pipeline only because
				 * it's the preferred device but it's neither
				 * a device currently detected by HAL nor a
				 * standard pipeline */
				if (g_str_has_prefix (pipeline, "hal")) {
					suffix = _("Not connected");
				} else {
					suffix = _("Custom");
				}
				description_mod = g_strdup_printf ("%s (%s)", description, suffix);
			} else {
				description_mod = g_strdup (description);
			}
			gtk_list_store_set (device_chooser->model, &iter, 1, description_mod, -1);
			
			/* we need to store the unmodified description to not
			 * modify already modified descriptions */
			gtk_list_store_set (device_chooser->model, &iter, 3, description, -1);
			
			g_free (description_mod);
		}
		
		if (selected) {
			gtk_combo_box_set_active_iter (GTK_COMBO_BOX (device_chooser->combobox), &iter);
		}
	}
}

static void
remove_device (int type, const gchar *pipeline)
{
	GList *l;
	DeviceChooser *device_chooser;
	GtkTreeIter iter;
	gchar *row_pipeline;
	gchar *description;
	gchar *description_mod;
	GtkTreeModel *model;
	
	for (l = device_choosers; l != NULL; l = l->next) {
		device_chooser = (DeviceChooser *) l->data;
		
		if (device_chooser->type != type)
			continue;
		
		model = GTK_TREE_MODEL (device_chooser->model);

		gtk_combo_box_get_active_iter (GTK_COMBO_BOX (device_chooser->combobox), &iter);
		gtk_tree_model_get (model, &iter, 0, &row_pipeline, -1);

		/* don't remove currently selected devices */
		if (strcmp (row_pipeline, pipeline) == 0) {
			g_free (row_pipeline);
			
			/* let the user know that they have to reconnect the
			 * sound device */
			gtk_tree_model_get (model, &iter, 3, &description, -1);
			description_mod = g_strdup_printf ("%s (%s)", description, _("Not connected"));
			g_free (description);
			gtk_list_store_set (device_chooser->model, &iter, 1, description_mod, -1);
			g_free (description_mod);
		} else {
			g_free (row_pipeline);

			if (gtk_tree_model_get_iter_first (model, &iter)) {
				do {
					gtk_tree_model_get (model, &iter, 0, &row_pipeline, -1);
					if (strcmp (row_pipeline, pipeline) == 0) {
						g_free (row_pipeline);
						gtk_list_store_remove (device_chooser->model, &iter);
						break;
					}
					g_free (row_pipeline);
				} while (gtk_tree_model_iter_next (model, &iter));
			}
		}
	}
}

#if USE_HAL
static void
device_added_callback (LibHalContext *ctx, const char *udi)
{
	DBusError error;
	gchar *type_string;
	int type;
	const gchar *element;
	gchar *pipeline, *description;
	
	dbus_error_init (&error);

	if (!libhal_device_query_capability (ctx, udi, "alsa", &error)) {
		dbus_error_free (&error);
		return;
	}

	type_string = libhal_device_get_property_string (ctx, udi, "alsa.type", &error);
	if (strcmp (type_string, "playback") == 0) {
		type = AUDIO_PLAYBACK;
		element = "halaudiosink";
	} else if (strcmp (type_string, "capture") == 0) {
		type = AUDIO_CAPTURE;
		element = "halaudiosrc";
	} else {
		type = -1;
		element = NULL;
	}
	libhal_free_string (type_string);
	if (type == -1) {
		dbus_error_free (&error);
		return;
	}
	
	pipeline = g_strdup_printf ("%s udi=%s", element, udi);
	description = libhal_device_get_property_string (ctx, udi, "alsa.device_id", &error);

	add_device (type, pipeline, description, NULL);

	g_free (pipeline);
	libhal_free_string (description);

	dbus_error_free (&error);
}

static void
device_removed_callback (LibHalContext *ctx, const char *udi)
{
	gchar *pipeline;
	
	pipeline = g_strdup_printf ("halaudiosink udi=%s", udi);
	remove_device (AUDIO_PLAYBACK, pipeline);
	g_free (pipeline);
	
	pipeline = g_strdup_printf ("halaudiosrc udi=%s", udi);
	remove_device (AUDIO_CAPTURE, pipeline);
	g_free (pipeline);
}

static void
setup_hal_devices ()
{
	DBusConnection *connection;
	DBusError error;
	LibHalContext *ctx;
	char **devices;
	int i, num = 0;

	dbus_error_init (&error);
	
	connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
	if (connection == NULL) {
		/* cannot get a dbus connection */
		if (dbus_error_is_set (&error)) {
			g_warning ("Getting a system dbus connection an error occured: %s", error.message);
			dbus_error_free (&error);
		}
		return;
	}
	
	dbus_connection_setup_with_g_main (connection, g_main_context_default ());
	
	ctx = libhal_ctx_new ();
	g_return_if_fail (ctx != NULL);
	
	libhal_ctx_set_device_added (ctx, device_added_callback);
	libhal_ctx_set_device_removed (ctx, device_removed_callback);
	libhal_ctx_set_dbus_connection (ctx, connection);

	if (!libhal_ctx_init (ctx, &error)) {
		/* cannot connect to hald */
		if (dbus_error_is_set (&error)) {
			g_warning ("Connecting to hald an error occured: %s", error.message);
			dbus_error_free (&error);
		}
		return;
	}

	devices = libhal_find_device_by_capability (ctx, "alsa", &num, &error);
	if (devices == NULL) {
		/* error in the libhal_find_device_by_capability function */
		if (dbus_error_is_set (&error)) {
			g_warning ("Calling a hal function an error occured: %s", error.message);
			dbus_error_free (&error);
		}
		return;
	}

	for (i = 0; i < num; i++) {
		device_added_callback (ctx, devices[i]);
	}
	
	dbus_free_string_array (devices);
}
#endif

static void
device_test_button_clicked (GtkWidget *button, gpointer user_data)
{
	DeviceChooser *device_chooser = (DeviceChooser *) user_data;
	
	GladeXML *dialog = glade_xml_new (GNOMECC_DATA_DIR "/interfaces/sound-properties.glade", NULL, NULL);

	user_test_pipeline (dialog, GTK_WINDOW (dialog_win), device_chooser->test_pipeline);
	
	g_object_unref (dialog);
}

static gchar *
get_gconf_key_for_profile (const gchar *profile, int type)
{
	const gchar *element_type;
	
	switch (type) {
		case AUDIO_PLAYBACK:
			element_type = "audiosink";
			break;
		case AUDIO_CAPTURE:
			element_type = "audiosrc";
			break;
		case VIDEO_PLAYBACK:
			element_type = "videosink";
			break;
		case VIDEO_CAPTURE:
			element_type = "videosrc";
			break;
		default:
			g_return_val_if_reached (NULL);
	}
	
	return g_strdup_printf ("%s/default/%s%s", GST_GCONF_DIR, profile, element_type);
}

static void
device_changed (GtkComboBox *widget, gpointer user_data)
{
	DeviceChooser *device_chooser = (DeviceChooser *) user_data;
	GtkTreeIter iter;
	gchar *pipeline, *description;
	gchar *base_key, *key;
	
	gtk_combo_box_get_active_iter (widget, &iter);
	gtk_tree_model_get (GTK_TREE_MODEL (device_chooser->model), &iter, 0, &pipeline, 3, &description, -1);
	
	base_key = get_gconf_key_for_profile (device_chooser->profile, device_chooser->type);
	gconf_client_set_string (gconf_client, base_key, pipeline, NULL);
	g_free (pipeline);
	key = g_strconcat (base_key, "_description", NULL);
	g_free (base_key);
	gconf_client_set_string (gconf_client, key, description, NULL);
	g_free (description);
	g_free (key);
}

static gchar *
get_gconf_description_for_profile (const gchar *profile, int type)
{
	gchar *base_key;
	gchar *key;
	gchar *description;
	
	base_key = get_gconf_key_for_profile (profile, type);
	key = g_strconcat (base_key, "_description", NULL);
	g_free (base_key);

	description = gconf_client_get_string (gconf_client, key, NULL);
	g_free (key);
	
	return description;
}

static void
gconf_key_changed (GConfClient *client, guint cnxn_id, GConfEntry *entry, gpointer data)
{
	DeviceChooser *device_chooser = (DeviceChooser *) data;
	gchar *description;
	
	description = get_gconf_description_for_profile (device_chooser->profile, device_chooser->type);
	
	add_device (device_chooser->type, gconf_value_get_string (gconf_entry_get_value (entry)), description, device_chooser->profile);
	
	g_free (description);
}

static void
setup_device_chooser (const gchar *profile, int type, GtkWidget *combobox, GtkWidget *test_button, const gchar *test_pipeline)
{
	DeviceChooser *device_chooser;
	GtkCellRenderer *cell;
	gchar *gconf_key;
	
	g_return_if_fail (GTK_IS_COMBO_BOX (combobox));
	g_return_if_fail (GTK_IS_BUTTON (test_button));
	
	device_chooser = g_malloc0 (sizeof (DeviceChooser));

	device_chooser->profile = profile;
	device_chooser->type = type;
	device_chooser->combobox = combobox;
	device_chooser->model = gtk_list_store_new (4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING);
	device_chooser->test_pipeline = test_pipeline;
	gtk_combo_box_set_model (GTK_COMBO_BOX (combobox), GTK_TREE_MODEL (device_chooser->model));
	cell = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combobox), cell, TRUE);
	gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (combobox), cell, "text", 1);
	
	device_choosers = g_list_prepend (device_choosers, device_chooser);
	
	gconf_key = get_gconf_key_for_profile (profile, type);
	gconf_client_notify_add (gconf_client, gconf_key, gconf_key_changed,
				 device_chooser, NULL, NULL);
	g_free (gconf_key);
	
	g_signal_connect (combobox, "changed", G_CALLBACK (device_changed), device_chooser);

	g_signal_connect (test_button, "clicked", G_CALLBACK (device_test_button_clicked), device_chooser);
}

static void
add_selected_device (const gchar *profile, int type)
{
	gchar *gconf_key;
	gchar *description;
	gchar *pipeline;

	gconf_key = get_gconf_key_for_profile (profile, type);
	pipeline = gconf_client_get_string (gconf_client, gconf_key, NULL);
	g_free (gconf_key);
	
	g_return_if_fail (pipeline != NULL);
	
	description = get_gconf_description_for_profile (profile, type);
	
	add_device (type, pipeline, description, profile);
	
	g_free (description);
	g_free (pipeline);
}

static void
setup_devices ()
{
	add_device (AUDIO_PLAYBACK, "autoaudiosink", _("Autodetect"), NULL);

#if USE_HAL
	setup_hal_devices ();
#endif
	add_device (AUDIO_PLAYBACK, "alsasink", _("ALSA - Advanced Linux Sound Architecture"), NULL);
	add_device (AUDIO_CAPTURE, "alsasrc", _("ALSA - Advanced Linux Sound Architecture"), NULL);
	add_device (AUDIO_PLAYBACK, "artsdsink", _("Artsd - ART Sound Daemon"), NULL);
	add_device (AUDIO_PLAYBACK, "esdsink", _("ESD - Enlightenment Sound Daemon"), NULL);
	add_device (AUDIO_CAPTURE, "esdmon", _("ESD - Enlightenment Sound Daemon"), NULL);
	add_device (AUDIO_PLAYBACK, "osssink", _("OSS - Open Sound System"), NULL);
	add_device (AUDIO_CAPTURE, "osssrc", _("OSS - Open Sound System"), NULL);
	add_device (AUDIO_PLAYBACK, "polypsink", _("Polypaudio Sound Server"), NULL);
	add_device (AUDIO_CAPTURE, "polypsrc", _("Polypaudio Sound Server"), NULL);
	add_device (AUDIO_CAPTURE, "audiotestsrc wave=triangle is-live=true", _("Test Sound"), NULL);
	add_device (AUDIO_CAPTURE, "audiotestsrc wave=silence is-live=true", _("Silence"), NULL);
	
	add_selected_device ("", AUDIO_PLAYBACK);
	add_selected_device ("music", AUDIO_PLAYBACK);
	add_selected_device ("chat", AUDIO_PLAYBACK);
	add_selected_device ("", AUDIO_CAPTURE);
}

/* setup_dialog
 *
 * Set up the property editors for our dialog
 */

static void
setup_dialog (GladeXML *dialog, GConfChangeSet *changeset) 
{
	GObject *peditor;
	GtkSizeGroup *combobox_size_group;
	GtkSizeGroup *label_size_group;

	gconf_client_add_dir (gconf_client, GST_GCONF_DIR,
			      GCONF_CLIENT_PRELOAD_RECURSIVE, NULL);

	combobox_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	label_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	gtk_size_group_add_widget (label_size_group, WID ("sounds_playback_label"));
	gtk_size_group_add_widget (label_size_group, WID ("music_playback_label"));
	gtk_size_group_add_widget (label_size_group, WID ("chat_audio_playback_label"));
	gtk_size_group_add_widget (label_size_group, WID ("chat_audio_capture_label"));
	gtk_size_group_add_widget (combobox_size_group, WID ("sounds_playback_device"));
	gtk_size_group_add_widget (combobox_size_group, WID ("music_playback_device"));
	gtk_size_group_add_widget (combobox_size_group, WID ("chat_audio_playback_device"));
	gtk_size_group_add_widget (combobox_size_group, WID ("chat_audio_capture_device"));

	setup_device_chooser ("", AUDIO_PLAYBACK, WID ("sounds_playback_device"),
			      WID ("sounds_playback_test"),
			      AUDIO_TEST_SOURCE AUDIO_TEST_IN_BETWEEN "gconfaudiosink");
	setup_device_chooser ("music", AUDIO_PLAYBACK, WID ("music_playback_device"),
			      WID ("music_playback_test"),
			      AUDIO_TEST_SOURCE AUDIO_TEST_IN_BETWEEN "gconfaudiosink profile=music");
	setup_device_chooser ("chat", AUDIO_PLAYBACK, WID ("chat_audio_playback_device"),
			      WID ("chat_audio_playback_test"),
			      AUDIO_TEST_SOURCE AUDIO_TEST_IN_BETWEEN "gconfaudiosink profile=chat");
	setup_device_chooser ("", AUDIO_CAPTURE, WID ("chat_audio_capture_device"),
			      WID ("chat_audio_capture_test"),
			      "gconfaudiosrc" AUDIO_TEST_IN_BETWEEN "gconfaudiosink profile=chat");

	peditor = gconf_peditor_new_boolean (NULL, ENABLE_ESD_KEY, WID ("enable_toggle"), NULL);
	gconf_peditor_widget_set_guard (GCONF_PROPERTY_EDITOR (peditor), WID ("events_toggle"));
	gconf_peditor_widget_set_guard (GCONF_PROPERTY_EDITOR (peditor), WID ("events_vbox"));

	gconf_peditor_new_boolean (NULL, EVENT_SOUNDS_KEY, WID ("events_toggle"), NULL);

	gconf_peditor_new_boolean (NULL, AUDIO_BELL_KEY, WID ("bell_audible_toggle"), NULL);

	peditor = gconf_peditor_new_boolean (NULL, VISUAL_BELL_KEY, WID ("bell_visual_toggle"), NULL);
	gconf_peditor_widget_set_guard (GCONF_PROPERTY_EDITOR (peditor), WID ("bell_flash_vbox"));

	/* peditor not so convenient for the radiobuttons */
	gconf_peditor_new_select_radio (NULL,
					VISUAL_BELL_TYPE_KEY,
					gtk_radio_button_get_group (GTK_RADIO_BUTTON (WID ("bell_flash_window_radio"))),
					"conv-to-widget-cb", bell_flash_to_widget,
					"conv-from-widget-cb", bell_flash_from_widget,
					NULL);
}

/* get_legacy_settings
 *
 * Retrieve older gnome_config -style settings and store them in the
 * configuration database.
 *
 * In most cases, it's best to use the COPY_FROM_LEGACY macro defined in
 * capplets/common/capplet-util.h.
 */

static void
get_legacy_settings (void) 
{
	GConfClient *client;
	gboolean val_bool, def;

	client = gconf_client_get_default ();
	COPY_FROM_LEGACY (bool, "/desktop/gnome/sound/enable_esd", "/sound/system/settings/start_esd=false");
	COPY_FROM_LEGACY (bool, "/desktop/gnome/sound/event_sounds", "/sound/system/settings/event_sounds=false");
	g_object_unref (G_OBJECT (client));
}

static void
dialog_button_clicked_cb (GtkDialog *dialog, gint response_id, GConfChangeSet *changeset) 
{
	if (response_id == GTK_RESPONSE_HELP)
		capplet_help (GTK_WINDOW (dialog),
			"user-guide.xml",
			"goscustmulti-2");
	else
		gtk_main_quit ();
}

int
main (int argc, char **argv) 
{
	GConfChangeSet *changeset;
	GladeXML       *dialog = NULL;

	static gboolean apply_only;
	static gboolean get_legacy;
	static struct poptOption cap_options[] = {
		{ "apply", '\0', POPT_ARG_NONE, &apply_only, 0,
		  N_("Just apply settings and quit (compatibility only; now handled by daemon)"), NULL },
		{ "init-session-settings", '\0', POPT_ARG_NONE, &apply_only, 0,
		  N_("Just apply settings and quit (compatibility only; now handled by daemon)"), NULL },
		{ "get-legacy", '\0', POPT_ARG_NONE, &get_legacy, 0,
		  N_("Retrieve and store legacy settings"), NULL },
		{ NULL, '\0', 0, NULL, 0, NULL, NULL }
	};

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gst_init (&argc, &argv);

	gnome_program_init ("gnome-sound-properties", VERSION,
			    LIBGNOMEUI_MODULE, argc, argv,
			    GNOME_PARAM_POPT_TABLE, cap_options,
			    NULL);

	activate_settings_daemon ();
	
	gconf_client = gconf_client_get_default ();
	gconf_client_add_dir (gconf_client, "/desktop/gnome/sound", GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);
	gconf_client_add_dir (gconf_client, "/apps/metacity/general", GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

	if (get_legacy) {
		get_legacy_settings ();
	} else {
		changeset = gconf_change_set_new ();
		dialog = create_dialog ();
		setup_dialog (dialog, changeset);
		setup_devices ();

		dialog_win = gtk_dialog_new_with_buttons
			(_("Sound Preferences"), NULL, GTK_DIALOG_NO_SEPARATOR,
			 GTK_STOCK_HELP, GTK_RESPONSE_HELP,
			 GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
			 NULL);

		gtk_container_set_border_width (GTK_CONTAINER (dialog_win), 5);
		gtk_box_set_spacing (GTK_BOX (GTK_DIALOG(dialog_win)->vbox), 2);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog_win), GTK_RESPONSE_CLOSE);
		g_signal_connect (G_OBJECT (dialog_win), "response", (GCallback) dialog_button_clicked_cb, changeset);
		gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog_win)->vbox), WID ("prefs_widget"), TRUE, TRUE, 0);
		capplet_set_icon (dialog_win, "gnome-settings-sound");
		gtk_widget_show_all (dialog_win);

		gtk_main ();
		gconf_change_set_unref (changeset);
	}
	
	g_object_unref (gconf_client);
	g_object_unref (dialog);
	return 0;
}
