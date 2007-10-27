/*
 * Copyright (C) 2007 The GNOME Foundation
 * Written by Thomas Wood <thos@gnome.org>
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "appearance.h"

#include <string.h>
#include <pango/pango.h>

#include "theme-util.h"
#include "gtkrc-utils.h"
#include "gconf-property-editor.h"
#include "theme-thumbnail.h"

typedef void (* ThumbnailGenFunc) (void               *type,
				   ThemeThumbnailFunc  theme,
				   AppearanceData     *data,
				   GDestroyNotify     *destroy);

static const gchar *symbolic_names[NUM_SYMBOLIC_COLORS] = {
  "fg_color", "bg_color",
  "text_color", "base_color",
  "selected_fg_color", "selected_bg_color",
  "tooltip_fg_color", "tooltip_bg_color"
};

static gchar *
find_string_in_model (GtkTreeModel *model, const gchar *value, gint column)
{
  GtkTreeIter iter;
  gboolean valid;
  gchar *path = NULL, *test;

  if (!value)
    return NULL;

  for (valid = gtk_tree_model_get_iter_first (model, &iter); valid;
       valid = gtk_tree_model_iter_next (model, &iter))
  {
    gtk_tree_model_get (model, &iter, column, &test, -1);

    if (test)
    {
      gint cmp = strcmp (test, value);
      g_free (test);

      if (!cmp)
      {
        path = gtk_tree_model_get_string_from_iter (model, &iter);
        break;
      }
    }
  }

  return path;
}

static GConfValue *
conv_to_widget_cb (GConfPropertyEditor *peditor, const GConfValue *value)
{
  GtkTreeModel *store;
  GtkTreeView *list;
  const gchar *curr_value;
  GConfValue *new_value;
  gchar *path;

  /* find value in model */
  curr_value = gconf_value_get_string (value);
  list = GTK_TREE_VIEW (gconf_property_editor_get_ui_control (peditor));
  store = gtk_tree_view_get_model (list);

  path = find_string_in_model (store, curr_value, COL_NAME);

  /* Add a temporary item if we can't find a match
   * TODO: delete this item if it is no longer selected?
   */
  if (!path)
  {
    GtkListStore *list_store;
    GtkTreeIter iter, sort_iter;

    list_store = GTK_LIST_STORE (gtk_tree_model_sort_get_model (GTK_TREE_MODEL_SORT (store)));

    gtk_list_store_insert_with_values (list_store, &iter, 0,
                                       COL_LABEL, curr_value,
                                       COL_NAME, curr_value, -1);
    /* convert the tree store iter for use with the sort model */
    gtk_tree_model_sort_convert_child_iter_to_iter (GTK_TREE_MODEL_SORT (store),
                                                    &sort_iter, &iter);
    path = gtk_tree_model_get_string_from_iter (store, &sort_iter);
  }

  new_value = gconf_value_new (GCONF_VALUE_STRING);
  gconf_value_set_string (new_value, path);
  g_free (path);

  return new_value;
}

static GConfValue *
conv_from_widget_cb (GConfPropertyEditor *peditor, const GConfValue *value)
{
  GConfValue *new_value = NULL;
  GtkTreeIter iter;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeView *list;

  list = GTK_TREE_VIEW (gconf_property_editor_get_ui_control (peditor));
  selection = gtk_tree_view_get_selection (list);

  if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
    gchar *list_value;

    gtk_tree_model_get (model, &iter, COL_NAME, &list_value, -1);

    if (list_value) {
      new_value = gconf_value_new (GCONF_VALUE_STRING);
      gconf_value_set_string (new_value, list_value);
      g_free (list_value);
    }
  }

  return new_value;
}

static gint
cursor_theme_sort_func (GtkTreeModel *model,
                        GtkTreeIter *a,
                        GtkTreeIter *b,
                        gpointer user_data)
{
  gchar *a_label = NULL;
  gchar *b_label = NULL;
  const gchar *default_label;
  gint result;

  gtk_tree_model_get (model, a, COL_LABEL, &a_label, -1);
  gtk_tree_model_get (model, b, COL_LABEL, &b_label, -1);

  default_label = _("Default Pointer");

  if (!strcmp (a_label, default_label))
    result = -1;
  else if (!strcmp (b_label, default_label))
    result = 1;
  else
    result = strcmp (a_label, b_label);

  g_free (a_label);
  g_free (b_label);

  return result;
}

static void
update_color_buttons_from_string (const gchar *color_scheme, AppearanceData *data)
{
  GdkColor colors[NUM_SYMBOLIC_COLORS];
  GtkWidget *widget;
  gint i;

  if (!gnome_theme_color_scheme_parse (color_scheme, colors))
    return;

  /* now set all the buttons to the correct settings */
  for (i = 0; i < NUM_SYMBOLIC_COLORS; ++i) {
    widget = glade_xml_get_widget (data->xml, symbolic_names[i]);
    gtk_color_button_set_color (GTK_COLOR_BUTTON (widget), &colors[i]);
  }
}

static void
update_color_buttons_from_settings (GtkSettings *settings,
                                    AppearanceData *data)
{
  gchar *scheme, *setting;

  scheme = gconf_client_get_string (data->client, COLOR_SCHEME_KEY, NULL);
  g_object_get (settings, "gtk-color-scheme", &setting, NULL);

  if (scheme == NULL || strcmp (scheme, "") == 0)
    gtk_widget_set_sensitive (glade_xml_get_widget (data->xml, "color_scheme_defaults_button"), FALSE);

  g_free (scheme);
  update_color_buttons_from_string (setting, data);
  g_free (setting);
}

static void
color_scheme_changed (GObject    *settings,
                      GParamSpec *pspec,
                      AppearanceData  *data)
{
  update_color_buttons_from_settings (GTK_SETTINGS (settings), data);
}

static void
check_color_schemes_enabled (GtkSettings *settings,
                             AppearanceData *data)
{
  gchar *theme = NULL;
  gchar *filename;
  GSList *symbolic_colors = NULL;
  gboolean enable_colors = FALSE;
  gint i;

  g_object_get (settings, "gtk-theme-name", &theme, NULL);
  filename = gtkrc_find_named (theme);
  g_free (theme);

  gtkrc_get_details (filename, NULL, &symbolic_colors);
  g_free (filename);

  for (i = 0; i < NUM_SYMBOLIC_COLORS; ++i) {
    gboolean found;

    found = (g_slist_find_custom (symbolic_colors, symbolic_names[i], (GCompareFunc) strcmp) != NULL);
    gtk_widget_set_sensitive (glade_xml_get_widget (data->xml, symbolic_names[i]), found);

    enable_colors |= found;
  }

  g_slist_foreach (symbolic_colors, (GFunc) g_free, NULL);
  g_slist_free (symbolic_colors);

  gtk_widget_set_sensitive (glade_xml_get_widget (data->xml, "color_scheme_table"), enable_colors);
  gtk_widget_set_sensitive (glade_xml_get_widget (data->xml, "color_scheme_defaults_button"), enable_colors);

  if (enable_colors)
    gtk_widget_hide (glade_xml_get_widget (data->xml, "color_scheme_message_hbox"));
  else
    gtk_widget_show (glade_xml_get_widget (data->xml, "color_scheme_message_hbox"));
}

static void
color_button_clicked_cb (GtkWidget *colorbutton, AppearanceData *data)
{
  GtkWidget *widget;
  GdkColor color;
  GString *scheme = g_string_new (NULL);
  gchar *colstr;
  gchar *old_scheme = NULL;
  gint i;

  for (i = 0; i < NUM_SYMBOLIC_COLORS; ++i) {
    widget = glade_xml_get_widget (data->xml, symbolic_names[i]);
    gtk_color_button_get_color (GTK_COLOR_BUTTON (widget), &color);

    colstr = gdk_color_to_string (&color);
    g_string_append_printf (scheme, "%s:%s\n", symbolic_names[i], colstr);
    g_free (colstr);
  }
  /* remove the last newline */
  g_string_truncate (scheme, scheme->len - 1);

  /* verify that the scheme really has changed */
  g_object_get (gtk_settings_get_default (), "gtk-color-scheme", &old_scheme, NULL);

  if (!gnome_theme_color_scheme_equal (old_scheme, scheme->str)) {
    gconf_client_set_string (data->client, COLOR_SCHEME_KEY, scheme->str, NULL);

    gtk_widget_set_sensitive (glade_xml_get_widget (data->xml, "color_scheme_defaults_button"), TRUE);
  }
  g_free (old_scheme);
  g_string_free (scheme, TRUE);
}

static void
color_scheme_defaults_button_clicked_cb (GtkWidget *button, AppearanceData *data)
{
  gconf_client_unset (data->client, COLOR_SCHEME_KEY, NULL);
  gtk_widget_set_sensitive (glade_xml_get_widget (data->xml, "color_scheme_defaults_button"), FALSE);
}

static void
style_response_cb (GtkDialog *dialog, gint response_id)
{
  if (response_id == GTK_RESPONSE_HELP) {
    /* FIXME: help */
  } else {
    gtk_widget_hide (GTK_WIDGET (dialog));
  }
}

static void
gtk_theme_changed (GConfPropertyEditor *peditor,
		   const gchar *key,
		   const GConfValue *value,
		   AppearanceData *data)
{
  GnomeThemeInfo *theme = NULL;
  const gchar *name;
  GtkSettings *settings = gtk_settings_get_default ();

  if (value && (name = gconf_value_get_string (value))) {
    gchar *current;

    theme = gnome_theme_info_find (name);

    /* Manually update GtkSettings to new gtk+ theme.
     * This will eventually happen anyway, but we need the
     * info for the color scheme updates already. */
    g_object_get (settings, "gtk-theme-name", &current, NULL);

    if (strcmp (current, name) != 0)
      g_object_set (settings, "gtk-theme-name", name, NULL);

    g_free (current);

    check_color_schemes_enabled (settings, data);
    update_color_buttons_from_settings (settings, data);
  }

  gtk_widget_set_sensitive (glade_xml_get_widget (data->xml, "gtk_themes_delete"),
			    theme_is_writable (theme, THEME_TYPE_GTK));
}

static void
window_theme_changed (GConfPropertyEditor *peditor,
		      const gchar *key,
		      const GConfValue *value,
		      AppearanceData *data)
{
  GnomeThemeInfo *theme = NULL;
  const gchar *name;

  if (value && (name = gconf_value_get_string (value)))
    theme = gnome_theme_info_find (name);

  gtk_widget_set_sensitive (glade_xml_get_widget (data->xml, "window_themes_delete"),
			    theme_is_writable (theme, THEME_TYPE_WINDOW));
}

static void
icon_theme_changed (GConfPropertyEditor *peditor,
		    const gchar *key,
		    const GConfValue *value,
		    AppearanceData *data)
{
  GnomeThemeIconInfo *theme = NULL;
  const gchar *name;

  if (value && (name = gconf_value_get_string (value)))
    theme = gnome_theme_icon_info_find (name);

  gtk_widget_set_sensitive (glade_xml_get_widget (data->xml, "icon_themes_delete"),
			    theme_is_writable (theme, THEME_TYPE_ICON));
}

#ifdef HAVE_XCURSOR
static void
cursor_size_changed_cb (int size, AppearanceData *data)
{
  gconf_client_set_int (data->client, CURSOR_SIZE_KEY, size, NULL);
}

static void
cursor_size_scale_value_changed_cb (GtkRange *range, AppearanceData *data)
{
  GnomeThemeCursorInfo *theme;
  gchar *name;

  name = gconf_client_get_string (data->client, CURSOR_THEME_KEY, NULL);
  if (name == NULL)
    return;

  theme = gnome_theme_cursor_info_find (name);
  g_free (name);

  if (theme) {
    gint size;

    size = g_array_index (theme->sizes, gint, (int) gtk_range_get_value (range));
    cursor_size_changed_cb (size, data);
  }
}
#endif

static void
update_cursor_size_scale (GnomeThemeCursorInfo *theme,
                          AppearanceData *data)
{
#ifdef HAVE_XCURSOR
  GtkWidget *cursor_size_scale;
  GtkWidget *cursor_size_label;
  GtkWidget *cursor_size_small_label;
  GtkWidget *cursor_size_large_label;
  gboolean sensitive;
  gint size, gconf_size;

  cursor_size_scale = glade_xml_get_widget (data->xml, "cursor_size_scale");
  cursor_size_label = glade_xml_get_widget (data->xml, "cursor_size_label");
  cursor_size_small_label = glade_xml_get_widget (data->xml, "cursor_size_small_label");
  cursor_size_large_label = glade_xml_get_widget (data->xml, "cursor_size_large_label");

  sensitive = theme && theme->sizes->len > 1;
  gtk_widget_set_sensitive (cursor_size_scale, sensitive);
  gtk_widget_set_sensitive (cursor_size_label, sensitive);
  gtk_widget_set_sensitive (cursor_size_small_label, sensitive);
  gtk_widget_set_sensitive (cursor_size_large_label, sensitive);

  gconf_size = gconf_client_get_int (data->client, CURSOR_SIZE_KEY, NULL);

  if (sensitive) {
    GtkAdjustment *adjustment;
    gint i, index;
    GtkRange *range = GTK_RANGE (cursor_size_scale);

    adjustment = gtk_range_get_adjustment (range);
    g_object_set (adjustment, "upper", (gdouble) theme->sizes->len - 1, NULL);


    /* fallback if the gconf value is bigger than all available sizes;
       use the largest we have */
    index = theme->sizes->len - 1;

    /* set the slider to the cursor size which matches the gconf setting best  */
    for (i = 0; i < theme->sizes->len; i++) {
      size = g_array_index (theme->sizes, gint, i);

      if (size == gconf_size) {
      	index = i;
        break;
      } else if (size > gconf_size) {
        if (i == 0) {
          index = 0;
        } else {
          gint diff, diff_to_last;

          diff = size - gconf_size;
          diff_to_last = gconf_size - g_array_index (theme->sizes, gint, i - 1);

          index = (diff < diff_to_last) ? i : i - 1;
        }
        break;
      }
    }

    gtk_range_set_value (range, (gdouble) index);

    size = g_array_index (theme->sizes, gint, index);
  } else {
    if (theme->sizes->len > 0)
      size = g_array_index (theme->sizes, gint, 0);
    else
      size = 18;
  }

  if (size != gconf_size)
    cursor_size_changed_cb (size, data);
#endif
}

static void
cursor_theme_changed (GConfPropertyEditor *peditor,
		    const gchar *key,
		    const GConfValue *value,
		    AppearanceData *data)
{
  GnomeThemeCursorInfo *theme = NULL;
  const gchar *name;

  if (value && (name = gconf_value_get_string (value)))
    theme = gnome_theme_cursor_info_find (name);

  update_cursor_size_scale (theme, data);

  gtk_widget_set_sensitive (glade_xml_get_widget (data->xml, "cursor_themes_delete"),
			    theme_is_writable (theme, THEME_TYPE_CURSOR));

}

static void
generic_theme_delete (const gchar *tv_name, ThemeType type, AppearanceData *data)
{
  GtkTreeView *treeview = GTK_TREE_VIEW (glade_xml_get_widget (data->xml, tv_name));
  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);
  GtkTreeModel *model;
  GtkTreeIter iter;

  if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
    gchar *name;

    gtk_tree_model_get (model, &iter, COL_NAME, &name, -1);

    if (name != NULL && theme_delete (name, type)) {
      /* remove theme from the model, too */
      GtkTreeIter child;
      GtkTreePath *path;

      path = gtk_tree_model_get_path (model, &iter);
      gtk_tree_model_sort_convert_iter_to_child_iter (
          GTK_TREE_MODEL_SORT (model), &child, &iter);
      gtk_list_store_remove (GTK_LIST_STORE (
          gtk_tree_model_sort_get_model (GTK_TREE_MODEL_SORT (model))), &child);

      if (gtk_tree_model_get_iter (model, &iter, path) ||
          theme_model_iter_last (model, &iter)) {
        gtk_tree_path_free (path);
        path = gtk_tree_model_get_path (model, &iter);
	gtk_tree_selection_select_path (selection, path);
	gtk_tree_view_scroll_to_cell (treeview, path, NULL, FALSE, 0, 0);
      }
      gtk_tree_path_free (path);
    }
    g_free (name);
  }
}

static void
gtk_theme_delete_cb (GtkWidget *button, AppearanceData *data)
{
  generic_theme_delete ("gtk_themes_list", THEME_TYPE_GTK, data);
}

static void
window_theme_delete_cb (GtkWidget *button, AppearanceData *data)
{
  generic_theme_delete ("window_themes_list", THEME_TYPE_WINDOW, data);
}

static void
icon_theme_delete_cb (GtkWidget *button, AppearanceData *data)
{
  generic_theme_delete ("icon_themes_list", THEME_TYPE_ICON, data);
}

static void
cursor_theme_delete_cb (GtkWidget *button, AppearanceData *data)
{
  generic_theme_delete ("cursor_themes_list", THEME_TYPE_CURSOR, data);
}

static void
add_to_treeview (const gchar *tv_name,
		 const gchar *theme_name,
		 const gchar *theme_label,
		 GdkPixbuf *theme_thumbnail,
		 AppearanceData *data)
{
  GtkTreeView *treeview;
  GtkListStore *model;

  treeview = GTK_TREE_VIEW (glade_xml_get_widget (data->xml, tv_name));
  model = GTK_LIST_STORE (
          gtk_tree_model_sort_get_model (
          GTK_TREE_MODEL_SORT (gtk_tree_view_get_model (treeview))));

  gtk_list_store_insert_with_values (model, NULL, 0,
          COL_LABEL, theme_label,
          COL_NAME, theme_name,
          COL_THUMBNAIL, theme_thumbnail,
          -1);
}

static void
remove_from_treeview (const gchar *tv_name,
		      const gchar *theme_name,
		      AppearanceData *data)
{
  GtkTreeView *treeview;
  GtkListStore *model;
  GtkTreeIter iter;

  treeview = GTK_TREE_VIEW (glade_xml_get_widget (data->xml, tv_name));
  model = GTK_LIST_STORE (
          gtk_tree_model_sort_get_model (
          GTK_TREE_MODEL_SORT (gtk_tree_view_get_model (treeview))));

  if (theme_find_in_model (GTK_TREE_MODEL (model), theme_name, &iter))
    gtk_list_store_remove (model, &iter);
}

static void
update_in_treeview (const gchar *tv_name,
		    const gchar *theme_name,
		    const gchar *theme_label,
		    AppearanceData *data)
{
  GtkTreeView *treeview;
  GtkListStore *model;
  GtkTreeIter iter;

  treeview = GTK_TREE_VIEW (glade_xml_get_widget (data->xml, tv_name));
  model = GTK_LIST_STORE (
          gtk_tree_model_sort_get_model (
          GTK_TREE_MODEL_SORT (gtk_tree_view_get_model (treeview))));

  if (theme_find_in_model (GTK_TREE_MODEL (model), theme_name, &iter)) {
    gtk_list_store_set (model, &iter,
          COL_LABEL, theme_label,
          COL_NAME, theme_name,
          -1);
  }
}

static void
update_thumbnail_in_treeview (const gchar *tv_name,
		    const gchar *theme_name,
		    GdkPixbuf *theme_thumbnail,
		    AppearanceData *data)
{
  GtkTreeView *treeview;
  GtkListStore *model;
  GtkTreeIter iter;

  if (theme_thumbnail == NULL)
    return;

  treeview = GTK_TREE_VIEW (glade_xml_get_widget (data->xml, tv_name));
  model = GTK_LIST_STORE (
          gtk_tree_model_sort_get_model (
          GTK_TREE_MODEL_SORT (gtk_tree_view_get_model (treeview))));

  if (theme_find_in_model (GTK_TREE_MODEL (model), theme_name, &iter)) {
    gtk_list_store_set (model, &iter,
          COL_THUMBNAIL, theme_thumbnail,
          -1);
  }

  g_object_unref (theme_thumbnail);
}

static void
gtk_theme_thumbnail_cb (GdkPixbuf *pixbuf,
                        gchar *theme_name,
                        AppearanceData *data)
{
  update_thumbnail_in_treeview ("gtk_themes_list", theme_name, pixbuf, data);
}

static void
metacity_theme_thumbnail_cb (GdkPixbuf *pixbuf,
                             gchar *theme_name,
                             AppearanceData *data)
{
  update_thumbnail_in_treeview ("window_themes_list", theme_name, pixbuf, data);
}

static void
icon_theme_thumbnail_cb (GdkPixbuf *pixbuf,
                         gchar *theme_name,
                         AppearanceData *data)
{
  update_thumbnail_in_treeview ("icon_themes_list", theme_name, pixbuf, data);
}

static void
changed_on_disk_cb (GnomeThemeType       type,
		    gpointer             theme,
		    GnomeThemeChangeType change_type,
		    GnomeThemeElement    element,
		    AppearanceData      *data)
{
  if (type == GNOME_THEME_TYPE_REGULAR) {
    GnomeThemeInfo *info = theme;

    if (change_type == GNOME_THEME_CHANGE_DELETED) {
      if (info->has_gtk)
        remove_from_treeview ("gtk_themes_list", info->name, data);
      if (info->has_metacity)
        remove_from_treeview ("window_themes_list", info->name, data);

    } else {
      if (info->has_gtk) {
        if (change_type == GNOME_THEME_CHANGE_CREATED)
          add_to_treeview ("gtk_themes_list", info->name, info->name, data->gtk_theme_icon, data);
        else if (change_type == GNOME_THEME_CHANGE_CHANGED)
          update_in_treeview ("gtk_themes_list", info->name, info->name, data);

        generate_gtk_theme_thumbnail_async (info,
            (ThemeThumbnailFunc) gtk_theme_thumbnail_cb, data, NULL);
      }

      if (info->has_metacity) {
        if (change_type == GNOME_THEME_CHANGE_CREATED)
          add_to_treeview ("window_themes_list", info->name, info->name, data->window_theme_icon, data);
        else if (change_type == GNOME_THEME_CHANGE_CHANGED)
          update_in_treeview ("window_themes_list", info->name, info->name, data);

        generate_metacity_theme_thumbnail_async (info,
            (ThemeThumbnailFunc) metacity_theme_thumbnail_cb, data, NULL);
      }
    }

  } else if (type == GNOME_THEME_TYPE_ICON) {
    GnomeThemeIconInfo *info = theme;

    if (change_type == GNOME_THEME_CHANGE_DELETED) {
      remove_from_treeview ("icon_themes_list", info->name, data);
    } else {
      if (change_type == GNOME_THEME_CHANGE_CREATED)
        add_to_treeview ("icon_themes_list", info->name, info->readable_name, data->icon_theme_icon, data);
      else if (change_type == GNOME_THEME_CHANGE_CHANGED)
        update_in_treeview ("icon_themes_list", info->name, info->readable_name, data);

      generate_icon_theme_thumbnail_async (info,
          (ThemeThumbnailFunc) icon_theme_thumbnail_cb, data, NULL);
    }
  }
}

static void
prepare_list (AppearanceData *data, GtkWidget *list, ThemeType type, GCallback callback)
{
  GtkListStore *store;
  GList *l, *themes = NULL;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkTreeModel *sort_model;
  GdkPixbuf *thumbnail;
  const gchar *key;
  GObject *peditor;
  GConfValue *value;
  ThumbnailGenFunc generator;
  ThemeThumbnailFunc thumb_cb;

  switch (type)
  {
    case THEME_TYPE_GTK:
      themes = gnome_theme_info_find_by_type (GNOME_THEME_GTK_2);
      thumbnail = data->gtk_theme_icon;
      key = GTK_THEME_KEY;
      generator = (ThumbnailGenFunc) generate_gtk_theme_thumbnail_async;
      thumb_cb = (ThemeThumbnailFunc) gtk_theme_thumbnail_cb;
      break;

    case THEME_TYPE_WINDOW:
      themes = gnome_theme_info_find_by_type (GNOME_THEME_METACITY);
      thumbnail = data->window_theme_icon;
      key = METACITY_THEME_KEY;
      generator = (ThumbnailGenFunc) generate_metacity_theme_thumbnail_async;
      thumb_cb = (ThemeThumbnailFunc) metacity_theme_thumbnail_cb;
      break;

    case THEME_TYPE_ICON:
      themes = gnome_theme_icon_info_find_all ();
      thumbnail = data->icon_theme_icon;
      key = ICON_THEME_KEY;
      generator = (ThumbnailGenFunc) generate_icon_theme_thumbnail_async;
      thumb_cb = (ThemeThumbnailFunc) icon_theme_thumbnail_cb;
      break;

    case THEME_TYPE_CURSOR:
      themes = gnome_theme_cursor_info_find_all ();
      thumbnail = NULL;
      key = CURSOR_THEME_KEY;
      generator = NULL;
      thumb_cb = NULL;
      break;

    default:
      /* we don't deal with any other type of themes here */
      return;
  }

  store = gtk_list_store_new (NUM_COLS, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING);

  for (l = themes; l; l = g_list_next (l))
  {
    const gchar *name = NULL;
    const gchar *label = NULL;
    GtkTreeIter i;

    if (type == THEME_TYPE_GTK || type == THEME_TYPE_WINDOW) {
      name = ((GnomeThemeInfo *) l->data)->name;
    } else if (type == THEME_TYPE_ICON) {
      name = ((GnomeThemeIconInfo *) l->data)->name;
      label = ((GnomeThemeIconInfo *) l->data)->readable_name;
    } else if (type == THEME_TYPE_CURSOR) {
      name = ((GnomeThemeCursorInfo *) l->data)->name;
      label = ((GnomeThemeCursorInfo *) l->data)->readable_name;
    }

    if (!name)
      continue; /* just in case... */

    if (type == THEME_TYPE_CURSOR) {
      thumbnail = ((GnomeThemeCursorInfo *) l->data)->thumbnail;
    } else {
      generator (l->data, thumb_cb, data, NULL);
    }

    gtk_list_store_insert_with_values (store, &i, 0,
                                       COL_LABEL, label ? label : name,
                                       COL_NAME, name,
                                       COL_THUMBNAIL, thumbnail,
                                       -1);

    if (type == THEME_TYPE_CURSOR && thumbnail) {
      g_object_unref (thumbnail);
      thumbnail = NULL;
    }
  }
  g_list_free (themes);

  sort_model = gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (store));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (sort_model),
                                        COL_LABEL, GTK_SORT_ASCENDING);

  if (type == THEME_TYPE_CURSOR)
    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (sort_model), COL_LABEL,
                                     (GtkTreeIterCompareFunc) cursor_theme_sort_func,
                                     NULL, NULL);

  gtk_tree_view_set_model (GTK_TREE_VIEW (list), GTK_TREE_MODEL (sort_model));

  renderer = gtk_cell_renderer_pixbuf_new ();
  g_object_set (renderer, "xpad", 3, "ypad", 3, NULL);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_add_attribute (column, renderer, "pixbuf", COL_THUMBNAIL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);

  renderer = gtk_cell_renderer_text_new ();

  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_add_attribute (column, renderer, "text", COL_LABEL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);

  peditor = gconf_peditor_new_tree_view (NULL, key, list,
      "conv-to-widget-cb", conv_to_widget_cb,
      "conv-from-widget-cb", conv_from_widget_cb,
      NULL);
  g_signal_connect (peditor, "value-changed", callback, data);

  /* init the delete buttons */
  value = gconf_client_get (data->client, key, NULL);
  (*((void (*) (GConfPropertyEditor *, const gchar *, const GConfValue *, gpointer)) callback))
      (GCONF_PROPERTY_EDITOR (peditor), key, value, data);
  if (value)
    gconf_value_free (value);
}

void
style_init (AppearanceData *data)
{
  GtkSettings *settings;
  GtkWidget *w;
  gchar *label;
  gint i;

  data->gtk_theme_icon = gdk_pixbuf_new_from_file (GNOMECC_PIXMAP_DIR "/gtk-theme-thumbnailing.png", NULL);
  data->window_theme_icon = gdk_pixbuf_new_from_file (GNOMECC_PIXMAP_DIR "/window-theme-thumbnailing.png", NULL);
  data->icon_theme_icon = gdk_pixbuf_new_from_file (GNOMECC_PIXMAP_DIR "/icon-theme-thumbnailing.png", NULL);

  w = glade_xml_get_widget (data->xml, "theme_details");
  g_signal_connect (w, "response", (GCallback) style_response_cb, NULL);
  g_signal_connect (w, "delete_event", (GCallback) gtk_true, NULL);

  prepare_list (data, glade_xml_get_widget (data->xml, "window_themes_list"), THEME_TYPE_WINDOW, (GCallback) window_theme_changed);
  prepare_list (data, glade_xml_get_widget (data->xml, "gtk_themes_list"), THEME_TYPE_GTK, (GCallback) gtk_theme_changed);
  prepare_list (data, glade_xml_get_widget (data->xml, "icon_themes_list"), THEME_TYPE_ICON, (GCallback) icon_theme_changed);
  prepare_list (data, glade_xml_get_widget (data->xml, "cursor_themes_list"), THEME_TYPE_CURSOR, (GCallback) cursor_theme_changed);

  w = glade_xml_get_widget (data->xml, "color_scheme_message_hbox");
  gtk_widget_set_no_show_all (w, TRUE);

  w = glade_xml_get_widget (data->xml, "color_scheme_defaults_button");
  gtk_button_set_image (GTK_BUTTON (w),
                        gtk_image_new_from_stock (GTK_STOCK_REVERT_TO_SAVED,
                                                  GTK_ICON_SIZE_BUTTON));

  settings = gtk_settings_get_default ();
  g_signal_connect (settings, "notify::gtk-color-scheme", (GCallback) color_scheme_changed, data);

#ifdef HAVE_XCURSOR
  w = glade_xml_get_widget (data->xml, "cursor_size_scale");
  g_signal_connect (w, "value-changed", (GCallback) cursor_size_scale_value_changed_cb, data);

  w = glade_xml_get_widget (data->xml, "cursor_size_small_label");
  label = g_strdup_printf ("<small><i>%s</i></small>", gtk_label_get_text (GTK_LABEL (w)));
  gtk_label_set_markup (GTK_LABEL (w), label);
  g_free (label);

  w = glade_xml_get_widget (data->xml, "cursor_size_large_label");
  label = g_strdup_printf ("<small><i>%s</i></small>", gtk_label_get_text (GTK_LABEL (w)));
  gtk_label_set_markup (GTK_LABEL (w), label);
  g_free (label);
#else
  w = glade_xml_get_widget (data->xml, "cursor_size_hbox");
  gtk_widget_set_no_show_all (w, TRUE);
  gtk_widget_hide (w);
  gtk_widget_show (glade_xml_get_widget (data->xml, "cursor_message_hbox"));
  gtk_box_set_spacing (GTK_BOX (glade_xml_get_widget (data->xml, "cursor_vbox")), 12);
#endif

  /* connect signals */
  /* color buttons */
  for (i = 0; i < NUM_SYMBOLIC_COLORS; ++i)
    g_signal_connect (glade_xml_get_widget (data->xml, symbolic_names[i]), "color-set", (GCallback) color_button_clicked_cb, data);

  /* revert button */
  g_signal_connect (glade_xml_get_widget (data->xml, "color_scheme_defaults_button"), "clicked", (GCallback) color_scheme_defaults_button_clicked_cb, data);
  /* delete buttons */
  g_signal_connect (glade_xml_get_widget (data->xml, "gtk_themes_delete"), "clicked", (GCallback) gtk_theme_delete_cb, data);
  g_signal_connect (glade_xml_get_widget (data->xml, "window_themes_delete"), "clicked", (GCallback) window_theme_delete_cb, data);
  g_signal_connect (glade_xml_get_widget (data->xml, "icon_themes_delete"), "clicked", (GCallback) icon_theme_delete_cb, data);
  g_signal_connect (glade_xml_get_widget (data->xml, "cursor_themes_delete"), "clicked", (GCallback) cursor_theme_delete_cb, data);

  gnome_theme_info_register_theme_change ((ThemeChangedCallback) changed_on_disk_cb, data);
}

void
style_shutdown (AppearanceData *data)
{
  if (data->gtk_theme_icon)
    g_object_unref (data->gtk_theme_icon);
  if (data->window_theme_icon)
    g_object_unref (data->window_theme_icon);
  if (data->icon_theme_icon)
    g_object_unref (data->icon_theme_icon);
}
