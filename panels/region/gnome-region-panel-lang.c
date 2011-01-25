/*
 * Copyright (C) 2010 Bastien Nocera
 *
 * Written by: Bastien Nocera <hadess@hadess.net>
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
#include <glib/gi18n.h>

#include "gnome-region-panel-lang.h"
#include "cc-common-language.h"
#include "gdm-languages.h"

static GHashTable *
new_ht_for_user_languages (void)
{
	GHashTable *ht;
	char *name;
	char *language;

	ht = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	/* FIXME: As in um-language-dialog.c, we should add the
	 * languages used by other users on the system */

	/* Add current locale */
	name = cc_common_language_get_current_language ();
	language = gdm_get_language_from_name (name, NULL);
	g_hash_table_insert (ht, name, language);

	return ht;
}

static void
remove_timeout (gpointer data,
		GObject *where_the_object_was)
{
	guint timeout = GPOINTER_TO_UINT (data);
	g_source_remove (timeout);
}

static gboolean
finish_language_setup (gpointer user_data)
{
	GtkWidget *list = (GtkWidget *) user_data;
	GtkTreeModel *model;
	GtkWidget *parent;
	GHashTable *user_langs;
	guint timeout;

	/* Did we get called after the widget was destroyed? */
	if (list == NULL)
		return FALSE;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (list));
	user_langs = g_object_get_data (G_OBJECT (list), "user-langs");

	cc_common_language_add_available_languages (GTK_LIST_STORE (model), user_langs);

	parent = gtk_widget_get_toplevel (list);
	gdk_window_set_cursor (gtk_widget_get_window (parent), NULL);

	g_object_set_data (G_OBJECT (list), "user-langs", NULL);
	timeout = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (list), "timeout"));
	g_object_weak_unref (G_OBJECT (list), (GWeakNotify) remove_timeout, GUINT_TO_POINTER (timeout));

	return FALSE;
}

void
setup_language (GtkBuilder *builder)
{
	GtkWidget *treeview;
	GHashTable *user_langs;
	GtkWidget *parent;
	GtkTreeModel *model;
	GdkWindow *window;
	guint timeout;

	treeview = GTK_WIDGET (gtk_builder_get_object (builder, "display_language_treeview"));
	parent = gtk_widget_get_toplevel (treeview);
	g_message ("parent %p", parent);

	/* Add user languages */
	user_langs = new_ht_for_user_languages ();
	cc_common_language_setup_list (treeview, user_langs);
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (treeview));

	/* Setup so that the list is populated after the list appears */
	window = gtk_widget_get_window (parent);
	if (window) {
		GdkCursor *cursor;

		cursor = gdk_cursor_new (GDK_WATCH);
		gdk_window_set_cursor (gtk_widget_get_window (parent), cursor);
		g_object_unref (cursor);
	}

	g_object_set_data_full (G_OBJECT (treeview), "user-langs",
				user_langs, (GDestroyNotify) g_hash_table_destroy);
	timeout = g_idle_add ((GSourceFunc) finish_language_setup, treeview);
	g_object_set_data (G_OBJECT (treeview), "timeout", GUINT_TO_POINTER (timeout));
	g_object_weak_ref (G_OBJECT (treeview), (GWeakNotify) remove_timeout, GUINT_TO_POINTER (timeout));
}
