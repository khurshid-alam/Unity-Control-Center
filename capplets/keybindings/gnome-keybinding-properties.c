#include <config.h>

#include <string.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <glade/glade.h>

#include "theme-common.h"
#include "capplet-util.h"
#include "eggcellrendererkeys.h"
#include "activate-settings-daemon.h"

#define LABEL_DATA "gnome-keybinding-properties-label"

enum
{
  DESCRIPTION_COLUMN,
  KEYENTRY_COLUMN,
  N_COLUMNS
};

typedef struct
{
  char *gconf_key;
  guint keyval;
  GdkModifierType mask;
  gboolean editable;
  GtkTreeModel *model;
  guint gconf_cnxn;
} KeyEntry;

GList *signals = NULL;

static GladeXML *
create_dialog (void)
{
  GladeXML *dialog;

  dialog = glade_xml_new ("gnome-keybinding-properties.glade", "gnome-keybinding-dialog", NULL);
  if (dialog == NULL)
    dialog = glade_xml_new (GNOMECC_DATA_DIR "/interfaces/gnome-keybinding-properties.glade", "gnome-keybinding-dialog", NULL);

  return dialog;
}

static char*
binding_name (guint            keyval,
              GdkModifierType  mask,
              gboolean         translate)
{
  if (keyval != 0)
    return gtk_accelerator_name (keyval, mask);
  else
    return translate ? g_strdup (_("Disabled")) : g_strdup ("disabled");
}

static void
accel_set_func (GtkTreeViewColumn *tree_column,
                GtkCellRenderer   *cell,
                GtkTreeModel      *model,
                GtkTreeIter       *iter,
                gpointer           data)
{
  KeyEntry *key_entry;
  
  gtk_tree_model_get (model, iter,
                      KEYENTRY_COLUMN, &key_entry,
                      -1);
  
  if (key_entry == NULL)
    g_object_set (G_OBJECT (cell),
		  "visible", FALSE,
		  NULL);
  else if (! key_entry->editable)
    g_object_set (G_OBJECT (cell),
		  "visible", TRUE,
		  "editable", FALSE,
		  "accel_key", key_entry->keyval,
		  "accel_mask", key_entry->mask,
		  "style", PANGO_STYLE_ITALIC,
		  NULL);
  else
    g_object_set (G_OBJECT (cell),
		  "visible", TRUE,
		  "editable", TRUE,
		  "accel_key", key_entry->keyval,
		  "accel_mask", key_entry->mask,
		  "style", PANGO_STYLE_NORMAL,
		  NULL);
}

static void
keybinding_key_changed (GConfClient *client,
			guint        cnxn_id,
			GConfEntry  *entry,
			gpointer     user_data)
{
  KeyEntry *key_entry;
  const gchar *key_value;
  GtkTreePath *path;
  GtkTreeIter iter;
  gboolean valid;
  
  key_entry = (KeyEntry *)user_data;
  key_value = gconf_value_get_string (entry->value);

  if (key_value != NULL)
    {
      gtk_accelerator_parse (key_value, &key_entry->keyval, &key_entry->mask);
    }
  else
    {
      key_entry->keyval = 0;
      key_entry->mask = 0;
    }

  path = gtk_tree_path_new_first ();
  for (valid = gtk_tree_model_get_iter_first (key_entry->model, &iter);
       valid;
       valid = gtk_tree_model_iter_next (key_entry->model, &iter))
    {
      KeyEntry *tmp_key_entry;

      gtk_tree_model_get (key_entry->model, &iter,
			  KEYENTRY_COLUMN, &tmp_key_entry,
			  -1);
      if (tmp_key_entry == key_entry)
	{
	  gtk_tree_model_row_changed (key_entry->model, path, &iter);
	}
      gtk_tree_path_next (path);
    }
  gtk_tree_path_free (path);
}


static void
clear_old_model (GtkWidget *tree_view)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  KeyEntry *key_entry;
  gboolean valid;
  GConfClient *client;

  client = gconf_client_get_default ();
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (tree_view));
  if (model == NULL)
    return;

  g_object_ref (model);
  gtk_tree_view_set_model (GTK_TREE_VIEW (tree_view), NULL);

  for (valid = gtk_tree_model_get_iter_first (model, &iter);
       valid;
       valid = gtk_tree_model_iter_next (model, &iter))
    {
      gtk_tree_model_get (model, &iter,
			  KEYENTRY_COLUMN, &key_entry,
			  -1);
      gconf_client_notify_remove (client, key_entry->gconf_cnxn);
      g_free (key_entry->gconf_key);
      g_free (key_entry);
    }

  g_object_unref (model);
}

static void
setup_tree (GladeXML *dialog,
	    GSList   *keys_list)
{
  GConfClient *client;
  GtkTreeModel *model;
  GSList *list;
  gint i = 0;

  clear_old_model (WID ("shortcut_treeview"));
  client = gconf_client_get_default ();
  model = (GtkTreeModel *) gtk_list_store_new (N_COLUMNS, G_TYPE_STRING, G_TYPE_POINTER);

  for (list = keys_list; list; list = list->next)
    {
      GConfEntry *entry;
      GConfSchema *schema = NULL;
      KeyEntry *key_entry;
      GError *error = NULL;
      GtkTreeIter iter;
      gchar *key_string;
      gchar *key_value;

      key_string = g_strdup ((gchar *) list->data);
      entry = gconf_client_get_entry (client,
                                      key_string,
				      NULL,
				      TRUE,
				      &error);
      if (error || entry == NULL)
	{
	  /* We don't actually want to popup a dialog */
	  g_free (key_string);
	  if (error)
	    g_error_free (error);
	  continue;
	}

      if (entry->schema_name)
	schema = gconf_client_get_schema (client, entry->schema_name, &error);
      
      if (error || schema == NULL)
	{
	  /* We don't actually want to popup a dialog */
	  g_free (key_string);
	  if (error)
	    g_error_free (error);
	  continue;
	}

      key_value = gconf_client_get_string (client, key_string, &error);

      key_entry = g_new0 (KeyEntry, 1);
      key_entry->gconf_key = key_string;
      key_entry->editable = TRUE;
      key_entry->model = model;
      gconf_client_add_dir (client, key_string, GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);
      key_entry->gconf_cnxn = gconf_client_notify_add (client,
						       key_string,
						       (GConfClientNotifyFunc) &keybinding_key_changed,
						       key_entry, NULL, NULL);
      if (key_value)
	{
	  gtk_accelerator_parse (key_value, &key_entry->keyval, &key_entry->mask);
	}
      else
	{
	  key_entry->keyval = 0;
	  key_entry->mask = 0;
	}
      g_free (key_value);

      i++;
      gtk_list_store_append (GTK_LIST_STORE (model), &iter);
      if (schema->short_desc)
	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    DESCRIPTION_COLUMN, schema->short_desc,
			    KEYENTRY_COLUMN, key_entry,
			    -1);
      else
	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    DESCRIPTION_COLUMN, _("<Unknown Action>"),
			    KEYENTRY_COLUMN, key_entry,
			    -1);
      gconf_entry_free (entry);
      gconf_schema_free (schema);
    }

  if (i > 8)
    {
      gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (WID ("actions_swindow")),
				      GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    }

  if (i == 0)
    {
      gtk_widget_hide (WID ("shortcuts_frame"));
    }
  else
    {
      gtk_widget_show (WID ("shortcuts_frame"));
    }
  gtk_tree_view_set_model (GTK_TREE_VIEW (WID ("shortcut_treeview")), model);
}

static void
keybinding_key_list_changed (GConfClient *client,
			     guint        cnxn_id,
			     GConfEntry  *entry,
			     gpointer     user_data)
{
  GladeXML *dialog;
  GSList *value_list, *list;
  GSList *string_list = NULL;

  dialog = user_data;

  if (strcmp (entry->key, "/apps/gnome_keybinding_properties/keybinding_key_list"))
    return;

  value_list = gconf_value_get_list (entry->value);
  for (list = value_list; list; list = list->next)
    {
      GConfValue *value = list->data;
      string_list = g_slist_append (string_list, (char *)gconf_value_get_string (value));
    }
  setup_tree (dialog, string_list);
  g_slist_free (string_list);
}

static void
key_theme_changed (GConfClient *client,
		   guint        cnxn_id,
		   GConfEntry  *entry,
		   gpointer     user_data)
{
  GtkWidget *omenu = (GtkWidget *) user_data;
  GtkWidget *menu;
  GtkWidget *menu_item;
  GConfValue *value;
  const gchar *new_key_theme;
  GList *list;
  gint i = 0;

  menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (omenu));
  value = gconf_entry_get_value (entry);
  new_key_theme = gconf_value_get_string (value);

  for (list = GTK_MENU_SHELL (menu)->children; list; list = list->next, i++)
    {
      gchar *text;

      menu_item = GTK_WIDGET (list->data);
      text = g_object_get_data (G_OBJECT (menu_item), LABEL_DATA);
      if (! strcmp (text, new_key_theme))
	{
	  if (gtk_option_menu_get_history (GTK_OPTION_MENU (omenu)) != i)
	    gtk_option_menu_set_history (GTK_OPTION_MENU (omenu), i);
	  return;
	}
    }

  /* We didn't find our theme.  Add it to our list. */
  menu_item = gtk_menu_item_new_with_label (new_key_theme);
  g_object_set_data_full (G_OBJECT (menu_item), LABEL_DATA, g_strdup (new_key_theme), g_free);
  gtk_widget_show (menu_item);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
  gtk_option_menu_set_history (GTK_OPTION_MENU (omenu), i);
}


static void
accel_edited_callback (GtkCellRendererText *cell,
                       const char          *path_string,
                       guint                keyval,
                       GdkModifierType      mask,
                       gpointer             data)
{
  GtkTreeView *view = (GtkTreeView *)data;
  GtkTreeModel *model;
  GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
  GtkTreeIter iter;
  KeyEntry *key_entry;
  GError *err = NULL;
  char *str;

  model = gtk_tree_view_get_model (view);
  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get (model, &iter,
		      KEYENTRY_COLUMN, &key_entry,
		      -1);

  /* sanity check */
  if (key_entry == NULL)
    return;

  str = binding_name (keyval, mask, FALSE);

  gconf_client_set_string (gconf_client_get_default(),
                           key_entry->gconf_key,
                           str,
                           &err);
  g_free (str);
  
  if (err != NULL)
    {
      GtkWidget *dialog;

      dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (view))),
				       GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
				       GTK_MESSAGE_WARNING,
				       GTK_BUTTONS_OK,
				       "Error setting new accelerator in configuration database: %s\n",
				       err->message);
      gtk_dialog_run (GTK_DIALOG (dialog));

      gtk_widget_destroy (dialog);
      g_error_free (err);
      key_entry->editable = FALSE;
    }
  
  gtk_tree_path_free (path);
}

static void
setup_dialog (GladeXML *dialog)
{
  GConfClient *client;
  GList *key_theme_list;
  GtkCellRenderer *renderer;
  GSList *keys_list;
  GtkWidget *widget;

  client = gconf_client_get_default ();

  key_theme_list = theme_common_get_list ();

  if (key_theme_list == NULL)
    {
      GtkWidget *msg_dialog = gtk_message_dialog_new (NULL, 0,
						      GTK_MESSAGE_ERROR,
						      GTK_BUTTONS_OK,
						      _("Unable to find any keyboard themes.  This means your GTK+ "
							"installation has been incompletely installed."));
      gtk_dialog_run (GTK_DIALOG (msg_dialog));
      gtk_widget_destroy (msg_dialog);

      gtk_widget_hide (WID ("shortcut_hbox"));
    }
  else
    {
      GtkWidget *omenu;
      GtkWidget *menu;
      GtkWidget *menu_item;
      GList *list;

      omenu = WID ("key_theme_omenu");
      menu = gtk_menu_new ();
      for (list = key_theme_list; list; list = list->next)
	{
	  ThemeInfo *info = list->data;

	  /* Make sure we have a default */
	  if (!strcasecmp (info->name, "default"))
	    {
	      menu_item = gtk_menu_item_new_with_label (info->name);
	      g_object_set_data_full (G_OBJECT (menu_item), LABEL_DATA, g_strdup (info->name), g_free);
	      gtk_widget_show (menu_item);
	      gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	    }
	}
      for (list = key_theme_list; list; list = list->next)
	{
	  ThemeInfo *info = list->data;

	  if (strcasecmp (info->name, "default"))
	    {
	      menu_item = gtk_menu_item_new_with_label (info->name);
	      g_object_set_data_full (G_OBJECT (menu_item), LABEL_DATA, g_strdup (info->name), g_free);
	      gtk_widget_show (menu_item);
	      gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	    }
	}
      gtk_widget_show (menu);
      gtk_option_menu_set_menu (GTK_OPTION_MENU (omenu), menu);

      gconf_client_add_dir (client, "/desktop/gnome/interface", GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);
      gconf_client_notify_add (client,
			       "/desktop/gnome/interface/gtk_key_theme",
			       (GConfClientNotifyFunc) &key_theme_changed,
			       omenu, NULL, NULL);
    }


  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (WID ("shortcut_treeview")),
					       -1,
					       _("_Action"),
					       gtk_cell_renderer_text_new (),
					       "text", DESCRIPTION_COLUMN,
					       NULL);
  renderer = (GtkCellRenderer *) g_object_new (EGG_TYPE_CELL_RENDERER_KEYS,
					       "editable", TRUE,
					       NULL);
  g_signal_connect (G_OBJECT (renderer),
		    "keys_edited",
                    G_CALLBACK (accel_edited_callback),
                    WID ("shortcut_treeview"));
  gtk_tree_view_insert_column_with_data_func (GTK_TREE_VIEW (WID ("shortcut_treeview")),
					      -1, _("_Shortcut"),
					      renderer,
					      accel_set_func, NULL, NULL);
  gconf_client_add_dir (client, "/apps/gnome_keybinding_properties", GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);
  gconf_client_notify_add (client,
			   "/apps/gnome_keybinding_properties/keybinding_key_list",
			   (GConfClientNotifyFunc) &keybinding_key_list_changed,
			   dialog, NULL, NULL);
  keys_list = gconf_client_get_list (client, "/apps/gnome_keybinding_properties/keybinding_key_list", GCONF_VALUE_STRING,  NULL);
  setup_tree (dialog, keys_list);

  widget = WID ("gnome-keybinding-dialog");
  gtk_widget_show (widget);

  g_signal_connect (G_OBJECT (widget), "response", gtk_main_quit, NULL);
  g_signal_connect (G_OBJECT (widget), "close", gtk_main_quit, NULL);
}

int
main (int argc, char *argv[])
{
  GladeXML *dialog;

  gtk_init (&argc, &argv);

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  gnome_program_init (argv[0], VERSION, LIBGNOMEUI_MODULE, argc, argv,
		      GNOME_PARAM_APP_DATADIR, GNOMECC_DATA_DIR,
		      NULL);

  activate_settings_daemon ();

  dialog = create_dialog ();
  setup_dialog (dialog);

  gtk_main ();

  return 0;
}
