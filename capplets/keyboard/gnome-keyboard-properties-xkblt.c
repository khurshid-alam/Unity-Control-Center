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

#include <gnome.h>
#include <gconf/gconf-client.h>
#include <glade/glade.h>

#include "capplet-util.h"

#include <libgnomekbd/gkbd-desktop-config.h>

#include "gnome-keyboard-properties-xkb.h"

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
			 gchar * path, GladeXML * dialog)
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
			    GConfEntry * entry, GladeXML * dialog)
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
		default_group = gconf_value_get_int (value);
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
xkb_layouts_enable_disable_buttons (GladeXML * dialog)
{
	GtkWidget *add_layout_btn = WID ("xkb_layouts_add");
	GtkWidget *del_layout_btn = WID ("xkb_layouts_remove");
	GtkWidget *up_layout_btn = WID ("xkb_layouts_up");
	GtkWidget *dn_layout_btn = WID ("xkb_layouts_down");
	GtkWidget *selected_layouts_tree = WID ("xkb_layouts_selected");

	GtkTreeSelection *s_selection =
	    gtk_tree_view_get_selection (GTK_TREE_VIEW
					 (selected_layouts_tree));
	const int n_selected_selected_layouts =
	    gtk_tree_selection_count_selected_rows (s_selection);
	gboolean can_move_up = FALSE;
	gboolean can_move_dn = FALSE;
	GtkTreeIter iter;
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
	gtk_widget_set_sensitive (del_layout_btn,
				  n_selected_selected_layouts > 0);

	if (gtk_tree_selection_get_selected (s_selection, NULL, &iter)) {
		GtkTreePath *path =
		    gtk_tree_model_get_path (selected_layouts_model,
					     &iter);
		if (path != NULL) {
			int *indices = gtk_tree_path_get_indices (path);
			int idx = indices[0];
			can_move_up = idx > 0;
			can_move_dn = idx < (n_selected_layouts - 1);
			gtk_tree_path_free (path);
		}
	}

	gtk_widget_set_sensitive (up_layout_btn, can_move_up);
	gtk_widget_set_sensitive (dn_layout_btn, can_move_dn);
}

void
xkb_layouts_enable_disable_default (GladeXML * dialog, gboolean enable)
{
	GValue val = { 0 };
	g_value_init (&val, G_TYPE_BOOLEAN);
	g_value_set_boolean (&val, enable);
	g_object_set_property (G_OBJECT (toggle_renderer), "activatable",
			       &val);
}

void
xkb_layouts_prepare_selected_tree (GladeXML * dialog,
				   GConfChangeSet * changeset)
{
	GtkListStore *list_store =
	    gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_BOOLEAN,
				G_TYPE_STRING);
	GtkWidget *tree_view = WID ("xkb_layouts_selected");
	GtkTreeViewColumn *desc_column, *def_column;
	GtkTreeSelection *selection;

	text_renderer = GTK_CELL_RENDERER (gtk_cell_renderer_text_new ());
	toggle_renderer =
	    GTK_CELL_RENDERER (gtk_cell_renderer_toggle_new ());
	gtk_cell_renderer_toggle_set_radio (
	    GTK_CELL_RENDERER_TOGGLE (toggle_renderer), TRUE);

	desc_column =
	    gtk_tree_view_column_new_with_attributes (_("Layout"),
						      text_renderer,
						      "text",
						      SEL_LAYOUT_TREE_COL_DESCRIPTION,
						      NULL);
	def_column =
	    gtk_tree_view_column_new_with_attributes (_("Default"),
						      toggle_renderer,
						      "active",
						      SEL_LAYOUT_TREE_COL_DEFAULT,
						      NULL);
	selection =
	    gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));

	gtk_tree_view_set_model (GTK_TREE_VIEW (tree_view),
				 GTK_TREE_MODEL (list_store));

	gtk_tree_view_column_set_sizing (desc_column,
					 GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_sizing (def_column,
					 GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable (desc_column, TRUE);
	gtk_tree_view_column_set_resizable (def_column, TRUE);

	gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view),
				     desc_column);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view),
				     def_column);

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
}

void
xkb_layouts_fill_selected_tree (GladeXML * dialog)
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
		char *l, *sl, *v, *sv;
		char *v1, *utf_visible;
		const char *visible = (char *) cur_layout->data;
		gtk_list_store_append (list_store, &iter);
		if (gkbd_keyboard_config_get_descriptions
		    (config_registry, visible, &sl, &l, &sv, &v))
			visible =
			    gkbd_keyboard_config_format_full_layout (l, v);
		v1 = g_strdup (visible);
		utf_visible =
		    g_locale_to_utf8 (g_strstrip (v1), -1, NULL, NULL,
				      NULL);
		gtk_list_store_set (list_store, &iter,
				    SEL_LAYOUT_TREE_COL_DESCRIPTION,
				    utf_visible,
				    SEL_LAYOUT_TREE_COL_DEFAULT, FALSE,
				    SEL_LAYOUT_TREE_COL_ID,
				    cur_layout->data, -1);
		g_free (utf_visible);
		g_free (v1);
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
add_selected_layout (GtkWidget * button, GladeXML * dialog)
{
	xkb_layout_choose (dialog);
}

static void
move_selected_layout (GladeXML * dialog, int offset)
{
	GtkTreeSelection *selection =
	    gtk_tree_view_get_selection (GTK_TREE_VIEW
					 (WID ("xkb_layouts_selected")));
	GtkTreeIter selected_iter;
	GtkTreeModel *model;
	if (gtk_tree_selection_get_selected
	    (selection, &model, &selected_iter)) {
		GSList *layouts_list = xkb_layouts_get_selected_list ();
		GtkTreePath *path = gtk_tree_model_get_path (model,
							     &selected_iter);
		if (path != NULL) {
			int *indices = gtk_tree_path_get_indices (path);
			int idx = indices[0];
			char *id = NULL;
			GSList *node2Remove =
			    g_slist_nth (layouts_list, idx);

			layouts_list =
			    g_slist_remove_link (layouts_list,
						 node2Remove);

			id = (char *) node2Remove->data;
			g_slist_free_1 (node2Remove);

			if (offset == 0) {
				g_free (id);
				if (default_group > idx)
					save_default_group (default_group -
							    1);
				else if (default_group == idx)
					save_default_group (-1);
			} else {
				layouts_list =
				    g_slist_insert (layouts_list, id,
						    idx + offset);
				idx2select = idx + offset;
				if (idx == default_group)
					save_default_group (idx2select);
				else if (idx2select == default_group)
					save_default_group (idx);
			}

			xkb_layouts_set_selected_list (layouts_list);
			gtk_tree_path_free (path);
		}
		clear_xkb_elements_list (layouts_list);
	}
}

static void
remove_selected_layout (GtkWidget * button, GladeXML * dialog)
{
	move_selected_layout (dialog, 0);
}

static void
up_selected_layout (GtkWidget * button, GladeXML * dialog)
{
	move_selected_layout (dialog, -1);
}

static void
down_selected_layout (GtkWidget * button, GladeXML * dialog)
{
	move_selected_layout (dialog, +1);
}

void
xkb_layouts_register_buttons_handlers (GladeXML * dialog)
{
	g_signal_connect (G_OBJECT (WID ("xkb_layouts_add")), "clicked",
			  G_CALLBACK (add_selected_layout), dialog);
	g_signal_connect (G_OBJECT (WID ("xkb_layouts_remove")), "clicked",
			  G_CALLBACK (remove_selected_layout), dialog);
	g_signal_connect (G_OBJECT (WID ("xkb_layouts_up")), "clicked",
			  G_CALLBACK (up_selected_layout), dialog);
	g_signal_connect (G_OBJECT (WID ("xkb_layouts_down")), "clicked",
			  G_CALLBACK (down_selected_layout), dialog);
}

static void
xkb_layouts_update_list (GConfClient * client,
			 guint cnxn_id, GConfEntry * entry,
			 GladeXML * dialog)
{
	xkb_layouts_fill_selected_tree (dialog);
	enable_disable_restoring (dialog);
}

void
xkb_layouts_register_gconf_listener (GladeXML * dialog)
{
	gconf_client_notify_add (xkb_gconf_client,
				 GKBD_KEYBOARD_CONFIG_KEY_LAYOUTS,
				 (GConfClientNotifyFunc)
				 xkb_layouts_update_list, dialog, NULL,
				 NULL);
}
