/* -*- mode: c; style: linux -*- */

/* gnome-keyboard-properties-xkblt.c
 * Copyright (C) 2003-2007 Sergey V. Udaltsov
 *
 * Written by: Sergey V. Udaltsov <svu@gnome.org>
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
#include <glib/gi18n.h>

#include <libgnomekbd/gkbd-desktop-config.h>
#include <libgnomekbd/gkbd-keyboard-drawing.h>

#include "capplet-util.h"
#include "gnome-keyboard-properties-xkb.h"


#define SEL_LAYOUT_TREE_COL_DESCRIPTION 0
#define SEL_LAYOUT_TREE_COL_DEFAULT 1
#define SEL_LAYOUT_TREE_COL_ID 2

static int idx2select = -1;
static int max_selected_layouts = -1;
static int default_group = -1;

static GtkCellRenderer *text_renderer;
static GtkCellRenderer *toggle_renderer;

static gboolean disable_buttons_sensibility_update = FALSE;

void
clear_xkb_elements_list (GSList * list)
{
	while (list != NULL) {
		GSList *p = list;
		list = list->next;
		g_free (p->data);
		g_slist_free_1 (p);
	}
}

static gint
find_selected_layout_idx (GtkBuilder * dialog)
{
	GtkTreeSelection *selection =
	    gtk_tree_view_get_selection (GTK_TREE_VIEW
					 (WID ("xkb_layouts_selected")));
	GtkTreeIter selected_iter;
	GtkTreeModel *model;
	GtkTreePath *path;
	gint *indices;
	gint rv;

	if (!gtk_tree_selection_get_selected
	    (selection, &model, &selected_iter))
		return -1;

	path = gtk_tree_model_get_path (model, &selected_iter);
	if (path == NULL)
		return -1;

	indices = gtk_tree_path_get_indices (path);
	rv = indices[0];
	gtk_tree_path_free (path);
	return rv;
}

GSList *
xkb_layouts_get_selected_list (void)
{
	GSList *retval;

	retval = gconf_client_get_list (xkb_gconf_client,
					GKBD_KEYBOARD_CONFIG_KEY_LAYOUTS,
					GCONF_VALUE_STRING, NULL);
	if (retval == NULL) {
		GSList *cur_layout;

		for (cur_layout = initial_config.layouts_variants;
		     cur_layout != NULL; cur_layout = cur_layout->next)
			retval =
			    g_slist_prepend (retval,
					     g_strdup (cur_layout->data));

		retval = g_slist_reverse (retval);
	}

	return retval;
}

static void
save_default_group (int default_group)
{
	if (default_group != gconf_client_get_int (xkb_gconf_client,
						   GKBD_DESKTOP_CONFIG_KEY_DEFAULT_GROUP,
						   NULL))
		gconf_client_set_int (xkb_gconf_client,
				      GKBD_DESKTOP_CONFIG_KEY_DEFAULT_GROUP,
				      default_group, NULL);
}

static void
def_group_in_ui_changed (GtkCellRendererToggle * cell_renderer,
			 gchar * path, GtkBuilder * dialog)
{
	GtkTreePath *chpath = gtk_tree_path_new_from_string (path);
	int new_default_group = -1;
	gboolean previously_selected =
	    gtk_cell_renderer_toggle_get_active (cell_renderer);

	if (!previously_selected) {	/* prev state - non-selected! */
		int *indices = gtk_tree_path_get_indices (chpath);
		new_default_group = indices[0];
	}

	save_default_group (new_default_group);
	gtk_tree_path_free (chpath);
}

static void
def_group_in_gconf_changed (GConfClient * client,
			    guint cnxn_id,
			    GConfEntry * entry, GtkBuilder * dialog)
{
	GConfValue *value = gconf_entry_get_value (entry);

	if (!value)
		return;

	if (value->type == GCONF_VALUE_INT) {
		GtkWidget *tree_view = WID ("xkb_layouts_selected");
		GtkTreeModel *model =
		    GTK_TREE_MODEL (gtk_tree_view_get_model
				    (GTK_TREE_VIEW (tree_view)));
		GtkTreeIter iter;
		int counter = 0;
		default_group = MAX(0, gconf_value_get_int (value));
		if (gtk_tree_model_get_iter_first (model, &iter)) {
			do {
				gboolean cur_val;
				gtk_tree_model_get (model, &iter,
						    SEL_LAYOUT_TREE_COL_DEFAULT,
						    &cur_val, -1);
				if (cur_val != (counter == default_group))
					gtk_list_store_set (GTK_LIST_STORE
							    (model), &iter,
							    SEL_LAYOUT_TREE_COL_DEFAULT,
							    counter ==
							    default_group,
							    -1);
				counter++;
			}
			while (gtk_tree_model_iter_next (model, &iter));
		}
	}
}

static void
xkb_layouts_enable_disable_buttons (GtkBuilder * dialog)
{
	GtkWidget *add_layout_btn = WID ("xkb_layouts_add");
	GtkWidget *print_layout_btn = WID ("xkb_layouts_print");
	GtkWidget *del_layout_btn = WID ("xkb_layouts_remove");
	GtkWidget *selected_layouts_tree = WID ("xkb_layouts_selected");

	GtkTreeSelection *s_selection =
	    gtk_tree_view_get_selection (GTK_TREE_VIEW
					 (selected_layouts_tree));
	const int n_selected_selected_layouts =
	    gtk_tree_selection_count_selected_rows (s_selection);
	GtkTreeModel *selected_layouts_model = gtk_tree_view_get_model
	    (GTK_TREE_VIEW (selected_layouts_tree));
	const int n_selected_layouts =
	    gtk_tree_model_iter_n_children (selected_layouts_model,
					    NULL);

	if (disable_buttons_sensibility_update)
		return;

	gtk_widget_set_sensitive (add_layout_btn,
				  (n_selected_layouts <
				   max_selected_layouts
				   || max_selected_layouts == 0));
	gtk_widget_set_sensitive (del_layout_btn, (n_selected_layouts > 1)
				  && (n_selected_selected_layouts > 0));
	gtk_widget_set_sensitive (print_layout_btn,
				  (n_selected_selected_layouts > 0));
}

static GtkTreeViewColumn *
xkb_layouts_create_default_layout_column ()
{
	GtkTreeViewColumn *def_column =
	    gtk_tree_view_column_new_with_attributes (_("Default"),
						      toggle_renderer,
						      "active",
						      SEL_LAYOUT_TREE_COL_DEFAULT,
						      NULL);
	gtk_tree_view_column_set_sizing (def_column,
					 GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable (def_column, TRUE);
	gtk_tree_view_column_set_expand (def_column, FALSE);
	return def_column;
}

void
xkb_layouts_enable_disable_default (GtkBuilder * dialog, gboolean enable)
{
	GtkWidget *tree_view = WID ("xkb_layouts_selected");

	if (enable) {
		gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view),
					     xkb_layouts_create_default_layout_column
					     ());
		g_object_unref (toggle_renderer);
	} else {
		GtkTreeViewColumn *col =
		    gtk_tree_view_get_column (GTK_TREE_VIEW (tree_view),
					      1);
		if (col != NULL) {
			g_object_ref (toggle_renderer);
			gtk_tree_view_column_clear (col);
			gtk_tree_view_remove_column (GTK_TREE_VIEW
						     (tree_view), col);
		}
	}

	gtk_widget_queue_draw (tree_view);
}

static void
xkb_layouts_dnd_data_get (GtkWidget * widget, GdkDragContext * dc,
			  GtkSelectionData * selection_data, guint info,
			  guint t, GtkBuilder * dialog)
{
	/* Storing the value into selection -
	 * while it is actually not used
	 */
	gint idx = find_selected_layout_idx (dialog);
	gtk_selection_data_set (selection_data,
				GDK_SELECTION_TYPE_INTEGER, 32,
				(guchar *) & idx, sizeof (idx));
}

static void
xkb_layouts_dnd_data_received (GtkWidget * widget, GdkDragContext * dc,
			       gint x, gint y,
			       GtkSelectionData * selection_data,
			       guint info, guint t, GtkBuilder * dialog)
{
	gint sidx = find_selected_layout_idx (dialog);
	GtkWidget *tree_view = WID ("xkb_layouts_selected");
	GtkTreePath *path = NULL;
	GtkTreeViewDropPosition pos;
	gint didx;
	gchar *id;
	GSList *layouts_list;
	GSList *node2Remove;

	if (sidx == -1)
		return;

	layouts_list = xkb_layouts_get_selected_list ();
	node2Remove = g_slist_nth (layouts_list, sidx);

	id = (gchar *) node2Remove->data;
	layouts_list = g_slist_delete_link (layouts_list, node2Remove);

	if (!gtk_tree_view_get_dest_row_at_pos
	    (GTK_TREE_VIEW (tree_view), x, y, &path, &pos)) {
		/* Move to the very end */
		layouts_list =
		    g_slist_append (layouts_list, g_strdup (id));
		xkb_layouts_set_selected_list (layouts_list);
	} else if (path != NULL) {
		gint *indices = gtk_tree_path_get_indices (path);
		didx = indices[0];
		gtk_tree_path_free (path);
		/* Move to the new position */
		if (sidx != didx) {
			layouts_list =
			    g_slist_insert (layouts_list, g_strdup (id),
					    didx);
			xkb_layouts_set_selected_list (layouts_list);
		}
	}
	g_free (id);
	clear_xkb_elements_list (layouts_list);
}

void
xkb_layouts_prepare_selected_tree (GtkBuilder * dialog,
				   GConfChangeSet * changeset)
{
	GtkListStore *list_store =
	    gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_BOOLEAN,
				G_TYPE_STRING);
	GtkWidget *tree_view = WID ("xkb_layouts_selected");
	GtkTreeSelection *selection;
	GtkTargetEntry self_drag_target =
	    { "xkb_layouts_selected", GTK_TARGET_SAME_WIDGET, 0 };
	GtkTreeViewColumn *desc_column;

	text_renderer = GTK_CELL_RENDERER (gtk_cell_renderer_text_new ());
	toggle_renderer =
	    GTK_CELL_RENDERER (gtk_cell_renderer_toggle_new ());
	gtk_cell_renderer_toggle_set_radio (GTK_CELL_RENDERER_TOGGLE
					    (toggle_renderer), TRUE);
	g_object_ref (toggle_renderer);

	desc_column =
	    gtk_tree_view_column_new_with_attributes (_("Layout"),
						      text_renderer,
						      "text",
						      SEL_LAYOUT_TREE_COL_DESCRIPTION,
						      NULL);
	selection =
	    gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));

	gtk_tree_view_set_model (GTK_TREE_VIEW (tree_view),
				 GTK_TREE_MODEL (list_store));

	gtk_tree_view_column_set_sizing (desc_column,
					 GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable (desc_column, TRUE);
	gtk_tree_view_column_set_expand (desc_column, TRUE);

	gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view),
				     desc_column);

	g_signal_connect_swapped (G_OBJECT (selection), "changed",
				  G_CALLBACK
				  (xkb_layouts_enable_disable_buttons),
				  dialog);
	max_selected_layouts = xkl_engine_get_max_num_groups (engine);

	gconf_client_notify_add (xkb_gconf_client,
				 GKBD_DESKTOP_CONFIG_KEY_DEFAULT_GROUP,
				 (GConfClientNotifyFunc)
				 def_group_in_gconf_changed, dialog, NULL,
				 NULL);
	g_signal_connect (G_OBJECT (toggle_renderer), "toggled",
			  G_CALLBACK (def_group_in_ui_changed), dialog);

	/* Setting up DnD */
	gtk_drag_source_set (tree_view, GDK_BUTTON1_MASK,
			     &self_drag_target, 1, GDK_ACTION_MOVE);
	gtk_drag_source_set_icon_name (tree_view, "input-keyboard");
	gtk_drag_dest_set (tree_view, GTK_DEST_DEFAULT_ALL,
			   &self_drag_target, 1, GDK_ACTION_MOVE);

	g_signal_connect (G_OBJECT (tree_view), "drag_data_get",
			  G_CALLBACK (xkb_layouts_dnd_data_get), dialog);
	g_signal_connect (G_OBJECT (tree_view), "drag_data_received",
			  G_CALLBACK (xkb_layouts_dnd_data_received),
			  dialog);
}

gchar *
xkb_layout_description_utf8 (const gchar * visible)
{
	char *l, *sl, *v, *sv;
	if (gkbd_keyboard_config_get_descriptions
	    (config_registry, visible, &sl, &l, &sv, &v))
		visible = gkbd_keyboard_config_format_full_layout (l, v);
	return g_strstrip (g_strdup (visible));
}

void
xkb_layouts_fill_selected_tree (GtkBuilder * dialog)
{
	GConfEntry *gce;
	GError *err = NULL;
	GSList *layouts = xkb_layouts_get_selected_list ();
	GSList *cur_layout;
	GtkListStore *list_store =
	    GTK_LIST_STORE (gtk_tree_view_get_model
			    (GTK_TREE_VIEW
			     (WID ("xkb_layouts_selected"))));

	/* temporarily disable the buttons' status update */
	disable_buttons_sensibility_update = TRUE;

	gtk_list_store_clear (list_store);

	for (cur_layout = layouts; cur_layout != NULL;
	     cur_layout = cur_layout->next) {
		GtkTreeIter iter;
		const char *visible = (char *) cur_layout->data;
		gchar *utf_visible = xkb_layout_description_utf8 (visible);
		gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter,
				    SEL_LAYOUT_TREE_COL_DESCRIPTION,
				    utf_visible,
				    SEL_LAYOUT_TREE_COL_DEFAULT, FALSE,
				    SEL_LAYOUT_TREE_COL_ID,
				    cur_layout->data, -1);
		g_free (utf_visible);
	}

	clear_xkb_elements_list (layouts);

	/* enable the buttons' status update */
	disable_buttons_sensibility_update = FALSE;

	if (idx2select != -1) {
		GtkTreeSelection *selection =
		    gtk_tree_view_get_selection ((GTK_TREE_VIEW
						  (WID
						   ("xkb_layouts_selected"))));
		GtkTreePath *path =
		    gtk_tree_path_new_from_indices (idx2select, -1);
		gtk_tree_selection_select_path (selection, path);
		gtk_tree_path_free (path);
		idx2select = -1;
	} else {
		/* if there is nothing to select - just enable/disable the buttons,
		   otherwise it would be done by the selection change */
		xkb_layouts_enable_disable_buttons (dialog);
	}

	gce = gconf_client_get_entry (xkb_gconf_client,
				      GKBD_DESKTOP_CONFIG_KEY_DEFAULT_GROUP,
				      NULL, TRUE, &err);
	if (err == NULL) {
		def_group_in_gconf_changed (xkb_gconf_client, -1, gce,
					    dialog);
	} else {
		g_error_free (err);
	}
}

static void
add_selected_layout (GtkWidget * button, GtkBuilder * dialog)
{
	xkb_layout_choose (dialog);
}

static void
print_selected_layout (GtkWidget * button, GtkBuilder * dialog)
{
	gint idx = find_selected_layout_idx (dialog);

	if (idx != -1) {
		GSList *layouts_list = xkb_layouts_get_selected_list ();
		const gchar *id = g_slist_nth_data (layouts_list, idx);
		char *descr = xkb_layout_description_utf8 (id);

		GtkWidget *window = WID ("keyboard_dialog");
		GtkWidget *kbdraw =
		    xkb_layout_preview_create_widget (NULL);
		g_object_ref_sink (kbdraw);
		gtk_widget_set_parent (kbdraw, window);
		xkb_layout_preview_set_drawing_layout (kbdraw, id);
		gkbd_keyboard_drawing_print (GKBD_KEYBOARD_DRAWING
					     (kbdraw), GTK_WINDOW (window),
					     descr);
		g_object_unref (kbdraw);
		g_free (descr);

		clear_xkb_elements_list (layouts_list);
	}
}

static void
remove_selected_layout (GtkWidget * button, GtkBuilder * dialog)
{
	gint idx = find_selected_layout_idx (dialog);

	if (idx != -1) {
		GSList *layouts_list = xkb_layouts_get_selected_list ();
		char *id = NULL;
		GSList *node2Remove = g_slist_nth (layouts_list, idx);

		layouts_list =
		    g_slist_remove_link (layouts_list, node2Remove);

		id = (char *) node2Remove->data;
		g_slist_free_1 (node2Remove);
		g_free (id);

		if (default_group > idx)
			save_default_group (default_group - 1);
		else if (default_group == idx)
			save_default_group (-1);

		xkb_layouts_set_selected_list (layouts_list);
		clear_xkb_elements_list (layouts_list);
	}
}

void
xkb_layouts_register_buttons_handlers (GtkBuilder * dialog)
{
	g_signal_connect (G_OBJECT (WID ("xkb_layouts_add")), "clicked",
			  G_CALLBACK (add_selected_layout), dialog);
	g_signal_connect (G_OBJECT (WID ("xkb_layouts_print")), "clicked",
			  G_CALLBACK (print_selected_layout), dialog);
	g_signal_connect (G_OBJECT (WID ("xkb_layouts_remove")), "clicked",
			  G_CALLBACK (remove_selected_layout), dialog);
}

static void
xkb_layouts_update_list (GConfClient * client,
			 guint cnxn_id, GConfEntry * entry,
			 GtkBuilder * dialog)
{
	xkb_layouts_fill_selected_tree (dialog);
	enable_disable_restoring (dialog);
}

void
xkb_layouts_register_gconf_listener (GtkBuilder * dialog)
{
	gconf_client_notify_add (xkb_gconf_client,
				 GKBD_KEYBOARD_CONFIG_KEY_LAYOUTS,
				 (GConfClientNotifyFunc)
				 xkb_layouts_update_list, dialog, NULL,
				 NULL);
}
