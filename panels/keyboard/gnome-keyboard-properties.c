/* -*- mode: c; style: linux -*- */

/* keyboard-properties.c
 * Copyright (C) 2000-2001 Ximian, Inc.
 * Copyright (C) 2001 Jonathan Blandford
 *
 * Written by: Bradford Hovinen <hovinen@ximian.com>
 *             Rachel Hestilow <hestilow@ximian.com>
 *	       Jonathan Blandford <jrb@redhat.com>
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

#include <gconf/gconf-client.h>

#include "gconf-property-editor.h"

#include "gnome-keyboard-properties.h"
#include "gnome-keyboard-properties-a11y.h"
#include "gnome-keyboard-properties-xkb.h"

enum {
	RESPONSE_APPLY = 1,
	RESPONSE_CLOSE
};

static void
create_dialog (GtkBuilder * dialog)
{
	GtkSizeGroup *size_group;
	GtkWidget *image;

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	gtk_size_group_add_widget (size_group, WID ("repeat_slow_label"));
	gtk_size_group_add_widget (size_group, WID ("delay_short_label"));
	gtk_size_group_add_widget (size_group, WID ("blink_slow_label"));
	g_object_unref (G_OBJECT (size_group));

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	gtk_size_group_add_widget (size_group, WID ("repeat_fast_label"));
	gtk_size_group_add_widget (size_group, WID ("delay_long_label"));
	gtk_size_group_add_widget (size_group, WID ("blink_fast_label"));
	g_object_unref (G_OBJECT (size_group));

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	gtk_size_group_add_widget (size_group, WID ("repeat_delay_scale"));
	gtk_size_group_add_widget (size_group, WID ("repeat_speed_scale"));
	gtk_size_group_add_widget (size_group,
				   WID ("cursor_blink_time_scale"));
	g_object_unref (G_OBJECT (size_group));

	image =
	    gtk_image_new_from_stock (GTK_STOCK_ADD, GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image (GTK_BUTTON (WID ("xkb_layouts_add")), image);

	image =
	    gtk_image_new_from_stock (GTK_STOCK_REFRESH,
				      GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image (GTK_BUTTON (WID ("xkb_reset_to_defaults")),
			      image);
}

static GConfValue *
blink_from_widget (GConfPropertyEditor * peditor, const GConfValue * value)
{
	GConfValue *new_value;

	new_value = gconf_value_new (GCONF_VALUE_INT);
	gconf_value_set_int (new_value,
			     2600 - gconf_value_get_int (value));

	return new_value;
}

static GConfValue *
blink_to_widget (GConfPropertyEditor * peditor, const GConfValue * value)
{
	GConfValue *new_value;
	gint current_rate;

	current_rate = gconf_value_get_int (value);
	new_value = gconf_value_new (GCONF_VALUE_INT);
	gconf_value_set_int (new_value,
			     CLAMP (2600 - current_rate, 100, 2500));

	return new_value;
}

static void
dialog_response (GtkWidget * widget,
		 gint response_id, GConfChangeSet * changeset)
{
/*
	if (response_id == GTK_RESPONSE_HELP)
		capplet_help (GTK_WINDOW (widget), "goscustperiph-2");
	else
		gtk_main_quit ();
*/
}

static void
setup_dialog (GtkBuilder * dialog, GConfChangeSet * changeset)
{
	GObject *peditor;
	gchar *monitor;

	peditor = gconf_peditor_new_boolean
	    (changeset, "/desktop/gnome/peripherals/keyboard/repeat",
	     WID ("repeat_toggle"), NULL);
	gconf_peditor_widget_set_guard (GCONF_PROPERTY_EDITOR (peditor),
					WID ("repeat_table"));

	gconf_peditor_new_numeric_range
	    (changeset, "/desktop/gnome/peripherals/keyboard/delay",
	     WID ("repeat_delay_scale"), NULL);

	gconf_peditor_new_numeric_range
	    (changeset, "/desktop/gnome/peripherals/keyboard/rate",
	     WID ("repeat_speed_scale"), NULL);

	peditor = gconf_peditor_new_boolean
	    (changeset, "/desktop/gnome/interface/cursor_blink",
	     WID ("cursor_toggle"), NULL);
	gconf_peditor_widget_set_guard (GCONF_PROPERTY_EDITOR (peditor),
					WID ("cursor_hbox"));
	gconf_peditor_new_numeric_range (changeset,
					 "/desktop/gnome/interface/cursor_blink_time",
					 WID ("cursor_blink_time_scale"),
					 "conv-to-widget-cb",
					 blink_to_widget,
					 "conv-from-widget-cb",
					 blink_from_widget, NULL);

	/* Ergonomics */
	monitor = g_find_program_in_path ("gnome-typing-monitor");
	if (monitor != NULL) {
		g_free (monitor);

		peditor = gconf_peditor_new_boolean
		    (changeset, "/desktop/gnome/typing_break/enabled",
		     WID ("break_enabled_toggle"), NULL);
		gconf_peditor_widget_set_guard (GCONF_PROPERTY_EDITOR
						(peditor),
						WID
						("break_details_table"));
		gconf_peditor_new_numeric_range (changeset,
						 "/desktop/gnome/typing_break/type_time",
						 WID
						 ("break_enabled_spin"),
						 NULL);
		gconf_peditor_new_numeric_range (changeset,
						 "/desktop/gnome/typing_break/break_time",
						 WID
						 ("break_interval_spin"),
						 NULL);
		gconf_peditor_new_boolean (changeset,
					   "/desktop/gnome/typing_break/allow_postpone",
					   WID
					   ("break_postponement_toggle"),
					   NULL);

	} else {
		/* don't show the typing break tab if the daemon is not available */
		GtkNotebook *nb = GTK_NOTEBOOK (WID ("keyboard_notebook"));
		gint tb_page =
		    gtk_notebook_page_num (nb,
					   WID ("break_enabled_toggle"));
		gtk_notebook_remove_page (nb, tb_page);
	}

	g_signal_connect (WID ("keyboard_dialog"), "response",
			  (GCallback) dialog_response, changeset);

	setup_xkb_tabs (dialog, changeset);
	setup_a11y_tabs (dialog, changeset);
}


GtkWidget *
gnome_keyboard_properties_init (GConfClient * client, GtkBuilder * dialog)
{
	GtkWidget *dialog_win = NULL;

	gconf_client_add_dir (client,
			      "/desktop/gnome/peripherals/keyboard",
			      GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);
	gconf_client_add_dir (client, "/desktop/gnome/interface",
			      GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);
	create_dialog (dialog);
	if (dialog) {
		setup_dialog (dialog, NULL);
		dialog_win = WID ("keyboard_dialog");
		/* g_signal_connect (dialog_win, "response",
		   G_CALLBACK (dialog_response_cb), NULL); */
	}

	return dialog_win;
}
