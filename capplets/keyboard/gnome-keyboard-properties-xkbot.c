/* -*- mode: c; style: linux -*- */

/* gnome-keyboard-properties-xkbot.c
 * Copyright (C) 2003-2007 Sergey V. Udaltsov
 *
 * Written by: Sergey V. Udaltsov <svu@gnome.org>
 *             John Spray <spray_john@users.sourceforge.net>
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
#include <glade/glade.h>

#include "capplet-util.h"

#include "gnome-keyboard-properties-xkb.h"

static GladeXML *chooser_dialog = NULL;
static const char *current1st_level_id = NULL;
static GtkWidget *current_vbox = NULL;
static GtkWidget *current_none_radio = NULL;
static gboolean current_multi_select = FALSE;
static GSList *current_radio_group = NULL;

#define OPTION_ID_PROP "optionID"
#define SELCOUNTER_PROP "selectionCounter"
#define GCONFSTATE_PROP "gconfState"
#define EXPANDERS_PROP "expandersList"

GSList *
xkb_options_get_selected_list (void)
{
	GSList *retval;

	retval = gconf_client_get_list (xkb_gconf_client,
					GKBD_KEYBOARD_CONFIG_KEY_OPTIONS,
					GCONF_VALUE_STRING, NULL);
	if (retval == NULL) {
		GSList *cur_option;

		for (cur_option = initial_config.options;
		     cur_option != NULL; cur_option = cur_option->next)
			retval =
			    g_slist_prepend (retval,
					     g_strdup (cur_option->data));

		retval = g_slist_reverse (retval);
	}

	return retval;
}

static GtkWidget *
xkb_options_get_expander (GtkWidget * option_button)
{
	return
	    gtk_widget_get_parent (gtk_widget_get_parent
				   (gtk_widget_get_parent
				    (option_button)));
}

static int
xkb_options_expander_selcounter_get (GtkWidget * expander)
{
	return
	    GPOINTER_TO_INT (g_object_get_data
			     (G_OBJECT (expander), SELCOUNTER_PROP));
}

static void
xkb_options_expander_selcounter_add (GtkWidget * expander, int value)
{
	g_object_set_data (G_OBJECT (expander), SELCOUNTER_PROP,
			   GINT_TO_POINTER
			   (xkb_options_expander_selcounter_get (expander)
			    + value));
}

static void
xkb_options_expander_selcounter_reset (GtkWidget * expander)
{
	g_object_set_data (G_OBJECT (expander), SELCOUNTER_PROP,
			   GINT_TO_POINTER (0));
}

static void
xkb_options_expander_highlight (GtkWidget * expander)
{
	char *utf_group_name =
	    g_object_get_data (G_OBJECT (expander), "utfGroupName");
	int counter = xkb_options_expander_selcounter_get (expander);
	if (utf_group_name != NULL) {
		gchar *titlemarkup =
		    g_strconcat (counter >
				 0 ? "<span weight=\"bold\">" : "<span>",
				 utf_group_name, "</span>", NULL);
		gtk_expander_set_label (GTK_EXPANDER (expander),
					titlemarkup);
		g_free (titlemarkup);
	}
}

/* Add optionname from the backend's selection list if it's not
   already in there. */
static void
xkb_options_select (gchar * optionname)
{
	gboolean already_selected = FALSE;
	GSList *options_list = xkb_options_get_selected_list ();
	GSList *option;
	for (option = options_list; option != NULL; option = option->next)
		if (!strcmp ((gchar *) option->data, optionname))
			already_selected = TRUE;

	if (!already_selected)
		options_list =
		    g_slist_append (options_list, g_strdup (optionname));
	xkb_options_set_selected_list (options_list);

	clear_xkb_elements_list (options_list);
}

/* Remove all occurences of optionname from the backend's selection list */
static void
xkb_options_deselect (gchar * optionname)
{
	GSList *options_list = xkb_options_get_selected_list ();
	GSList *nodetmp;
	GSList *option = options_list;
	while (option != NULL) {
		gchar *id = (char *) option->data;
		if (!strcmp (id, optionname)) {
			nodetmp = option->next;
			g_free (id);
			options_list =
			    g_slist_remove_link (options_list, option);
			g_slist_free_1 (option);
			option = nodetmp;
		} else
			option = option->next;
	}
	xkb_options_set_selected_list (options_list);
	clear_xkb_elements_list (options_list);
}

/* Return true if optionname describes a string already in the backend's
   list of selected options */
static gboolean
xkb_options_is_selected (gchar * optionname)
{
	gboolean retval = FALSE;
	GSList *options_list = xkb_options_get_selected_list ();
	GSList *option;
	for (option = options_list; option != NULL; option = option->next) {
		if (!strcmp ((gchar *) option->data, optionname))
			retval = TRUE;
	}
	clear_xkb_elements_list (options_list);
	return retval;
}

/* Update xkb backend to reflect the new UI state */
static void
option_toggled_cb (GtkWidget * checkbutton, gpointer data)
{
	gpointer optionID =
	    g_object_get_data (G_OBJECT (checkbutton), OPTION_ID_PROP);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbutton)))
		xkb_options_select (optionID);
	else
		xkb_options_deselect (optionID);
}

/* Add a check_button or radio_button to control a particular option
   This function makes particular use of the current... variables at
   the top of this file. */
static void
xkb_options_add_option (XklConfigRegistry * config_registry,
			XklConfigItem * config_item, GladeXML * dialog)
{
	GtkWidget *option_check;
	gchar *utf_option_name = xci_desc_to_utf8 (config_item);
	/* Copy this out because we'll load it into the widget with set_data */
	gchar *full_option_name =
	    g_strdup (gkbd_keyboard_config_merge_items
		      (current1st_level_id, config_item->name));
	gboolean initial_state;

	if (current_multi_select)
		option_check =
		    gtk_check_button_new_with_label (utf_option_name);
	else {
		if (current_radio_group == NULL) {
			/* The first radio in a group is to be "Default", meaning none of
			   the below options are to be included in the selected list.
			   This is a HIG-compliant alternative to allowing no
			   selection in the group. */
			option_check =
			    gtk_radio_button_new_with_label
			    (current_radio_group, _("Default"));
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON
						      (option_check),
						      TRUE);
			gtk_box_pack_start_defaults (GTK_BOX
						     (current_vbox),
						     option_check);
			current_radio_group =
			    gtk_radio_button_get_group (GTK_RADIO_BUTTON
							(option_check));
			current_none_radio = option_check;
		}
		option_check =
		    gtk_radio_button_new_with_label (current_radio_group,
						     utf_option_name);
		current_radio_group =
		    gtk_radio_button_get_group (GTK_RADIO_BUTTON
						(option_check));
		g_object_set_data (G_OBJECT (option_check), "NoneRadio",
				   current_none_radio);
	}
	g_free (utf_option_name);

	initial_state = xkb_options_is_selected (full_option_name);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (option_check),
				      initial_state);

	g_object_set_data_full (G_OBJECT (option_check), OPTION_ID_PROP,
				full_option_name, g_free);

	g_signal_connect (G_OBJECT (option_check), "toggled",
			  G_CALLBACK (option_toggled_cb), NULL);

	gtk_box_pack_start_defaults (GTK_BOX (current_vbox), option_check);

	xkb_options_expander_selcounter_add (xkb_options_get_expander
					     (option_check),
					     initial_state);
	g_object_set_data (G_OBJECT (option_check), GCONFSTATE_PROP,
			   GINT_TO_POINTER (initial_state));
}

/* Add a group of options: create title and layout widgets and then
   add widgets for all the options in the group. */
static void
xkb_options_add_group (XklConfigRegistry * config_registry,
		       XklConfigItem * config_item, GladeXML * dialog)
{
	GtkWidget *expander, *align, *vbox;
	gboolean allow_multiple_selection =
	    GPOINTER_TO_INT (g_object_get_data (G_OBJECT (config_item),
						XCI_PROP_ALLOW_MULTIPLE_SELECTION));

	GSList *expanders_list =
	    g_object_get_data (G_OBJECT (dialog), EXPANDERS_PROP);

	gchar *utf_group_name = xci_desc_to_utf8 (config_item);
	gchar *titlemarkup =
	    g_strconcat ("<span>", utf_group_name, "</span>", NULL);

	expander = gtk_expander_new (titlemarkup);
	gtk_expander_set_use_markup (GTK_EXPANDER (expander), TRUE);
	g_object_set_data_full (G_OBJECT (expander), "utfGroupName",
				utf_group_name, g_free);
	g_object_set_data_full (G_OBJECT (expander), "groupId",
				g_strdup (config_item->name), g_free);

	g_free (titlemarkup);
	align = gtk_alignment_new (0, 0, 1, 1);
	gtk_alignment_set_padding (GTK_ALIGNMENT (align), 6, 12, 12, 0);
	vbox = gtk_vbox_new (TRUE, 6);
	gtk_container_add (GTK_CONTAINER (align), vbox);
	gtk_container_add (GTK_CONTAINER (expander), align);
	current_vbox = vbox;

	current_multi_select = (gboolean) allow_multiple_selection;
	current_radio_group = NULL;

	current1st_level_id = config_item->name;
	xkl_config_registry_foreach_option (config_registry,
					    config_item->name,
					    (ConfigItemProcessFunc)
					    xkb_options_add_option,
					    dialog);

	xkb_options_expander_highlight (expander);

	expanders_list = g_slist_append (expanders_list, expander);
	g_object_set_data (G_OBJECT (dialog), EXPANDERS_PROP,
			   expanders_list);
}

static gint
xkb_options_expanders_compare (GtkWidget * expander1,
			       GtkWidget * expander2)
{
	const gchar *t1 =
	    g_object_get_data (G_OBJECT (expander1), "utfGroupName");
	const gchar *t2 =
	    g_object_get_data (G_OBJECT (expander2), "utfGroupName");
	return g_utf8_collate (t1, t2);
}

/* Create widgets to represent the options made available by the backend */
void
xkb_options_load_options (GladeXML * dialog)
{
	GtkWidget *opts_vbox = WID ("options_vbox");
	GSList *expanders_list;
	GtkWidget *expander;

	current1st_level_id = NULL;
	current_vbox = NULL;
	current_none_radio = NULL;
	current_multi_select = FALSE;
	current_radio_group = NULL;

	/* fill the list */
	xkl_config_registry_foreach_option_group (config_registry,
						  (ConfigItemProcessFunc)
						  xkb_options_add_group,
						  dialog);
	/* sort it */
	expanders_list =
	    g_object_get_data (G_OBJECT (dialog), EXPANDERS_PROP);
	expanders_list =
	    g_slist_sort (expanders_list,
			  (GCompareFunc) xkb_options_expanders_compare);
	while (expanders_list) {
		expander = GTK_WIDGET (expanders_list->data);
		gtk_box_pack_start (GTK_BOX (opts_vbox), expander, FALSE,
				    FALSE, 0);
		expanders_list = expanders_list->next;
	}

	gtk_widget_show_all (opts_vbox);
}

static void
chooser_response_cb (GtkDialog * dialog, gint response, gpointer data)
{
	if (response == GTK_RESPONSE_CLOSE) {
		/* just cleanup */
		GSList *expanders_list =
		    g_object_get_data (G_OBJECT (dialog), EXPANDERS_PROP);
		g_object_set_data (G_OBJECT (dialog), EXPANDERS_PROP,
				   NULL);
		g_slist_free (expanders_list);

		gtk_widget_destroy (GTK_WIDGET (dialog));
		chooser_dialog = NULL;
	}
}

/* Create popup dialog*/
void
xkb_options_popup_dialog (GladeXML * dialog)
{
	chooser_dialog =
	    glade_xml_new (GNOMECC_GLADE_DIR
			   "/gnome-keyboard-properties.glade",
			   "xkb_options_dialog", NULL);
	GtkWidget *chooser = CWID ("xkb_options_dialog");

	gtk_window_set_transient_for (GTK_WINDOW (chooser),
				      GTK_WINDOW (WID
						  ("keyboard_dialog")));
	xkb_options_load_options (chooser_dialog);

	g_signal_connect (chooser, "response",
			  G_CALLBACK (chooser_response_cb), dialog);

	gtk_dialog_run (GTK_DIALOG (chooser));
}

/* Update selected option counters for a group-bound expander */
static void
xkb_options_update_option_counters (XklConfigRegistry * config_registry,
				    XklConfigItem * config_item,
				    GtkWidget * expander)
{
	gchar *full_option_name =
	    g_strdup (gkbd_keyboard_config_merge_items
		      (current1st_level_id, config_item->name));
	gboolean current_state =
	    xkb_options_is_selected (full_option_name);
	xkb_options_expander_selcounter_add (expander, current_state);
}

/* Respond to a change in the xkb gconf settings */
static void
xkb_options_update (GConfClient * client,
		    guint cnxn_id, GConfEntry * entry, GladeXML * dialog)
{
	/* Updating options is handled by gconf notifies for each widget
	   This is here to avoid calling it N_OPTIONS times for each gconf
	   change. */
	enable_disable_restoring (dialog);

	if (chooser_dialog != NULL) {
		GSList *expanders_list =
		    g_object_get_data (G_OBJECT (chooser_dialog),
				       EXPANDERS_PROP);
		while (expanders_list) {
			GtkWidget *expander =
			    GTK_WIDGET (expanders_list->data);
			gchar *group_id =
			    g_object_get_data (G_OBJECT (expander),
					       "groupId");
			current1st_level_id = group_id;
			xkb_options_expander_selcounter_reset (expander);
			xkl_config_registry_foreach_option
			    (config_registry, group_id,
			     (ConfigItemProcessFunc)
			     xkb_options_update_option_counters, expander);
			xkb_options_expander_highlight (expander);
			expanders_list = expanders_list->next;
		}
	}
}

void
xkb_options_register_gconf_listener (GladeXML * dialog)
{
	gconf_client_notify_add (xkb_gconf_client,
				 GKBD_KEYBOARD_CONFIG_KEY_OPTIONS,
				 (GConfClientNotifyFunc)
				 xkb_options_update, dialog, NULL, NULL);
}
