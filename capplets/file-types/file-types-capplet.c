/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* nautilus-mime-type-capplet.h
 *
 * Copyright (C) 1998 Redhat Software Inc. 
 * Copyright (C) 2000  Free Software Foundaton
 * Copyright (C) 2000  Eazel, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: 	Jonathan Blandford <jrb@redhat.com>
 * 		Gene Z. Ragan <gzr@eazel.com>
 */

#include <config.h>
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <sys/types.h>
#include <regex.h>
#include <string.h>

#include <capplet-widget.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdkprivate.h>
#include <gnome.h>
#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include <libgnomevfs/gnome-vfs-mime-info.h>
#include <libgnomevfs/gnome-vfs-init.h>

#include "nautilus-mime-type-capplet-dialogs.h"
#include "nautilus-mime-type-icon-entry.h"

#include "nautilus-mime-type-capplet.h"


#define TOTAL_COLUMNS 4

/* Local Prototypes */
static void	 init_mime_capplet 	  		(void);
static void 	 populate_application_menu 		(GtkWidget 	*menu, 	 
						 	 const char 	*mime_string);
static void 	 populate_viewer_menu			(GtkWidget 	*menu, 	 
						 	 const char 	*mime_string);
static void	 delete_mime_clicked       		(GtkWidget 	*widget, 
						 	 gpointer   	data);
static void	 add_mime_clicked 	  		(GtkWidget 	*widget, 
						 	 gpointer   	data);
static void	 edit_default_clicked 			(GtkWidget 	*widget, 	
						 	 gpointer   	data);
static GtkWidget *create_mime_list_and_scroller 	(void);
static char 	 *pixmap_file 				(const char 	*partial_path);
static void 	 ok_callback 		  		(void);
static void	 gtk_widget_make_bold 			(GtkWidget 	*widget);
static GdkFont 	 *gdk_font_get_bold 			(const GdkFont  *plain_font);
static void	 gtk_widget_set_font 			(GtkWidget 	*widget, 
						 	 GdkFont 	*font);
static void	 gtk_style_set_font 			(GtkStyle 	*style, 
						 	 GdkFont 	*font);
static GdkPixbuf *capplet_gdk_pixbuf_scale_to_fit 	(GdkPixbuf 	*pixbuf, 
							 int 		max_width, 
							 int 		max_height);


GtkWidget *capplet;
GtkWidget *delete_button;
GtkWidget *remove_button;
GtkWidget *add_button;
GtkWidget *icon_entry, *extension_list, *mime_list;
GtkWidget *default_menu;
GtkWidget *application_button, *viewer_button;
GtkLabel  *mime_label;
GtkWidget *description_entry;

/*
 *  main
 *
 *  Display capplet
 */

int
main (int argc, char **argv)
{
        int init_results;

        bindtextdomain (PACKAGE, GNOMELOCALEDIR);
        textdomain (PACKAGE);

        init_results = gnome_capplet_init("mime-type-capplet", VERSION,
                                          argc, argv, NULL, 0, NULL);

	if (init_results < 0) {
                exit (0);
	}

	gnome_vfs_init ();

	if (init_results == 0) {
		init_mime_capplet ();
		g_print ("initilaized\n");
	        capplet_gtk_main ();
	}
        return 0;
}

static void
ok_callback ()
{
}

static void
populate_extension_list (const char *mime_type, GtkCList *list)
{
	GList *extensions, *element;
	gchar *extension[1];
	gint row;
		
	if (mime_type == NULL || list == NULL) {
		return;
	}
		
	/* Clear out old items */
	gtk_clist_clear (list);

	extensions = gnome_vfs_mime_get_extensions_list (mime_type);
	if (extensions == NULL) {
		return;
	}

	for (element = extensions; element != NULL; element = element->next) {
		extension[0] = (char *)element->data;
		if (strlen (element->data) > 0) {
			row = gtk_clist_append (list, extension);
			
			/* Set to deletable */
			gtk_clist_set_row_data (list, row, GINT_TO_POINTER (FALSE));
		}
	}

	gnome_vfs_mime_extensions_list_free (extensions);	
	
	/* Select first item in extension list */
	gtk_clist_select_row (list, 0, 0);
}

void
nautilus_mime_type_capplet_add_extension (const char *extension)
{
	gchar *title[1];
	gchar *token, *search_string;
	gint rownumber;
	const char *mime_type;

	/* Check for empty string */
	if (strlen (extension) <= 0) {
		return;
	}

	/* Check for starting space in string */
	if (extension[0] == ' ') {
		return;
	}
	
	/* Copy only contiguous part of string.  No spaces allowed. */	
	search_string = g_strdup (extension);
	token = strtok (search_string, " ");

	if (token == NULL) {		
		title[0] = g_strdup (extension);
	} else if (strlen (token) <= 0) {
		return;
	}else {
		title[0] = g_strdup (token);		
	}
	g_free (search_string);
	
	rownumber = gtk_clist_append (GTK_CLIST (extension_list), title);
	gtk_clist_set_row_data (GTK_CLIST (extension_list), rownumber,
				GINT_TO_POINTER (TRUE));

	mime_type = nautilus_mime_type_capplet_get_selected_item_mime_type ();
	g_assert (mime_type != NULL);
	gnome_vfs_mime_add_extension (mime_type, extension);

	/* Select new item in list */
	gtk_clist_select_row (GTK_CLIST (extension_list), rownumber, 0);

}

static void
add_extension_clicked (GtkWidget *widget, gpointer data)
{
	nautilus_mime_type_capplet_show_new_extension_window ();
}

static void
remove_extension_clicked (GtkWidget *widget, gpointer data)
{
        gint row;
	gchar *text;
	gchar *store;
	const char *mime_type;
	
	text = (gchar *)g_malloc (sizeof (gchar) * 1024);
	gtk_clist_freeze (GTK_CLIST (extension_list));
	row = GPOINTER_TO_INT (GTK_CLIST (extension_list)->selection->data);
	gtk_clist_get_text (GTK_CLIST (extension_list), row, 0, &text);
	store = g_strdup (text);
	gtk_clist_remove (GTK_CLIST (extension_list), row);
	gtk_clist_thaw (GTK_CLIST (extension_list));


	mime_type = nautilus_mime_type_capplet_get_selected_item_mime_type ();
	if (mime_type != NULL) {
		gnome_vfs_mime_remove_extension (mime_type, store);
	}

	/* Select first item in list */
	gtk_clist_select_row (GTK_CLIST (extension_list), 0, 0);

	g_free (store);
}

static void
extension_list_selected (GtkWidget *clist, gint row, gint column, gpointer data)
{
        gboolean deletable;

	deletable = GPOINTER_TO_INT (gtk_clist_get_row_data (GTK_CLIST (clist), row));
	if (deletable)
	        gtk_widget_set_sensitive (remove_button, TRUE);
	else
	        gtk_widget_set_sensitive (remove_button, FALSE);
}

static void
extension_list_deselected (GtkWidget *clist, gint row, gint column, gpointer data)
{
        if (g_list_length (GTK_CLIST (clist)->selection) == 0) {
	        gtk_widget_set_sensitive (remove_button, FALSE);
	}
}

static void
mime_list_selected_row_callback (GtkWidget *widget, gint row, gint column, GdkEvent *event, gpointer data)
{
        const char *mime_type;

        mime_type = (const char *) gtk_clist_get_row_data (GTK_CLIST (widget),row);

	/* Update info on selection */
        nautilus_mime_type_capplet_update_info (mime_type);
        
	/* FIXME bugzilla.eazel.com 2764: Get user mime info and determine if we can enable the delete button */
}

static void
application_button_toggled (GtkToggleButton *button, gpointer user_data)
{
	const char *mime_type;
	
	if (gtk_toggle_button_get_active (button)) {

		mime_type = nautilus_mime_type_capplet_get_selected_item_mime_type ();
		
		gnome_vfs_mime_set_default_action_type (mime_type, GNOME_VFS_MIME_ACTION_TYPE_APPLICATION);

		/* Populate menu with application items */
		populate_application_menu (default_menu, mime_type);						
	}
}

static void
viewer_button_toggled (GtkToggleButton *button, gpointer user_data)
{
	const char *mime_type;
	
	if (gtk_toggle_button_get_active (button)) {
		mime_type = nautilus_mime_type_capplet_get_selected_item_mime_type ();

		gnome_vfs_mime_set_default_action_type (mime_type, GNOME_VFS_MIME_ACTION_TYPE_COMPONENT);

		/* Populate menu with viewer items */
		populate_viewer_menu (default_menu, mime_type);
	}
}

static void 
icon_changed (GtkWidget *entry, gpointer user_data)
{
	gchar *path, *filename;
	const char *mime_type;
	
	path = nautilus_mime_type_icon_entry_get_filename (NAUTILUS_MIME_ICON_ENTRY (icon_entry));
	if (path != NULL) {
		g_message ("%s", path);
		filename = strrchr (path, '/');
		if (filename != NULL) {
			filename++;
			mime_type = nautilus_mime_type_capplet_get_selected_item_mime_type ();
			gnome_vfs_mime_set_icon (mime_type, filename);
		}
		g_free (path);
	}
}

static void 
change_icon_clicked (GtkWidget *entry, gpointer user_data)
{
	nautilus_mime_type_show_icon_selection (NAUTILUS_MIME_ICON_ENTRY (user_data));
}

static void
init_mime_capplet (void)
{
	GtkWidget *main_vbox;
        GtkWidget *vbox, *hbox;
        GtkWidget *button;
        GtkWidget *mime_list_container;
        GtkWidget *extension_scroller;
        GtkWidget *frame;
        GtkWidget *alignment;
        GtkWidget *table, *extensions_table;
	int index, list_width, column_width;
	
	capplet = capplet_widget_new ();


	/* Main vertical box */                    
	main_vbox = gtk_vbox_new (FALSE, GNOME_PAD);
	gtk_container_set_border_width (GTK_CONTAINER (main_vbox), GNOME_PAD_SMALL);
        gtk_container_add (GTK_CONTAINER (capplet), main_vbox);

        /* Main horizontal box and mime list*/                    
        hbox = gtk_hbox_new (FALSE, GNOME_PAD);
        gtk_box_pack_start (GTK_BOX (main_vbox), hbox, TRUE, TRUE, 0);
        mime_list_container = create_mime_list_and_scroller ();
        gtk_box_pack_start (GTK_BOX (hbox), mime_list_container, TRUE, TRUE, 0);         
	
	vbox = gtk_vbox_new (FALSE, GNOME_PAD_SMALL);
        gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, FALSE, 0);

	/* Create table */
	table = gtk_table_new (2, 2, FALSE);
	gtk_box_pack_start (GTK_BOX (main_vbox), table, FALSE, FALSE, 0);
	gtk_table_set_row_spacings (GTK_TABLE (table), GNOME_PAD_SMALL);
	gtk_table_set_col_spacings (GTK_TABLE (table), GNOME_PAD_SMALL);
	
	/* Set up top left area.  This contains the icon display, mime description,
	   mime type label and change icon button */
	hbox = gtk_hbox_new (FALSE, GNOME_PAD_SMALL);
	gtk_table_attach_defaults ( GTK_TABLE (table), hbox, 0, 1, 0, 1);
	vbox = gtk_vbox_new (FALSE, GNOME_PAD_SMALL);
	gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, FALSE, 0);

	icon_entry = nautilus_mime_type_icon_entry_new ("mime_icon_entry", NULL);
	gtk_signal_connect (GTK_OBJECT (nautilus_mime_type_icon_entry_gtk_entry (NAUTILUS_MIME_ICON_ENTRY (icon_entry))),
			    "changed", icon_changed, NULL);
	gtk_box_pack_start (GTK_BOX (vbox), icon_entry, FALSE, FALSE, 0);
	
	button = gtk_button_new_with_label (_("Change Icon"));
	gtk_signal_connect (GTK_OBJECT (button), "clicked", change_icon_clicked, icon_entry);
	gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
	vbox = gtk_vbox_new (FALSE, GNOME_PAD_SMALL);	
	gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, FALSE, 0);

	description_entry = gtk_entry_new ();
	alignment = gtk_alignment_new (0, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 0);	
	gtk_container_add (GTK_CONTAINER (alignment), description_entry);
	gtk_widget_set_usize (description_entry, 200, 0);
	gtk_widget_make_bold (GTK_WIDGET (description_entry));
	mime_label = GTK_LABEL (gtk_label_new (_("Mime Type")));	
	gtk_label_set_justify (GTK_LABEL (mime_label), GTK_JUSTIFY_RIGHT);
	alignment = gtk_alignment_new (0, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 0);
	gtk_container_add (GTK_CONTAINER (alignment), GTK_WIDGET(mime_label));

	/* Set up bottom left area.  This contains two buttons to add
	   and delete mime types */
	vbox = gtk_vbox_new (FALSE, 7);
	gtk_table_attach_defaults ( GTK_TABLE (table), vbox, 0, 1, 1, 2);
	/* Put in a spacer to make buttons layout as we want them */
	{
		GtkWidget *spacer;
		spacer = gtk_fixed_new ();
		gtk_box_pack_start (GTK_BOX (vbox), spacer, FALSE, FALSE, 0);
		gtk_widget_set_usize (spacer, 10, 30);
	}

	alignment = gtk_alignment_new (0, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 0);
	
	button = gtk_button_new_with_label (_("Add new Mime type..."));
	gtk_signal_connect (GTK_OBJECT (button), "clicked", add_mime_clicked, NULL);
	gtk_container_add (GTK_CONTAINER (alignment), button);

	alignment = gtk_alignment_new (0, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 0);

	delete_button = gtk_button_new_with_label (_("Delete this Mime type..."));
	gtk_container_add (GTK_CONTAINER (alignment), delete_button);
	gtk_signal_connect (GTK_OBJECT (delete_button), "clicked", delete_mime_clicked, NULL);

	/* Set up top right area. */
	frame = gtk_frame_new ("Default Action:");
	gtk_table_attach_defaults ( GTK_TABLE (table), frame, 1, 2, 0, 1);

	vbox = gtk_vbox_new (FALSE, GNOME_PAD_SMALL);
	gtk_container_add (GTK_CONTAINER (frame), vbox);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);

	hbox = gtk_hbox_new (FALSE, GNOME_PAD_SMALL);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

	viewer_button = gtk_radio_button_new_with_label (NULL, _("Use Viewer"));
	gtk_box_pack_start (GTK_BOX (hbox), viewer_button, FALSE, FALSE, 0);
	gtk_signal_connect (GTK_OBJECT (viewer_button), "toggled",
			    GTK_SIGNAL_FUNC (viewer_button_toggled), NULL);

	application_button = gtk_radio_button_new_with_label_from_widget ( 
		           GTK_RADIO_BUTTON (viewer_button), _("Open With Application"));
	gtk_box_pack_start (GTK_BOX (hbox), application_button, FALSE, FALSE, 0);
	gtk_signal_connect (GTK_OBJECT (application_button), "toggled",
			    GTK_SIGNAL_FUNC (application_button_toggled), NULL);

	hbox = gtk_hbox_new (FALSE, GNOME_PAD_SMALL);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

	default_menu = gtk_option_menu_new();
	gtk_widget_set_usize (GTK_WIDGET (default_menu), 170, 0);
	gtk_box_pack_start (GTK_BOX (hbox), default_menu, FALSE, FALSE, 0);

        button = gtk_button_new_with_label (_("Edit List"));
        gtk_widget_set_usize (GTK_WIDGET (button), 70, 0);
        gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
	gtk_signal_connect (GTK_OBJECT (button), "clicked", edit_default_clicked, mime_list);

	/* Set up bottom right area. */
	frame = gtk_frame_new ("Extensions:");
	gtk_table_attach_defaults ( GTK_TABLE (table), frame, 1, 2, 1, 2);

	extensions_table = gtk_table_new (2, 2, FALSE);
	gtk_container_add (GTK_CONTAINER (frame), extensions_table);	
	gtk_table_set_row_spacings (GTK_TABLE (extensions_table), 10);
	gtk_table_set_col_spacings (GTK_TABLE (extensions_table), 10);
	gtk_container_set_border_width (GTK_CONTAINER (extensions_table), 5);

	extension_list = gtk_clist_new (1);
	gtk_clist_column_titles_passive (GTK_CLIST (extension_list));
	gtk_clist_set_auto_sort (GTK_CLIST (extension_list), TRUE);
	gtk_clist_set_selection_mode (GTK_CLIST (extension_list), GTK_SELECTION_BROWSE);
        gtk_clist_set_auto_sort (GTK_CLIST (extension_list), TRUE);

	extension_scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (extension_scroller),
					GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (extension_scroller), extension_list);

	gtk_widget_set_usize (extension_scroller, 150, 60);
	gtk_table_attach_defaults ( GTK_TABLE (extensions_table), extension_scroller, 0, 1, 0, 2);

        add_button = gtk_button_new_with_label (_("Add"));	
	gtk_table_attach_defaults ( GTK_TABLE (extensions_table), add_button, 1, 2, 0, 1);

        remove_button = gtk_button_new_with_label (_("Remove"));	
	gtk_table_attach_defaults ( GTK_TABLE (extensions_table), remove_button, 1, 2, 1, 2);

	gtk_signal_connect (GTK_OBJECT (remove_button), "clicked",
			    GTK_SIGNAL_FUNC (remove_extension_clicked), NULL);
	gtk_signal_connect (GTK_OBJECT (add_button), "clicked",
			    GTK_SIGNAL_FUNC (add_extension_clicked), NULL);
	gtk_signal_connect (GTK_OBJECT (extension_list), "select-row",
			    GTK_SIGNAL_FUNC (extension_list_selected), NULL);
	gtk_signal_connect (GTK_OBJECT (extension_list), "unselect-row",
			    GTK_SIGNAL_FUNC (extension_list_deselected), NULL);

	/* Set up enabled/disabled states of capplet buttons */	
	gtk_widget_set_sensitive (add_button, TRUE);
	/* FIXME bugzilla.eazel.com 2765: this call generates a 
	   Gtk-WARNING **: gtk_signal_disconnect_by_data(): could not find handler containing data (0x80FA6F8)
	*/
	gtk_widget_set_sensitive (remove_button, FALSE);
	
	/* Yes, show all widgets */
        gtk_widget_show_all (capplet);

	/* Make columns all fit within capplet list view bounds */
	list_width = GTK_WIDGET (mime_list)->allocation.width;
	column_width = list_width / TOTAL_COLUMNS;
	for (index = 0; index < TOTAL_COLUMNS; index++) {
		gtk_clist_set_column_width (GTK_CLIST (mime_list), index, column_width);
	}

        /* Setup capplet signals */
        gtk_signal_connect(GTK_OBJECT(capplet), "ok",
                           GTK_SIGNAL_FUNC(ok_callback), NULL);

	gtk_signal_connect (GTK_OBJECT (mime_list),"select_row",
       	                   GTK_SIGNAL_FUNC (mime_list_selected_row_callback), NULL);

	/* Select first item in list and update menus */
	gtk_clist_select_row (GTK_CLIST (mime_list), 0, 0);

	capplet_widget_state_changed (CAPPLET_WIDGET (capplet), FALSE);
}


/*
 *  nautilus_mime_type_capplet_update_info
 *
 *  Update controls with info based on mime type	
 */
 
void
nautilus_mime_type_capplet_update_info (const char *mime_type) {

	GnomeVFSMimeAction *action;
	const char *icon_name, *description;
	char *path;
	
	/* Update text items */
	gtk_label_set_text (GTK_LABEL (mime_label), mime_type);

	/* Add description to first column */
	description = gnome_vfs_mime_get_description (mime_type);	
	if (description != NULL && strlen (description) > 0) {
		gtk_entry_set_text (GTK_ENTRY (description_entry), description);
	} else {
		gtk_entry_set_text (GTK_ENTRY (description_entry), "No Description");
	}

	gtk_editable_set_editable (GTK_EDITABLE (description_entry), FALSE);
	
	/* Update menus */
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (application_button))) {
		populate_application_menu (default_menu, mime_type);						
	} else {
		populate_viewer_menu (default_menu, mime_type);						
	}

	/* Update extensions list */
	populate_extension_list (mime_type, GTK_CLIST (extension_list));

	/* Set icon for mime type */
	icon_name = gnome_vfs_mime_get_icon (mime_type);
	path = pixmap_file (icon_name);
	nautilus_mime_type_icon_entry_set_icon (NAUTILUS_MIME_ICON_ENTRY (icon_entry), path);

	/* Indicate default action */	
	action = gnome_vfs_mime_get_default_action (mime_type);
	if (action != NULL) {
		switch (action->action_type) {
			case GNOME_VFS_MIME_ACTION_TYPE_APPLICATION:
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (application_button), TRUE);
				break;

			case GNOME_VFS_MIME_ACTION_TYPE_COMPONENT:
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer_button), TRUE);
				break;
				
			default:
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (application_button), TRUE);
				break;
		}
	} else {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (application_button), TRUE);
	}
}

static void 
application_menu_activated (GtkWidget *menu_item, gpointer data)
{
	const char *id;
	const char *mime_type;

	/* Get our stashed data */
	id = gtk_object_get_data (GTK_OBJECT (menu_item), "id");
	mime_type = gtk_object_get_data (GTK_OBJECT (menu_item), "mime_type");

	if (id == NULL || mime_type == NULL) {
		return;
	}	
	gnome_vfs_mime_set_default_application (mime_type, id);
}

static void
populate_application_menu (GtkWidget *application_menu, const char *mime_type)
{
	GtkWidget *new_menu, *menu_item;
	GList *app_list, *copy_list;
	GnomeVFSMimeApplication *default_app, *application;
	gboolean has_none, found_match;
	char *mime_copy;
	const char *id;
	GList *children;
	int index;

	has_none = TRUE;
	found_match = FALSE;

	mime_copy = g_strdup (mime_type);
	
	new_menu = gtk_menu_new ();
	
	/* Get the default application */
	default_app = gnome_vfs_mime_get_default_application (mime_type);

	/* Get the application short list */
	app_list = gnome_vfs_mime_get_short_list_applications (mime_type);
	if (app_list != NULL) {
		for (copy_list = app_list; copy_list != NULL; copy_list = copy_list->next) {
			has_none = FALSE;

			application = copy_list->data;
			menu_item = gtk_menu_item_new_with_label (application->name);

			/* Store copy of application name and mime type in item; free when item destroyed. */
			gtk_object_set_data_full (GTK_OBJECT (menu_item),
						  "id",
						  g_strdup (application->id),
						  g_free);

			gtk_object_set_data_full (GTK_OBJECT (menu_item),
						  "mime_type",
						  g_strdup (mime_type),
						  g_free);
			
			gtk_menu_append (GTK_MENU (new_menu), menu_item);
			gtk_widget_show (menu_item);

			gtk_signal_connect (GTK_OBJECT (menu_item), "activate", 
					    application_menu_activated, NULL);
		}
	
		gnome_vfs_mime_application_list_free (app_list);
	}

	/* Find all applications or add a "None" item */
	if (has_none && default_app == NULL) {		
		menu_item = gtk_menu_item_new_with_label (_("None"));
		gtk_menu_append (GTK_MENU (new_menu), menu_item);
		gtk_widget_show (menu_item);
	} else {
		/* Check and see if default is in the short list */
		if (default_app != NULL) {
			/* Iterate */
			children = gtk_container_children (GTK_CONTAINER (new_menu));
			for (index = 0; children != NULL; children = children->next, ++index) {				
				id = (const char *)(gtk_object_get_data (GTK_OBJECT (children->data), "id"));
				if (id != NULL) {											
					if (strcmp (default_app->id, id) == 0) {
						found_match = TRUE;
						break;
					}
				}
			}
			g_list_free (children);

			/* See if we have a match */
			if (found_match) {
				/* Have menu appear with default application selected */
				gtk_menu_set_active (GTK_MENU (new_menu), index);
			} else {
				/* No match found.  We need to insert a menu item
				 * and add the application to the default list */
				menu_item = gtk_menu_item_new_with_label (default_app->name);

				/* Store copy of application name and mime type in item; free when item destroyed. */
				gtk_object_set_data_full (GTK_OBJECT (menu_item),
							  "id",
							  g_strdup (default_app->id),
							  g_free);

				gtk_object_set_data_full (GTK_OBJECT (menu_item),
							  "mime_type",
							  g_strdup (mime_type),
							  g_free);
				
				gtk_menu_append (GTK_MENU (new_menu), menu_item);
				gtk_widget_show (menu_item);

				gtk_signal_connect (GTK_OBJECT (menu_item), "activate", 
						    application_menu_activated, NULL);
				
				
				/* Iterate */
				children = gtk_container_children (GTK_CONTAINER (new_menu));
				for (index = 0; children != NULL; children = children->next, ++index) {				
					id = (const char *)(gtk_object_get_data (GTK_OBJECT (children->data), "id"));
					if (id != NULL) {											
						if (strcmp (default_app->id, id) == 0) {
							found_match = TRUE;
							break;
						}
					}
				}
				g_list_free (children);

				/* Set it as active */
				gtk_menu_set_active (GTK_MENU (new_menu), index);
			}
		}
	}
	
	gtk_option_menu_set_menu (GTK_OPTION_MENU (default_menu), new_menu);
}


static void 
component_menu_activated (GtkWidget *menu_item, gpointer data)
{
	const char *iid;
	const char *mime_type;

	/* Get our stashed data */
	iid = gtk_object_get_data (GTK_OBJECT (menu_item), "iid");
	mime_type = gtk_object_get_data (GTK_OBJECT (menu_item), "mime_type");

	if (iid == NULL || mime_type == NULL) {
		return;
	}

	gnome_vfs_mime_set_default_component (mime_type, iid);
}

static void
populate_viewer_menu (GtkWidget *component_menu, const char *mime_type)
{
	GtkWidget *new_menu;
	GtkWidget *menu_item;
	GList *component_list, *copy_list;
	GList *list_element;
	gboolean has_none, found_match;
	char *component_name;
	OAF_ServerInfo *default_component;
	OAF_ServerInfo *info;
	const char *iid;
	GList *children;
	int index;
	
	has_none = TRUE;
	found_match = FALSE;

	new_menu = gtk_menu_new ();
	
	/* Get the default component */
	default_component = gnome_vfs_mime_get_default_component (mime_type);

	/* Fill list with default components */
	component_list = gnome_vfs_mime_get_short_list_components (mime_type);
	if (component_list != NULL) {
		for (list_element = component_list; list_element != NULL; list_element = list_element->next) {
			has_none = FALSE;

			component_name = name_from_oaf_server_info (list_element->data);
			menu_item = gtk_menu_item_new_with_label (component_name);

			/* Store copy of component name and mime type in item; free when item destroyed. */
			info = list_element->data;
			gtk_object_set_data_full (GTK_OBJECT (menu_item),
						  "iid",
						  g_strdup (info->iid),
						  g_free);

			gtk_object_set_data_full (GTK_OBJECT (menu_item),
						  "mime_type",
						  g_strdup (mime_type),
						  g_free);
			
			gtk_menu_append (GTK_MENU (new_menu), menu_item);
			gtk_widget_show (menu_item);

			gtk_signal_connect (GTK_OBJECT (menu_item), "activate", 
					    component_menu_activated, NULL);
		}

		gnome_vfs_mime_component_list_free (component_list);
	}
	
	/* Add a "None" item */
	if (has_none && default_component == NULL) {		
		menu_item = gtk_menu_item_new_with_label (_("None"));
		gtk_menu_append (GTK_MENU (new_menu), menu_item);
		gtk_widget_show (menu_item);
	} else {
		/* Check and see if default is in the short list */
		if (default_component != NULL) {
			/* Iterate */
			children = gtk_container_children (GTK_CONTAINER (new_menu));
			for (index = 0; children != NULL; children = children->next, ++index) {				
				iid = (const char *)(gtk_object_get_data (GTK_OBJECT (children->data), "iid"));
				if (iid != NULL) {											
					if (strcmp (default_component->iid, iid) == 0) {
						found_match = TRUE;
						break;
					}
				}
			}
			g_list_free (children);

			/* See if we have a match */
			if (found_match) {
				/* Have menu appear with default application selected */
				gtk_menu_set_active (GTK_MENU (new_menu), index);
			} else {
				/* No match found.  We need to insert a menu item
				 * and add the application to the default list */

				/* FIXME bugzilla.eazel.com 2766: this is obviously not finished */
				copy_list = NULL;
				
				component_name = name_from_oaf_server_info (copy_list->data);
				menu_item = gtk_menu_item_new_with_label (component_name);


				/* Store copy of application name and mime type in item; free when item destroyed. */
				gtk_object_set_data_full (GTK_OBJECT (menu_item),
							  "iid",
							  g_strdup (default_component->iid),
							  g_free);

				gtk_object_set_data_full (GTK_OBJECT (menu_item),
							  "mime_type",
							  g_strdup (mime_type),
							  g_free);
				
				gtk_menu_append (GTK_MENU (new_menu), menu_item);
				gtk_widget_show (menu_item);

				gtk_signal_connect (GTK_OBJECT (menu_item), "activate", 
						    component_menu_activated, NULL);
				
				
				/* Iterate */
				children = gtk_container_children (GTK_CONTAINER (new_menu));
				for (index = 0; children != NULL; children = children->next, ++index) {				
					iid = (const char *)(gtk_object_get_data (GTK_OBJECT (children->data), "iid"));
					if (iid != NULL) {											
						if (strcmp (default_component->iid, iid) == 0) {
							found_match = TRUE;
							break;
						}
					}
				}
				g_list_free (children);

				/* Set it as active */
				gtk_menu_set_active (GTK_MENU (new_menu), index);
			}
		}
	}
	
	gtk_option_menu_set_menu (GTK_OPTION_MENU (default_menu), new_menu);
}


/*
 *  delete_mime_clicked	
 *
 *  Delete mime type if it is a user defined type.
 */
 
static void
delete_mime_clicked (GtkWidget *widget, gpointer data)
{
        const char *mime_type;
        gint row = 0;

        if (GTK_CLIST (mime_list)->selection)
                row = GPOINTER_TO_INT ((GTK_CLIST (mime_list)->selection)->data);
        else
                return;
        mime_type = (const char *) gtk_clist_get_row_data (GTK_CLIST (mime_list), row);

        gtk_clist_remove (GTK_CLIST (mime_list), row);
	/* FIXME bugzilla.eazel.com 2767: Get user mime info */
        //g_hash_table_remove (user_mime_types, mi->mime_type);
	//remove_mime_info (mi->mime_type);
}

static void
add_mime_clicked (GtkWidget *widget, gpointer data)
{
	nautilus_mime_type_capplet_show_new_mime_window ();
}

static void
edit_default_clicked (GtkWidget *widget, gpointer data)
{
	GtkWidget *list;
	const char *mime_type;
	GList *p;
	GtkCListRow *row;

	g_return_if_fail (GTK_IS_CLIST (data));

	list = data;
	row = NULL;

	/* Get first selected row.  This will be the only selection for this list */
	for (p = GTK_CLIST (list)->row_list; p != NULL; p = p->next) {
		row = p->data;
		if (row->state == GTK_STATE_SELECTED) {
			break;
		}
	}

	if (row == NULL) {
		return;
	}
	
	/* Show dialog */
	mime_type = (const char *) row->data;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (application_button))) {
		show_edit_applications_dialog (mime_type);
	} else {
		show_edit_components_dialog (mime_type);
	}
}

/*
 * nautilus_mime_type_capplet_update_application_info
 * 
 * Update state of the applications menu.  This function is called
 * when the Edit Applications dialog is closed with an OK.
 */
 
void
nautilus_mime_type_capplet_update_application_info (const char *mime_type)
{
	populate_application_menu (default_menu, mime_type);
}

/*
 * nautilus_mime_type_capplet_update_component_info
 * 
 * Update state of the components menu.  This function is called
 * when the Edit Componests dialog is closed with an OK.
 */
 
void
nautilus_mime_type_capplet_update_viewer_info (const char *mime_type)
{
	populate_viewer_menu (default_menu, mime_type);
}


static void
populate_mime_list (GList *type_list, GtkCList *clist)
{
	static gchar *text[3];        
	const char *description, *action_icon_name, *description_icon_name;
	char *extensions, *mime_string, *action_icon_path, *description_icon_path;
        gint row;
	GList *element;
	GdkPixbuf *pixbuf;
	GdkPixmap *pixmap;
	GdkBitmap *bitmap;
	GnomeVFSMimeAction *action;
	GnomeVFSMimeApplication *default_app;
	OAF_ServerInfo *default_component;

	//gtk_clist_set_row_height (clist, 20);
	
	for (element = type_list; element != NULL; element= element->next) {
		mime_string = (char *)element->data;
		
		/* Make sure we do not include comments */
		if (mime_string[0] != '#') {

			/* Add description to first column */
			description = gnome_vfs_mime_get_description (mime_string);	
			if (description != NULL && strlen (description) > 0) {
				text[0] = g_strdup (description);
			} else {
				text[0] = g_strdup ("");
			}

			/* Add mime type to second column */
			text[1] = mime_string;

			/* Add extension to third columns */
			extensions = gnome_vfs_mime_get_extensions_pretty_string (mime_string);
			if (extensions != NULL) {
				text[2] = extensions;
			} else {
				text[2] = "";
			}

			/* Add default action to fourth column */
			text[3] = _("None");

			/* Insert item into list */
			row = gtk_clist_insert (GTK_CLIST (clist), 1, text);
		        gtk_clist_set_row_data (GTK_CLIST (clist), row, g_strdup(mime_string));

			/* Set description column icon */
			description_icon_name = gnome_vfs_mime_get_icon (mime_string);			
			if (description_icon_name != NULL) {
				/* Get custom icon */
				description_icon_path = pixmap_file (description_icon_name);
				pixbuf = gdk_pixbuf_new_from_file (description_icon_path);				
			} else {
				/* Use default icon */
				pixbuf = gdk_pixbuf_new_from_file ("/gnome/share/pixmaps/nautilus/i-regular-24.png");
			}

			if (pixbuf != NULL) {
				pixbuf = capplet_gdk_pixbuf_scale_to_fit (pixbuf, 18, 18);
				gdk_pixbuf_render_pixmap_and_mask (pixbuf, &pixmap, &bitmap, 100);
				gtk_clist_set_pixtext (clist, row, 0, text[0], 5, pixmap, bitmap);
				gdk_pixbuf_unref (pixbuf);
			}

			/* Set up action column */

			pixbuf = NULL;
			action = gnome_vfs_mime_get_default_action (mime_string);
			if (action != NULL) {
				switch (action->action_type) {
					case GNOME_VFS_MIME_ACTION_TYPE_APPLICATION:
						/* Get the default application */
						default_app = gnome_vfs_mime_get_default_application (mime_string);
						text[3] = default_app->name;

						action_icon_name = gnome_vfs_mime_get_icon (mime_string);			
						if (action_icon_name != NULL) {
							/* Get custom icon */
							action_icon_path = pixmap_file (action_icon_name);
							pixbuf = gdk_pixbuf_new_from_file (action_icon_path);				
						} else {
							/* Use default icon */
							pixbuf = gdk_pixbuf_new_from_file ("/gnome/share/pixmaps/nautilus/i-executable.png");
						}
						break;

					case GNOME_VFS_MIME_ACTION_TYPE_COMPONENT:
						/* Get the default component */
						default_component = gnome_vfs_mime_get_default_component (mime_string);
						text[3] = name_from_oaf_server_info (default_component);
						pixbuf = gdk_pixbuf_new_from_file ("/gnome/share/pixmaps/nautilus/gnome-library.png");
						break;
						
					default:
						gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (application_button), TRUE);
						break;
				}
			}
			
			/* Set column icon */
			if (pixbuf != NULL) {
				pixbuf = capplet_gdk_pixbuf_scale_to_fit (pixbuf, 18, 18);
				gdk_pixbuf_render_pixmap_and_mask (pixbuf, &pixmap, &bitmap, 100);
				gtk_clist_set_pixtext (clist, row, 3, text[3], 5, pixmap, bitmap);
				gdk_pixbuf_unref (pixbuf);
			}
			
			g_free (text[0]);
			g_free (extensions);
		}
	}
}

static void
column_clicked (GtkCList *clist, gint column, gpointer user_data)
{
	gtk_clist_set_sort_column (clist, column);

	/* Toggle sort type */
	if (clist->sort_type == GTK_SORT_ASCENDING) {
		gtk_clist_set_sort_type (clist, GTK_SORT_DESCENDING);
	} else {
		gtk_clist_set_sort_type (clist, GTK_SORT_ASCENDING);
	}
  
	gtk_clist_sort (clist);
}


static GtkWidget *
create_mime_list_and_scroller (void)
{
        GtkWidget *window;
        gchar *titles[TOTAL_COLUMNS];
	GList *type_list;
	int index;
	        
        titles[0] = _("Description");
        titles[1] = _("Mime Type");
        titles[2] = _("Extension");
        titles[3] = _("Action");
        
        window = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (window),
                                        GTK_POLICY_AUTOMATIC,
                                        GTK_POLICY_AUTOMATIC);
	mime_list = gtk_clist_new_with_titles (TOTAL_COLUMNS, titles);
        gtk_clist_set_selection_mode (GTK_CLIST (mime_list), GTK_SELECTION_BROWSE);
        gtk_clist_set_auto_sort (GTK_CLIST (mime_list), TRUE);

	type_list = gnome_vfs_get_registered_mime_types ();
	populate_mime_list (type_list, GTK_CLIST (mime_list));
	gnome_vfs_mime_registered_mime_type_list_free (type_list);
	
        gtk_clist_columns_autosize (GTK_CLIST (mime_list));
        gtk_clist_select_row (GTK_CLIST (mime_list), 0, 0);
        gtk_container_add (GTK_CONTAINER (window), mime_list);

        /* Enable all titles */
	gtk_clist_column_titles_active (GTK_CLIST (mime_list));
	gtk_signal_connect (GTK_OBJECT (mime_list), "click_column", 
			    column_clicked, NULL);
	
	/* Turn off autoresizing of columns */
	for (index = 0; index < TOTAL_COLUMNS; index++) {
		gtk_clist_set_column_auto_resize (GTK_CLIST (mime_list), index, FALSE);
	}
		
        return window;
}

const char * 
nautilus_mime_type_capplet_get_selected_item_mime_type (void)
{
	const char *mime_type;
	int row;
	GtkCList *clist;

	clist = GTK_CLIST (mime_list);
	
	if (clist->selection == NULL) {
		return NULL;
	}

	/* This is a single selection list, so we just use the first item in 
	 * the list to retireve the data */
	row = GPOINTER_TO_INT (clist->selection->data);

	mime_type = (const char *) gtk_clist_get_row_data (clist, row);

	return mime_type;
}


/**
 * make_path:
 * 
 * Make a path name from a base path and name. The base path
 * can end with or without a separator character.
 *
 * Return value: the combined path name.
 **/
static char * 
make_path (const char *path, const char* name)
{
    	gboolean insert_separator;
    	int path_length;
	char *result;

	path_length = strlen (path);
    	insert_separator = path_length > 0 && 
    			   name[0] != '\0' && 
    			   path[path_length - 1] != G_DIR_SEPARATOR;

    	if (insert_separator) {
    		result = g_strconcat (path, G_DIR_SEPARATOR_S, name, NULL);
    	} else {
    		result = g_strconcat (path, name, NULL);
    	}

	return result;
}

static char *
pixmap_file (const char *partial_path)
{
	char *path;

	if (partial_path == NULL) {
		return NULL;
	}

	/* FIXME bugzilla.eazel.com 2768: Where to get DATADIR? */
	/*path = make_path (DATADIR "/pixmaps/nautilus", partial_path);*/
	path = make_path ("/gnome/share/pixmaps/nautilus", partial_path);
	if (g_file_exists (path)) {
		return path;
	} else {
		g_free (path);
		return NULL;
	}
}


/**
 * gtk_label_make_bold.
 *
 * Switches the font of label to a bold equivalent.
 * @label: The label.
 **/

static void
gtk_widget_make_bold (GtkWidget *widget)
{
	GtkStyle *style;
	GdkFont *bold_font;

	g_return_if_fail (GTK_IS_WIDGET (widget));
	style = gtk_widget_get_style (widget);

	bold_font = gdk_font_get_bold (style->font);

	if (bold_font == NULL) {
		return;
	}

	gtk_widget_set_font (widget, bold_font);
	gdk_font_unref (bold_font);
}

/**
 * gtk_widget_set_font
 *
 * Sets the font for a widget's style, managing the style objects.
 * @widget: The widget.
 * @font: The font.
 **/
static void
gtk_widget_set_font (GtkWidget *widget, GdkFont *font)
{
	GtkStyle *new_style;
	
	g_return_if_fail (GTK_IS_WIDGET (widget));
	g_return_if_fail (font != NULL);
	
	new_style = gtk_style_copy (gtk_widget_get_style (widget));

	gtk_style_set_font (new_style, font);
	
	gtk_widget_set_style (widget, new_style);
	gtk_style_unref (new_style);
}

/**
 * gtk_style_set_font
 *
 * Sets the font in a style object, managing the ref. counts.
 * @style: The style to change.
 * @font: The new font.
 **/
static void
gtk_style_set_font (GtkStyle *style, GdkFont *font)
{
	g_return_if_fail (style != NULL);
	g_return_if_fail (font != NULL);
	
	gdk_font_ref (font);
	gdk_font_unref (style->font);
	style->font = font;
}

/**
 * gdk_font_get_bold
 * @plain_font: A font.
 * Returns: A bold variant of @plain_font or NULL.
 *
 * Tries to find a bold flavor of a given font. Returns NULL if none is available.
 */
static GdkFont *
gdk_font_get_bold (const GdkFont *plain_font)
{
	const char *plain_name;
	const char *scanner;
	char *bold_name;
	int count;
	GSList *p;
	GdkFont *result;
	GdkFontPrivate *private_plain;

	private_plain = (GdkFontPrivate *)plain_font;

	if (private_plain->names == NULL) {
		return NULL;
	}


	/* -foundry-family-weight-slant-sel_width-add-style-pixels-points-hor_res-ver_res-spacing-average_width-char_set_registry-char_set_encoding */

	bold_name = NULL;
	for (p = private_plain->names; p != NULL; p = p->next) {
		plain_name = (const char *)p->data;
		scanner = plain_name;

		/* skip past foundry and family to weight */
		for (count = 2; count > 0; count--) {
			scanner = strchr (scanner + 1, '-');
			if (!scanner) {
				break;
			}
		}

		if (!scanner) {
			/* try the other names in the list */
			continue;
		}
		g_assert (*scanner == '-');

		/* copy "-foundry-family-" over */
		scanner++;
		bold_name = g_strndup (plain_name, scanner - plain_name);

		/* skip weight */
		scanner = strchr (scanner, '-');
		g_assert (scanner != NULL);

		/* add "bold" and copy everything past weight over */
		bold_name = g_strconcat (bold_name, "bold", scanner, NULL);
		break;
	}
	
	if (bold_name == NULL) {
		return NULL;
	}
	
	result = gdk_font_load (bold_name);
	g_free (bold_name);

	return result;
}


/* scale the passed in pixbuf to conform to the passed-in maximum width and height */
/* utility routine to scale the passed-in pixbuf to be smaller than the maximum allowed size, if necessary */
static GdkPixbuf *
capplet_gdk_pixbuf_scale_to_fit (GdkPixbuf *pixbuf, int max_width, int max_height)
{
	double scale_factor;
	double h_scale = 1.0;
	double v_scale = 1.0;

	int width  = gdk_pixbuf_get_width(pixbuf);
	int height = gdk_pixbuf_get_height(pixbuf);
	
	if (width > max_width) {
		h_scale = max_width / (double) width;
	}
	if (height > max_height) {
		v_scale = max_height  / (double) height;
	}
	scale_factor = MIN (h_scale, v_scale);
	
	if (scale_factor < 1.0) {
		GdkPixbuf *scaled_pixbuf;
		/* the width and scale factor are always > 0, so it's OK to round by adding here */
		int scaled_width  = floor(width * scale_factor + .5);
		int scaled_height = floor(height * scale_factor + .5);
				
		scaled_pixbuf = gdk_pixbuf_scale_simple (pixbuf, scaled_width, scaled_height, GDK_INTERP_BILINEAR);	
		gdk_pixbuf_unref (pixbuf);
		pixbuf = scaled_pixbuf;
	}
	
	return pixbuf;
}
