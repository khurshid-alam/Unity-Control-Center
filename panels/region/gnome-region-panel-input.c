/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * Written by: Matthias Clasen <mclasen@redhat.com>
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

#include <config.h>

#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "gnome-region-panel-input.h"

#define WID(s) GTK_WIDGET(gtk_builder_get_object (builder, s))

static GtkWidget *input_chooser_new          (GtkWindow     *main_window);
static gboolean   input_chooser_get_selected (GtkWidget     *chooser,
                                              GtkTreeModel **model,
                                              GtkTreeIter   *iter);

static gboolean
get_selected_iter (GtkBuilder    *builder,
                   GtkTreeModel **model,
                   GtkTreeIter   *iter)
{
  GtkTreeSelection *selection;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (WID ("active_input_sources")));

  return gtk_tree_selection_get_selected (selection, model, iter);
}

static gint
find_selected_layout_idx (GtkBuilder *builder)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkTreePath *path;
  gint idx;

  if (!get_selected_iter (builder, &model, &iter))
    return -1;

  path = gtk_tree_model_get_path (model, &iter);
  if (path == NULL)
    return -1;

  idx = gtk_tree_path_get_indices (path)[0];
  gtk_tree_path_free (path);

  return idx;
}

static void
update_button_sensitivity (GtkBuilder *builder)
{
  GtkWidget *remove_button;
  GtkWidget *up_button;
  GtkWidget *down_button;
  GtkWidget *show_button;
  GtkTreeView *tv;
  gint n_active;
  gint index;

  remove_button = WID("input_source_remove");
  show_button = WID("input_source_show");
  up_button = WID("input_source_move_up");
  down_button = WID("input_source_move_down");

  tv = GTK_TREE_VIEW (WID ("active_input_sources"));

  n_active = gtk_tree_model_iter_n_children (gtk_tree_view_get_model (tv), NULL);
  index = find_selected_layout_idx (builder);

  gtk_widget_set_sensitive (remove_button, index >= 0 && n_active > 1);
  gtk_widget_set_sensitive (show_button, index >= 0);
  gtk_widget_set_sensitive (up_button, index > 0);
  gtk_widget_set_sensitive (down_button, index >= 0 && index < n_active - 1);
}

static void
set_selected_path (GtkBuilder  *builder,
                   GtkTreePath *path)
{
  GtkTreeSelection *selection;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (WID ("active_input_sources")));

  gtk_tree_selection_select_path (selection, path);
}

static void
chooser_response (GtkWidget *chooser, gint response_id, gpointer data)
{
  GtkBuilder *builder = data;

  if (response_id == GTK_RESPONSE_OK)
    {
      GtkTreeModel *model;
      GtkTreeIter iter;

      if (input_chooser_get_selected (chooser, &model, &iter))
        {
          GtkTreeView *my_tv;
          GtkListStore *my_model;
          GtkTreeIter my_iter;
          gchar *name;

          gtk_tree_model_get (model, &iter,
                              0, &name,
                              -1);

          my_tv = GTK_TREE_VIEW (WID ("active_input_sources"));
          my_model = GTK_LIST_STORE (gtk_tree_view_get_model (my_tv));

          gtk_list_store_append (my_model, &my_iter);

          gtk_list_store_set (my_model, &my_iter,
                              0, name,
                              -1);

          g_free (name);

          gtk_tree_selection_select_iter (gtk_tree_view_get_selection (my_tv), &my_iter);

          update_button_sensitivity (builder);
        }
      else
        {
          g_debug ("nothing selected, nothing added");
        }
    }

  gtk_widget_destroy (GTK_WIDGET (chooser));
}

static void
add_input (GtkButton *button, gpointer data)
{
  GtkBuilder *builder = data;
  GtkWidget *chooser;
  GtkWidget *toplevel;

  g_debug ("add an input source");

  toplevel = gtk_widget_get_toplevel (WID ("region_notebook"));
  chooser = input_chooser_new (GTK_WINDOW (toplevel));
  g_signal_connect (chooser, "response",
                    G_CALLBACK (chooser_response), builder);
}

static void
remove_selected_input (GtkButton *button, gpointer data)
{
  GtkBuilder *builder = data;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreePath *path;

  g_debug ("remove selected input source");

  if (get_selected_iter (builder, &model, &iter) == FALSE)
    return;

  path = gtk_tree_model_get_path (model, &iter);
  gtk_list_store_remove (GTK_LIST_STORE (model), &iter);

  if (!gtk_tree_model_get_iter (model, &iter, path))
    gtk_tree_path_prev (path);

  set_selected_path (builder, path);

  gtk_tree_path_free (path);

  update_button_sensitivity (builder);
}

static void
move_selected_input_up (GtkButton *button, gpointer data)
{
  GtkBuilder *builder = data;
  GtkTreeModel *model;
  GtkTreeIter iter, prev;
  GtkTreePath *path;

  g_debug ("move selected input source up");

  if (!get_selected_iter (builder, &model, &iter))
    return;

  prev = iter;
  if (!gtk_tree_model_iter_previous (model, &prev))
    return;

  path = gtk_tree_model_get_path (model, &prev);

  gtk_list_store_swap (GTK_LIST_STORE (model), &iter, &prev);
  set_selected_path (builder, path);
  gtk_tree_path_free (path);

  update_button_sensitivity (builder);
}

static void
move_selected_input_down (GtkButton *button, gpointer data)
{
  GtkBuilder *builder = data;
  GtkTreeModel *model;
  GtkTreeIter iter, next;
  GtkTreePath *path;

  g_debug ("move selected input source down");

  if (!get_selected_iter (builder, &model, &iter))
    return;

  next = iter;
  if (!gtk_tree_model_iter_next (model, &next))
    return;

  path = gtk_tree_model_get_path (model, &next);
  gtk_list_store_swap (GTK_LIST_STORE (model), &iter, &next);
  set_selected_path (builder, path);
  gtk_tree_path_free (path);

  update_button_sensitivity (builder);
}

static void
show_selected_layout (GtkButton *button, gpointer data)
{
  GtkBuilder *builder = data;
  GtkTreeModel *model;
  GtkTreeIter iter;
  gchar *layout;
  gchar *kbd_viewer_args;

  g_debug ("show selected layout");

  if (!get_selected_iter (builder, &model, &iter))
    return;

  gtk_tree_model_get (model, &iter,
                      0, &layout,
                      -1);

  kbd_viewer_args = g_strdup_printf ("gkbd-keyboard-display -l %s", layout);
  g_spawn_command_line_async (kbd_viewer_args, NULL);

  g_free (kbd_viewer_args);
  g_free (layout);
}

static gboolean
go_to_shortcuts (GtkLinkButton *button,
                 CcRegionPanel *panel)
{
  CcShell *shell;
  const gchar *argv[] = { "shortcuts", NULL };
  GError *error = NULL;

  shell = cc_panel_get_shell (CC_PANEL (panel));
  if (!cc_shell_set_active_panel_from_id (shell, "keyboard", argv, &error))
    {
      g_warning ("Failed to activate Keyboard panel: %s", error->message);
      g_error_free (error);
    }

  return TRUE;
}

void
setup_input_tabs (GtkBuilder    *builder,
                  CcRegionPanel *panel)
{
  GtkWidget *treeview;
  GtkTreeViewColumn *column;
  GtkCellRenderer *cell;
  GtkListStore *store;
  GtkTreeSelection *selection;
  gchar *previous = NULL;
  gchar *next = NULL;
  GtkWidget *label;

  /* set up the list of active inputs */
  treeview = WID("active_input_sources");
  column = gtk_tree_view_column_new ();
  cell = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, cell, TRUE);
  gtk_tree_view_column_add_attribute (column, cell, "text", 0);
  gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

  store = gtk_list_store_new (1, G_TYPE_STRING);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  g_signal_connect_swapped (selection, "changed",
                            G_CALLBACK (update_button_sensitivity), builder);

  gtk_tree_view_set_model (GTK_TREE_VIEW (treeview), GTK_TREE_MODEL (store));

  /* set up the buttons */
  g_signal_connect (WID("input_source_add"), "clicked",
                    G_CALLBACK (add_input), builder);
  g_signal_connect (WID("input_source_remove"), "clicked",
                    G_CALLBACK (remove_selected_input), builder);
  g_signal_connect (WID("input_source_move_up"), "clicked",
                    G_CALLBACK (move_selected_input_up), builder);
  g_signal_connect (WID("input_source_move_down"), "clicked",
                    G_CALLBACK (move_selected_input_down), builder);
  g_signal_connect (WID("input_source_show"), "clicked",
                    G_CALLBACK (show_selected_layout), builder);

  /* use an em dash is no shortcut */
  if (!previous)
    previous = g_strdup ("\342\200\224");
  if (!next)
    next = g_strdup ("\342\200\224");

  label = WID("prev-source-shortcut-label");
  gtk_label_set_label (GTK_LABEL (label), previous);
  label = WID("next-source-shortcut-label");
  gtk_label_set_label (GTK_LABEL (label), next);

  g_free (previous);
  g_free (next);

  g_signal_connect (WID("jump-to-shortcuts"), "activate-link",
                    G_CALLBACK (go_to_shortcuts), panel);
}

static void
filter_clear (GtkEntry             *entry,
              GtkEntryIconPosition  icon_pos,
              GdkEvent             *event,
              gpointer              user_data)
{
  gtk_entry_set_text (entry, "");
}

static gchar **search_pattern_list;

static void
filter_changed (GtkBuilder *builder)
{
  GtkTreeModelFilter *filtered_model;
  GtkWidget *filter_entry;
  const gchar *pattern;
  gchar *upattern;

  filtered_model = GTK_TREE_MODEL_FILTER (gtk_builder_get_object (builder,
                                                   "filtered_input_source_model"));
  filter_entry = WID ("input_source_filter");
  pattern = gtk_entry_get_text (GTK_ENTRY (filter_entry));
  upattern = g_utf8_strup (pattern, -1);
  if (!g_strcmp0 (pattern, ""))
    g_object_set (G_OBJECT (filter_entry),
                  "secondary-icon-name", "edit-find-symbolic",
                  "secondary-icon-activatable", FALSE,
                  "secondary-icon-sensitive", FALSE,
                  NULL);
  else
    g_object_set (G_OBJECT (filter_entry),
                  "secondary-icon-name", "edit-clear-symbolic",
                  "secondary-icon-activatable", TRUE,
                  "secondary-icon-sensitive", TRUE,
                  NULL);

  if (search_pattern_list != NULL)
    g_strfreev (search_pattern_list);

  search_pattern_list = g_strsplit (upattern, " ", -1);
  g_free (upattern);

  gtk_tree_model_filter_refilter (filtered_model);
}

static void
selection_changed (GtkTreeSelection *selection,
                   GtkBuilder       *builder)
{
  gtk_widget_set_sensitive (WID ("ok-button"),
                            gtk_tree_selection_get_selected (selection, NULL, NULL));
}

static void
row_activated (GtkTreeView       *tree_view,
               GtkTreePath       *path,
               GtkTreeViewColumn *column,
               GtkBuilder        *builder)
{
  GtkWidget *add_button;
  GtkWidget *dialog;

  add_button = WID ("ok-button");
  dialog = WID ("input_source_chooser");
  if (gtk_widget_is_sensitive (add_button))
    gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
}

static gboolean
filter_func (GtkTreeModel *model,
             GtkTreeIter  *iter,
             gpointer      data)
{
  gchar *desc = NULL;
  gchar **pattern;
  gboolean rv = TRUE;

  if (search_pattern_list == NULL || search_pattern_list[0] == NULL)
    return TRUE;

  gtk_tree_model_get (model, iter,
                      0, &desc,
                      -1);

  pattern = search_pattern_list;
  do {
    gboolean is_pattern_found = FALSE;
    gchar *udesc = g_utf8_strup (desc, -1);
    if (udesc != NULL && g_strstr_len (udesc, -1, *pattern))
      {
        is_pattern_found = TRUE;
      }
    g_free (udesc);

    if (!is_pattern_found)
      {
        rv = FALSE;
        break;
      }

  } while (*++pattern != NULL);

  g_free (desc);

  return rv;
}

static GtkWidget *
input_chooser_new (GtkWindow *main_window)
{
  GtkBuilder *builder;
  GtkWidget *chooser;
  GtkWidget *filtered_list;
  GtkWidget *filter_entry;
  GtkTreeViewColumn *visible_column;
  GtkTreeSelection *selection;
  GtkListStore *model;
  GtkTreeModelFilter *filtered_model;

  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder,
                             GNOMECC_UI_DIR "/gnome-region-panel-input-chooser.ui",
                             NULL);
  chooser = WID ("input_source_chooser");
  g_object_set_data_full (G_OBJECT (chooser), "builder", builder, g_object_unref);

  filtered_list = WID ("filtered_input_source_list");
  filter_entry = WID ("input_source_filter");

  g_object_set_data (G_OBJECT (chooser),
                     "filtered_input_source_list", filtered_list);
  visible_column =
    gtk_tree_view_column_new_with_attributes ("Layout",
                                              gtk_cell_renderer_text_new (),
                                              "text", 0,
                                              NULL);

  gtk_window_set_transient_for (GTK_WINDOW (chooser), main_window);

  gtk_tree_view_append_column (GTK_TREE_VIEW (filtered_list),
                               visible_column);
  g_signal_connect_swapped (G_OBJECT (filter_entry), "notify::text",
                            G_CALLBACK (filter_changed), builder);

  g_signal_connect (G_OBJECT (filter_entry), "icon-release",
                    G_CALLBACK (filter_clear), NULL);

  selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (filtered_list));

  g_signal_connect (G_OBJECT (selection), "changed",
                    G_CALLBACK (selection_changed), builder);

  selection_changed (selection, builder);

  g_signal_connect (G_OBJECT (filtered_list), "row-activated",
                    G_CALLBACK (row_activated), builder);

  filtered_model = GTK_TREE_MODEL_FILTER (gtk_builder_get_object (builder, "filtered_input_source_model"));
  model = GTK_LIST_STORE (gtk_builder_get_object (builder, "input_source_model"));

  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model),
                                        0, GTK_SORT_ASCENDING);

  gtk_tree_model_filter_set_visible_func (filtered_model,
                                          filter_func,
                                          NULL, NULL);

  gtk_widget_grab_focus (filter_entry);

  gtk_widget_show (chooser);

  return chooser;
}

static gboolean
input_chooser_get_selected (GtkWidget     *dialog,
                            GtkTreeModel **model,
                            GtkTreeIter   *iter)
{
  GtkWidget *tv;
  GtkTreeSelection *selection;

  tv = g_object_get_data (G_OBJECT (dialog), "filtered_input_source_list");
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tv));

  return gtk_tree_selection_get_selected (selection, model, iter);
}
