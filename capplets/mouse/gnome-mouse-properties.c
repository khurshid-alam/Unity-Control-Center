/* -*- mode: c; style: linux -*- */

/* mouse-properties-capplet.c
 * Copyright (C) 2001 Red Hat, Inc.
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Written by: Jonathon Blandford <jrb@redhat.com>,
 *             Bradford Hovinen <hovinen@ximian.com>,
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
#include <gdk/gdkx.h>
#include <math.h>

#include "capplet-util.h"
#include "gconf-property-editor.h"
#include "activate-settings-daemon.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

/******************************************************************************/
/* A quick custom widget to ensure that the left handed toggle works no matter
 * which button is pressed.
 */
typedef struct { GtkCheckButton parent; } MouseCappletCheckButton;
typedef struct { GtkCheckButtonClass parent; } MouseCappletCheckButtonClass;
GNOME_CLASS_BOILERPLATE (MouseCappletCheckButton, mouse_capplet_check_button,
			 GtkCheckButton, GTK_TYPE_CHECK_BUTTON)
static void mouse_capplet_check_button_instance_init (MouseCappletCheckButton *obj) { }

static gboolean
mouse_capplet_check_button_button_press (GtkWidget *widget, GdkEventButton *event)
{
	if (event->type == GDK_BUTTON_PRESS) {
		if (!GTK_WIDGET_HAS_FOCUS (widget))
			gtk_widget_grab_focus (widget);
		gtk_button_pressed (GTK_BUTTON (widget));
	}
	return TRUE;
}
static gboolean
mouse_capplet_check_button_button_release (GtkWidget *widget, GdkEventButton *event)
{
      gtk_button_released (GTK_BUTTON (widget));
      return TRUE;
}

static void
mouse_capplet_check_button_class_init (MouseCappletCheckButtonClass *klass)
{
	GtkWidgetClass *widget_class = (GtkWidgetClass *)klass;
	widget_class->button_press_event   = mouse_capplet_check_button_button_press;
	widget_class->button_release_event = mouse_capplet_check_button_button_release;
}
/******************************************************************************/

enum
{
	DOUBLE_CLICK_TEST_OFF,
	DOUBLE_CLICK_TEST_MAYBE,
	DOUBLE_CLICK_TEST_ON,
};

enum
{
	COLUMN_PIXBUF,
	COLUMN_TEXT,
	COLUMN_FONT_PATH,
	N_COLUMNS
};

/* We use this in at least half a dozen places, so it makes sense just to
 * define the macro */

#define DOUBLE_CLICK_KEY "/desktop/gnome/peripherals/mouse/double_click"
#define CURSOR_FONT_KEY "/desktop/gnome/peripherals/mouse/cursor_font"
/* Write-once data; global for convenience. Set only by load_pixbufs */

GdkPixbuf *left_handed_pixbuf;
GdkPixbuf *right_handed_pixbuf;
GdkPixbuf *double_click_on_pixbuf;
GdkPixbuf *double_click_maybe_pixbuf;
GdkPixbuf *double_click_off_pixbuf;
GConfClient *client;
/* State in testing the double-click speed. Global for a great deal of
 * convenience
 */
gint double_click_state = DOUBLE_CLICK_TEST_OFF;

/* normalilzation routines */
/* All of our scales but double_click are on the range 1->10 as a result, we
 * have a few routines to convert from whatever the gconf key is to our range.
 */
static GConfValue *
double_click_from_gconf (GConfPropertyEditor *peditor, const GConfValue *value)
{
	GConfValue *new_value;

	new_value = gconf_value_new (GCONF_VALUE_FLOAT);
	gconf_value_set_float (new_value, CLAMP (floor ((gconf_value_get_int (value) + 50) / 100) * 100, 0, 1000) / 1000.0);
	return new_value;
}

static GConfValue *
double_click_to_gconf (GConfPropertyEditor *peditor, const GConfValue *value)
{
	GConfValue *new_value;

	new_value = gconf_value_new (GCONF_VALUE_INT);
	gconf_value_set_int (new_value, gconf_value_get_float (value) * 1000.0);
	return new_value;
}


static void
get_default_mouse_info (int *default_numerator, int *default_denominator, int *default_threshold)
{
	int numerator, denominator;
	int threshold;
	int tmp_num, tmp_den, tmp_threshold;
	
	/* Query X for the default value */
	XGetPointerControl (GDK_DISPLAY (), &numerator, &denominator,
			    &threshold);
	XChangePointerControl (GDK_DISPLAY (), True, True, -1, -1, -1);
	XGetPointerControl (GDK_DISPLAY (), &tmp_num, &tmp_den, &tmp_threshold);
	XChangePointerControl (GDK_DISPLAY (), True, True, numerator, denominator, threshold);

	if (default_numerator)
		*default_numerator = tmp_num;

	if (default_denominator)
		*default_denominator = tmp_den;

	if (default_threshold)
		*default_threshold = tmp_threshold;

}

static GConfValue *
motion_acceleration_from_gconf (GConfPropertyEditor *peditor, 
				const GConfValue *value)
{
	GConfValue *new_value;
	gfloat motion_acceleration;

	new_value = gconf_value_new (GCONF_VALUE_FLOAT);
	
	if (gconf_value_get_float (value) == -1.0) {
		int numerator, denominator;
		
		get_default_mouse_info (&numerator, &denominator, NULL);

		motion_acceleration = CLAMP ((gfloat)(numerator / denominator), 0.2, 6.0);
	}
	else {
		motion_acceleration = CLAMP (gconf_value_get_float (value), 0.2, 6.0);
	}

	if (motion_acceleration >= 1)
		gconf_value_set_float (new_value, motion_acceleration + 4);
	else
		gconf_value_set_float (new_value, motion_acceleration * 5);

	return new_value;
}

static GConfValue *
motion_acceleration_to_gconf (GConfPropertyEditor *peditor, 
			      const GConfValue *value)
{
	GConfValue *new_value;
	gfloat motion_acceleration;

	new_value = gconf_value_new (GCONF_VALUE_FLOAT);
	motion_acceleration = CLAMP (gconf_value_get_float (value), 1.0, 10.0);

	if (motion_acceleration < 5)
		gconf_value_set_float (new_value, motion_acceleration / 5.0);
	else
		gconf_value_set_float (new_value, motion_acceleration - 4);

	return new_value;
}

static GConfValue *
threshold_from_gconf (GConfPropertyEditor *peditor, 
		      const GConfValue *value)
{
	GConfValue *new_value;

	new_value = gconf_value_new (GCONF_VALUE_FLOAT);

	if (gconf_value_get_int (value) == -1) {
		int threshold;

		get_default_mouse_info (NULL, NULL, &threshold);
		gconf_value_set_float (new_value, CLAMP (threshold, 1, 10));
	}
	else {
		gconf_value_set_float (new_value, CLAMP (gconf_value_get_int (value), 1, 10));
	}

	return new_value;
}

static GConfValue *
drag_threshold_from_gconf (GConfPropertyEditor *peditor, 
			   const GConfValue *value)
{
	GConfValue *new_value;

	new_value = gconf_value_new (GCONF_VALUE_FLOAT);

	gconf_value_set_float (new_value, CLAMP (gconf_value_get_int (value), 1, 10));

	return new_value;
}

/* Retrieve legacy settings */

static void
get_legacy_settings (void) 
{
}

/* Double Click handling */

struct test_data_t 
{
	gint *timeout_id;
	GtkWidget *image;
};

/* Timeout for the double click test */

static gboolean
test_maybe_timeout (struct test_data_t *data)
{
	double_click_state = DOUBLE_CLICK_TEST_OFF;
	gtk_image_set_from_pixbuf (GTK_IMAGE (data->image), double_click_off_pixbuf);

	*data->timeout_id = 0;

	return FALSE;
}

/* Callback issued when the user clicks the double click testing area. */

static gboolean
event_box_button_press_event (GtkWidget   *widget,
			      GdkEventButton *event,
			      GConfChangeSet *changeset)
{
	gint                       double_click_time;
	GConfValue                *value;
	static struct test_data_t  data;
	static guint               test_on_timeout_id     = 0;
	static guint               test_maybe_timeout_id  = 0;
	static guint32             double_click_timestamp = 0;
	GtkWidget                 *image;
	GdkPixbuf                 *pixbuf = NULL;
	
	if (event->type != GDK_BUTTON_PRESS)
		return FALSE;

	image = g_object_get_data (G_OBJECT (widget), "image");
	
	if (!(changeset && gconf_change_set_check_value (changeset, DOUBLE_CLICK_KEY, &value))) 
		double_click_time = gconf_client_get_int (gconf_client_get_default (), DOUBLE_CLICK_KEY, NULL);
	else
		double_click_time = gconf_value_get_int (value);

	if (test_maybe_timeout_id != 0)
		gtk_timeout_remove  (test_maybe_timeout_id);
	if (test_on_timeout_id != 0)
		gtk_timeout_remove (test_on_timeout_id);

	switch (double_click_state) {
	case DOUBLE_CLICK_TEST_OFF:
		double_click_state = DOUBLE_CLICK_TEST_MAYBE;
		data.image = image;
		data.timeout_id = &test_maybe_timeout_id;
		test_maybe_timeout_id = gtk_timeout_add (double_click_time, (GtkFunction) test_maybe_timeout, &data);
		break;
	case DOUBLE_CLICK_TEST_MAYBE:
		if (event->time - double_click_timestamp < double_click_time) {
			double_click_state = DOUBLE_CLICK_TEST_ON;
			data.image = image;
			data.timeout_id = &test_on_timeout_id;
			test_on_timeout_id = gtk_timeout_add (2500, (GtkFunction) test_maybe_timeout, &data);
		}
		break;
	case DOUBLE_CLICK_TEST_ON:
		double_click_state = DOUBLE_CLICK_TEST_OFF;
		break;
	}

	double_click_timestamp = event->time;

	switch (double_click_state) {
	case DOUBLE_CLICK_TEST_ON:
		pixbuf = double_click_on_pixbuf;
		break;
	case DOUBLE_CLICK_TEST_MAYBE:
		pixbuf = double_click_maybe_pixbuf;
		break;
	case DOUBLE_CLICK_TEST_OFF:
		pixbuf = double_click_off_pixbuf;
		break;
	}

	gtk_image_set_from_pixbuf (GTK_IMAGE (image), pixbuf);

	return TRUE;
}

/* Callback issued when the user switches between left- and right-handed button
 * mappings. Updates the appropriate image on the dialog.
 */

static void
left_handed_toggle_cb (GConfPropertyEditor *peditor, const gchar *key, const GConfValue *value, GtkWidget *image)
{
	if (value && gconf_value_get_bool (value))
		g_object_set (G_OBJECT (image),
			      "pixbuf", left_handed_pixbuf,
			      NULL);
	else
		g_object_set (G_OBJECT (image),
			      "pixbuf", right_handed_pixbuf,
			      NULL);
}

/* Load the pixbufs we are going to use */

static void
load_pixbufs (void) 
{
	static gboolean called = FALSE;
	gchar *filename;
	GnomeProgram *program;

	if (called) return;

	program = gnome_program_get ();

	filename = gnome_program_locate_file (program, GNOME_FILE_DOMAIN_APP_PIXMAP, "mouse-left.png", TRUE, NULL);
	left_handed_pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
	g_free (filename);

	filename = gnome_program_locate_file (program, GNOME_FILE_DOMAIN_APP_PIXMAP, "mouse-right.png", TRUE, NULL);
	right_handed_pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
	g_free (filename);

	filename = gnome_program_locate_file (program, GNOME_FILE_DOMAIN_APP_PIXMAP, "double-click-on.png", TRUE, NULL);
	double_click_on_pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
	g_free (filename);

	filename = gnome_program_locate_file (program, GNOME_FILE_DOMAIN_APP_PIXMAP, "double-click-maybe.png", TRUE, NULL);
	double_click_maybe_pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
	g_free (filename);

	filename = gnome_program_locate_file (program, GNOME_FILE_DOMAIN_APP_PIXMAP, "double-click-off.png", TRUE, NULL);
	double_click_off_pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
	g_free (filename);

	called = TRUE;
}

static gchar *
read_cursor_font (void)
{
	DIR *dir;
	gchar *dir_name;
	struct dirent *file_dirent;

	dir_name = g_build_path (G_DIR_SEPARATOR_S, g_get_home_dir (), ".gnome2/share/cursor-fonts", NULL);
	if (! g_file_test (dir_name, G_FILE_TEST_EXISTS))
		return NULL;

	dir = opendir (dir_name);
  
	while ((file_dirent = readdir (dir)) != NULL) {
		struct stat st;
		gchar *link_name;

		link_name = g_build_filename (dir_name, file_dirent->d_name, NULL);
		if (lstat (link_name, &st)) {
			g_free (link_name);
			continue;
		}
	  
		if (S_ISLNK (st.st_mode)) {
			gint length;
			gchar target[256];

			length = readlink (link_name, target, 255);
			if (length > 0) {
				gchar *retval;
				target[length] = '\0';
				retval = g_strdup (target);
				g_free (link_name);
				return retval;
			}
			
		}
		g_free (link_name);
	}
	
	return NULL;
}

static void
cursor_font_changed (GConfClient *client,
		     guint        cnxn_id,
		     GConfEntry  *entry,
		     gpointer     user_data)
{
	GtkTreeView *tree_view;
	gchar *cursor_font;
	gchar *cursor_text;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeSelection *selection;

	tree_view = GTK_TREE_VIEW (user_data);
	selection = gtk_tree_view_get_selection (tree_view);
	model = gtk_tree_view_get_model (tree_view);

	cursor_font = gconf_client_get_string (client, CURSOR_FONT_KEY, NULL);
	gtk_tree_model_get_iter_root (model, &iter);

	do {
		gchar *temp_cursor_font;
		gtk_tree_model_get (model, &iter,
				    COLUMN_FONT_PATH, &temp_cursor_font,
				    -1);
		if ((temp_cursor_font == NULL && cursor_font == NULL) ||
		    ((temp_cursor_font != NULL && cursor_font != NULL) &&
		     (!strcmp (cursor_font, temp_cursor_font)))) {
			if (!gtk_tree_selection_iter_is_selected (selection, &iter))
				gtk_tree_selection_select_iter (selection, &iter);
			g_free (temp_cursor_font);
			g_free (cursor_font);
			return;
		}
		g_free (temp_cursor_font);
	} while (gtk_tree_model_iter_next (model, &iter));

	/* we didn't find it; we add it to the end. */
	gtk_list_store_append (GTK_LIST_STORE (model), &iter);
	cursor_text = g_strdup_printf (_("<b>Unknown Cursor</b>\n%s"), cursor_font);
	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    COLUMN_TEXT, cursor_text,
			    COLUMN_FONT_PATH, cursor_font,
			    -1);
	gtk_tree_selection_select_iter (selection, &iter);

	g_free (cursor_font);
	g_free (cursor_text);
}

static void
cursor_changed (GtkTreeSelection *selection,
		gpointer          data)
{
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	gchar *cursor_font = NULL;

	if (! gtk_tree_selection_get_selected (selection, &model, &iter))
		return;

	gtk_tree_model_get (model, &iter,
			    COLUMN_FONT_PATH, &cursor_font,
			    -1);
	if (cursor_font != NULL)
		gconf_client_set_string (gconf_client_get_default (),
					 CURSOR_FONT_KEY, cursor_font, NULL);
	else
		gconf_client_unset (gconf_client_get_default (),
				    CURSOR_FONT_KEY, NULL);
}

/* Set up the property editors in the dialog. */
static void
setup_dialog (GladeXML *dialog, GConfChangeSet *changeset)
{
	GObject           *peditor;
	GtkWidget         *tree_view;
	GtkTreeSelection  *selection;
	GtkTreeModel      *model;
	GtkCellRenderer   *renderer;
	GtkTreeViewColumn *column;
	GtkTreeIter        iter;
	GConfValue        *value;
	gchar             *filename;
	GdkPixbuf         *pixbuf;
	GnomeProgram      *program;
	gchar             *cursor_font;
	gchar             *font_path;
	gchar             *cursor_string;
	gboolean           found_default;

	program = gnome_program_get ();
	found_default = FALSE;
	
	/* Buttons page */
	/* Left-handed toggle */
	peditor = gconf_peditor_new_boolean
		(changeset, "/desktop/gnome/peripherals/mouse/left_handed", WID ("left_handed_toggle"), NULL);
	g_signal_connect (peditor, "value-changed", (GCallback) left_handed_toggle_cb, WID ("orientation_image"));

	/* Make sure the image gets initialized correctly */
	value = gconf_client_get (gconf_client_get_default (), "/desktop/gnome/peripherals/mouse/left_handed", NULL);
	left_handed_toggle_cb (GCONF_PROPERTY_EDITOR (peditor), NULL, value, WID ("orientation_image"));
	gconf_value_free (value);

	/* Double-click time */
	gtk_image_set_from_pixbuf (GTK_IMAGE (WID ("double_click_image")), double_click_off_pixbuf);
	g_object_set_data (G_OBJECT (WID ("double_click_eventbox")), "image", WID ("double_click_image"));
	g_signal_connect (WID ("double_click_eventbox"), "button_press_event",
			  G_CALLBACK (event_box_button_press_event), changeset);
	
	/* Cursors page */
	tree_view = WID ("cursor_tree");
	cursor_font = read_cursor_font ();

	model = (GtkTreeModel *) gtk_list_store_new (N_COLUMNS, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING);
	gtk_tree_view_set_model (GTK_TREE_VIEW (tree_view), model);
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	g_signal_connect (G_OBJECT (selection), "changed", G_CALLBACK (cursor_changed), NULL);
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (column, renderer,
					     "pixbuf", COLUMN_PIXBUF,
					     NULL);
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_attributes (column, renderer,
					     "markup", COLUMN_TEXT,
					     NULL);

	/* Default cursor */
	filename = gnome_program_locate_file (program, GNOME_FILE_DOMAIN_APP_PIXMAP, "mouse-cursor-normal.png", TRUE, NULL);
	pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
	g_free (filename);

	gtk_list_store_append (GTK_LIST_STORE (model), &iter);
	if (cursor_font == NULL) {
		cursor_string = _("<b>Default Cursor - Current</b>\nThe default cursor that ships with X");
		found_default = TRUE;
	} else {
		cursor_string = _("<b>Default Cursor</b>\nThe default cursor that ships with X");
	}

	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    COLUMN_PIXBUF, pixbuf,
			    COLUMN_TEXT, cursor_string,
			    COLUMN_FONT_PATH, NULL,
			    -1);


	/* Inverted cursor */
	filename = gnome_program_locate_file (program, GNOME_FILE_DOMAIN_APP_PIXMAP, "mouse-cursor-white.png", TRUE, NULL);
	pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
	g_free (filename);
	font_path = gnome_program_locate_file (program, GNOME_FILE_DOMAIN_DATADIR, "gnome/cursor-fonts/cursor-white.pcf", FALSE, NULL);

	gtk_list_store_append (GTK_LIST_STORE (model), &iter);
	if (cursor_font && ! strcmp (cursor_font, font_path)) {
		cursor_string = _("<b>White Cursor - Current</b>\nThe default cursor inverted");
		found_default = TRUE;
	} else {
		cursor_string = _("<b>White Cursor</b>\nThe default cursor inverted");
	}

	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    COLUMN_PIXBUF, pixbuf,
			    COLUMN_TEXT, cursor_string,
			    COLUMN_FONT_PATH, font_path,
			    -1);
	g_free (font_path);
	
	/* Large cursor */
	filename = gnome_program_locate_file (program, GNOME_FILE_DOMAIN_APP_PIXMAP, "mouse-cursor-normal-large.png", TRUE, NULL);
	pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
	g_free (filename);
	font_path = gnome_program_locate_file (program, GNOME_FILE_DOMAIN_DATADIR, "gnome/cursor-fonts/cursor-large.pcf", FALSE, NULL);

	gtk_list_store_append (GTK_LIST_STORE (model), &iter);
	if (cursor_font && ! strcmp (cursor_font, font_path)) {
		cursor_string = _("<b>Large Cursor - Current</b>\nLarge version of normal cursor");
		found_default = TRUE;
	} else {
		cursor_string = _("<b>Large Cursor</b>\nLarge version of normal cursor");
	}

	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    COLUMN_PIXBUF, pixbuf,
			    COLUMN_TEXT, cursor_string,
			    COLUMN_FONT_PATH, font_path,
			    -1);
	g_free (font_path);

	/* Large inverted cursor */
	filename = gnome_program_locate_file (program, GNOME_FILE_DOMAIN_APP_PIXMAP, "mouse-cursor-white-large.png", TRUE, NULL);
	pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
	g_free (filename);
	font_path = gnome_program_locate_file (program, GNOME_FILE_DOMAIN_DATADIR, "gnome/cursor-fonts/cursor-large-white.pcf", FALSE, NULL);

	gtk_list_store_append (GTK_LIST_STORE (model), &iter);
	if (cursor_font && ! strcmp (cursor_font, font_path)) {
		cursor_string = _("<b>Large White Cursor - Current</b>\nLarge version of white cursor");
		found_default = TRUE;
	} else {
		cursor_string = _("<b>Large White Cursor</b>\nLarge version of white cursor");
	}

	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    COLUMN_PIXBUF, pixbuf,
			    COLUMN_TEXT, cursor_string,
			    COLUMN_FONT_PATH, font_path,
			    -1);
	g_free (font_path);

	gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), column);
	
	gconf_peditor_new_boolean
		(changeset, "/desktop/gnome/peripherals/mouse/locate_pointer", WID ("locate_pointer_toggle"), NULL);
	/* Motion page */
	/* speed */
	gconf_peditor_new_numeric_range
		(changeset, DOUBLE_CLICK_KEY, WID ("delay_scale"),
		 "conv-to-widget-cb", double_click_from_gconf,
		 "conv-from-widget-cb", double_click_to_gconf,
		 NULL);

	gconf_peditor_new_numeric_range
		(changeset, "/desktop/gnome/peripherals/mouse/motion_acceleration", WID ("accel_scale"),
		 "conv-to-widget-cb", motion_acceleration_from_gconf,
		 "conv-from-widget-cb", motion_acceleration_to_gconf,
		 NULL);

	gconf_peditor_new_numeric_range
		(changeset, "/desktop/gnome/peripherals/mouse/motion_threshold", WID ("sensitivity_scale"),
		 "conv-to-widget-cb", threshold_from_gconf,
		 "conv-from-widget-cb", gconf_value_float_to_int,
		 NULL);

	/* DnD threshold */
	gconf_peditor_new_numeric_range
		(changeset, "/desktop/gnome/peripherals/mouse/drag_threshold", WID ("drag_threshold_scale"),
		 "conv-to-widget-cb", drag_threshold_from_gconf,
		 "conv-from-widget-cb", gconf_value_float_to_int,
		 NULL);

	/* listen to cursors changing */
        gconf_client_notify_add (gconf_client_get_default (),
				 CURSOR_FONT_KEY,
				 cursor_font_changed,
				 tree_view, NULL, NULL);

	/* and set it up initially... */
	cursor_font_changed (gconf_client_get_default (), 0, NULL, tree_view);
}

/* Construct the dialog */

static GladeXML *
create_dialog (void) 
{
	GtkWidget    *widget;
	GladeXML     *dialog;
	GtkSizeGroup *size_group;

	/* register the custom type */
	(void) mouse_capplet_check_button_get_type ();

	dialog = glade_xml_new (GNOMECC_DATA_DIR "/interfaces/gnome-mouse-properties.glade", "mouse_properties_dialog", NULL);
	widget = glade_xml_get_widget (dialog, "prefs_widget");

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	gtk_size_group_add_widget (size_group, WID ("acceleration_label"));
	gtk_size_group_add_widget (size_group, WID ("sensitivity_label"));
	gtk_size_group_add_widget (size_group, WID ("threshold_label"));

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	gtk_size_group_add_widget (size_group, WID ("high_label"));
	gtk_size_group_add_widget (size_group, WID ("fast_label"));
	gtk_size_group_add_widget (size_group, WID ("large_label"));

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	gtk_size_group_add_widget (size_group, WID ("low_label"));
	gtk_size_group_add_widget (size_group, WID ("slow_label"));
	gtk_size_group_add_widget (size_group, WID ("small_label"));

	return dialog;
}

/* Callback issued when a button is clicked on the dialog */

static void
dialog_response_cb (GtkDialog *dialog, gint response_id, GConfChangeSet *changeset) 
{
	if (response_id == GTK_RESPONSE_HELP)
		capplet_help (GTK_WINDOW (dialog),
			"wgoscustdesk.xml",
			"goscustperiph-5");
	else
		gtk_main_quit ();
}

int
main (int argc, char **argv)
{
	GConfClient    *client;
	GConfChangeSet *changeset;
	GladeXML       *dialog;
	GtkWidget      *dialog_win;

	static gboolean get_legacy;
	static struct poptOption cap_options[] = {
		{ "get-legacy", '\0', POPT_ARG_NONE, &get_legacy, 0,
		  N_("Retrieve and store legacy settings"), NULL },
		{ NULL, '\0', 0, NULL, 0, NULL, NULL }
	};

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gnome_program_init ("gnome-mouse-properties", VERSION,
			    LIBGNOMEUI_MODULE, argc, argv,
			    GNOME_PARAM_POPT_TABLE, cap_options,
			    GNOME_PARAM_APP_DATADIR, GNOMECC_DATA_DIR,
			    NULL);

	activate_settings_daemon ();

	client = gconf_client_get_default ();
	gconf_client_add_dir (client, "/desktop/gnome/peripherals/mouse", GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

	if (get_legacy) {
		get_legacy_settings ();
	} else {
		changeset = NULL;
		dialog = create_dialog ();
		load_pixbufs ();
		setup_dialog (dialog, changeset);

		dialog_win = WID ("mouse_properties_dialog");
		g_signal_connect (dialog_win, "response",
				  G_CALLBACK (dialog_response_cb), changeset);

		capplet_set_icon (dialog_win, "mouse-capplet.png");
		gtk_widget_show_all (dialog_win);

		gtk_main ();
	}

	return 0;
}
