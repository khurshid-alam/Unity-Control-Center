/* This program was written with lots of love under the GPL by Jonathan
 * Blandford <jrb@gnome.org>
 */

#include <config.h>

#include <string.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <glade/glade.h>
#include <libgnomevfs/gnome-vfs-async-ops.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include <libwindow-settings/gnome-wm-manager.h>

#include "theme-common.h"
#include "capplet-util.h"
#include "activate-settings-daemon.h"
#include "gconf-property-editor.h"
#include "file-transfer-dialog.h"

/* FIXME: This shouldn't be hardcoded
 */
#define METACITY_THEME_LOCATION "/usr/share/themes"

#define GTK_THEME_KEY     "/desktop/gnome/interface/gtk_theme"
#define WINDOW_THEME_KEY  "/desktop/gnome/applications/window_manager/theme"
#define METACITY_THEME_DIR "/apps/metacity/general"
#define METACITY_THEME_KEY METACITY_THEME_DIR "/theme"

#define MAX_ELEMENTS_BEFORE_SCROLLING 8

static void read_themes (GladeXML *dialog);

enum
{
  THEME_NAME_COLUMN,
  N_COLUMNS
};

enum
{
  TARGET_URI_LIST,
  TARGET_NS_URL
};

static GtkTargetEntry drop_types[] =
{
  {"text/uri-list", 0, TARGET_URI_LIST},
  {"_NETSCAPE_URL", 0, TARGET_NS_URL}
};

static gint n_drop_types = sizeof (drop_types) / sizeof (GtkTargetEntry);

static gboolean setting_model = FALSE;
static gboolean initial_scroll = TRUE;
static gboolean window_setting_model = FALSE;
static gboolean window_initial_scroll = TRUE;
static gboolean running_theme_install = FALSE;

static GladeXML *
create_dialog (void)
{
  GladeXML *dialog;

  dialog = glade_xml_new (GLADEDIR "/theme-properties.glade", NULL, NULL);

  return dialog;
}

static void
theme_selection_changed (GtkTreeSelection *selection,
			 const gchar      *gconf_key)
{
  GtkTreeModel *model;
  gchar *new_key;
  GConfClient *client;
  GtkTreeIter iter;

  if (setting_model)
    return;

  client = gconf_client_get_default ();

  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      gtk_tree_model_get (model, &iter,
			  THEME_NAME_COLUMN, &new_key,
			  -1);
    }
  else
    /* This shouldn't happen */
    {
      new_key = NULL;
    }

  if (new_key != NULL)
    {
      gchar *old_key;

      old_key = gconf_client_get_string (client, gconf_key, NULL);
      if (old_key && strcmp (old_key, new_key))
	{
	  gconf_client_set_string (client, gconf_key, new_key, NULL);
	}
      g_free (old_key);
    }
  else
    {
      gconf_client_unset (client, gconf_key, NULL);
    }
  g_free (new_key);
  g_object_unref (client);
}

static void
gtk_theme_selection_changed (GtkTreeSelection *selection,
			     gpointer          data)
{
  theme_selection_changed (selection, GTK_THEME_KEY);
}

static void
window_theme_selection_changed (GtkTreeSelection *selection,
				gpointer          data)
{
  theme_selection_changed (selection, METACITY_THEME_KEY);
}

static void
load_theme_names (GtkTreeView *tree_view,
		  GList       *theme_list,
		  char        *current_theme)
{
  GList *list;
  GtkTreeModel *model;
  GtkWidget *swindow;
  gint i = 0;
  gboolean current_theme_found = FALSE;
  GtkTreeRowReference *row_ref = NULL;

  swindow = GTK_WIDGET (tree_view)->parent;
  model = gtk_tree_view_get_model (tree_view);

  setting_model = TRUE;
  gtk_list_store_clear (GTK_LIST_STORE (model));
  
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swindow),
				  GTK_POLICY_NEVER, GTK_POLICY_NEVER);
  gtk_widget_set_usize (swindow, -1, -1);

  for (list = theme_list; list; list = list->next)
    {
      const char *name = list->data;
      GtkTreeIter iter;

      gtk_list_store_prepend (GTK_LIST_STORE (model), &iter);
      gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			  THEME_NAME_COLUMN, name,
			  -1);

      if (strcmp (current_theme, name) == 0)
	{
	  GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
	  row_ref = gtk_tree_row_reference_new (model, path);
	  gtk_tree_path_free (path);
	  current_theme_found = TRUE;
	}

      if (i == MAX_ELEMENTS_BEFORE_SCROLLING)
	{
	  GtkRequisition rectangle;
	  gtk_widget_size_request (GTK_WIDGET (tree_view), &rectangle);
	  gtk_widget_set_usize (swindow, -1, rectangle.height);
	  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swindow),
					  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	}
      i++;
    }

  if (! current_theme_found)
    {
      GtkTreeIter iter;
      GtkTreePath *path;

      gtk_list_store_prepend (GTK_LIST_STORE (model), &iter);
      gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			  THEME_NAME_COLUMN, current_theme,
			  -1);

      path = gtk_tree_model_get_path (model, &iter);
      row_ref = gtk_tree_row_reference_new (model, path);
      gtk_tree_path_free (path);
    }

  if (row_ref)
    {
      GtkTreePath *path;

      path = gtk_tree_row_reference_get_path (row_ref);
      gtk_tree_view_set_cursor (tree_view,path, NULL, FALSE);

      if (initial_scroll)
	{
	  gtk_tree_view_scroll_to_cell (tree_view, path, NULL, TRUE, 0.5, 0.0);
	  initial_scroll = FALSE;
	}
      
      gtk_tree_path_free (path);
      gtk_tree_row_reference_free (row_ref);
    }
  setting_model = FALSE;

  g_free (current_theme);
}

static void
read_themes (GladeXML *dialog)
{
  GList *theme_list, *gtk_theme_list, *metacity_theme_list;
  GList *list;
  GConfClient *client;
  char *current_theme;

  client = gconf_client_get_default ();

  /* Read in themes
   */
  theme_list = theme_common_get_list ();

  gtk_theme_list = NULL;
  metacity_theme_list = NULL;
  for (list = theme_list; list; list = list->next)
    {
      ThemeInfo *info = list->data;

      if (info->has_gtk)
	gtk_theme_list = g_list_prepend (gtk_theme_list, info->name);
      if (info->has_metacity){
	metacity_theme_list = g_list_prepend (metacity_theme_list, info->name);
      }
    }

  current_theme = gconf_client_get_string (client, GTK_THEME_KEY, NULL);
  if (current_theme == NULL)
    current_theme = g_strdup ("Default");

  load_theme_names (GTK_TREE_VIEW (WID ("theme_treeview")), gtk_theme_list, current_theme);
  
  current_theme = gconf_client_get_string (client, METACITY_THEME_KEY, NULL);
  if (current_theme == NULL)
    current_theme = g_strdup ("Default");

  load_theme_names (GTK_TREE_VIEW (WID ("window_theme_treeview")), metacity_theme_list, current_theme);

  g_list_free (gtk_theme_list);
  g_list_free (metacity_theme_list);
}

static void
window_read_themes (GladeXML *dialog)
{
  GConfClient *client;
  GList *window_theme_list;
  GList *list;
  GtkTreeModel *model;
  GtkTreeView *tree_view;
  gchar *current_theme;
  gint i = 0;
  gboolean current_theme_found = FALSE;
  GtkTreeRowReference *row_ref = NULL;
  GnomeWindowManager *wm;
  GdkScreen *screen;

  client = gconf_client_get_default ();

  screen = gdk_display_get_default_screen (gdk_display_get_default ());
  
  wm = gnome_wm_manager_get_current (screen);  
  if (wm != NULL) {
    window_theme_list = gnome_window_manager_get_theme_list (wm);
    g_object_unref (G_OBJECT (wm));
  } else
    window_theme_list = NULL;

  tree_view = GTK_TREE_VIEW (WID ("window_theme_treeview"));
  model = gtk_tree_view_get_model (tree_view);

  window_setting_model = TRUE;
  gtk_list_store_clear (GTK_LIST_STORE (model));

  current_theme = gconf_client_get_string (client, WINDOW_THEME_KEY, NULL);
  if (current_theme == NULL)
    current_theme = g_strdup ("Default");
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (WID ("window_theme_swindow")),
				  GTK_POLICY_NEVER, GTK_POLICY_NEVER);
  gtk_widget_set_usize (WID ("window_theme_swindow"), -1, -1);

  for (list = window_theme_list; list; list = list->next)
    {
      char *theme_name = list->data;
      GtkTreeIter iter;

      gtk_list_store_prepend (GTK_LIST_STORE (model), &iter);
      gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			  THEME_NAME_COLUMN, theme_name,
			  -1);

      if (strcmp (current_theme, theme_name) == 0)
	{
	  GtkTreeSelection *selection;

	  selection = gtk_tree_view_get_selection (tree_view);
	  gtk_tree_selection_select_iter (selection, &iter);
	  if (window_initial_scroll)
	    {
	      GtkTreePath *path;

	      path = gtk_tree_model_get_path (model, &iter);
	      row_ref = gtk_tree_row_reference_new (model, path);
	      gtk_tree_path_free (path);
	    }
	  current_theme_found = TRUE;
	}

      if (i == MAX_ELEMENTS_BEFORE_SCROLLING)
	{
	  GtkRequisition rectangle;
	  gtk_widget_size_request (GTK_WIDGET (tree_view), &rectangle);
	  gtk_widget_set_usize (WID ("window_theme_swindow"), -1, rectangle.height);
	  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (WID ("window_theme_swindow")),
					  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	}
      i++;
    }

  if (! current_theme_found)
    {
      GtkTreeSelection *selection = gtk_tree_view_get_selection (tree_view);
      GtkTreeIter iter;

      gtk_list_store_prepend (GTK_LIST_STORE (model), &iter);
      gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			  THEME_NAME_COLUMN, current_theme,
			  -1);
      gtk_tree_selection_select_iter (selection, &iter);
      if (window_initial_scroll)
	{
	  GtkTreePath *path;

	  path = gtk_tree_model_get_path (model, &iter);
	  row_ref = gtk_tree_row_reference_new (model, path);
	  gtk_tree_path_free (path);
	}
    }


  if (row_ref && window_initial_scroll)
    {
      GtkTreePath *path;

      path = gtk_tree_row_reference_get_path (row_ref);

      gtk_tree_view_scroll_to_cell (tree_view, path, NULL, TRUE, 0.5, 0.0);
      
      gtk_tree_path_free (path);
      gtk_tree_row_reference_free (row_ref);
      initial_scroll = FALSE;
    }
  window_setting_model = FALSE;

  g_free (current_theme);
}

static void
theme_key_changed (GConfClient *client,
		   guint        cnxn_id,
		   GConfEntry  *entry,
		   gpointer     user_data)
{
  if (strcmp (entry->key, GTK_THEME_KEY))
    return;

  read_themes ((GladeXML *)user_data);
}

static void
metacity_key_changed (GConfClient *client,
		      guint        cnxn_id,
		      GConfEntry  *entry,
		      gpointer     user_data)
{
  if (strcmp (entry->key, METACITY_THEME_KEY))
    return;

  read_themes ((GladeXML *)user_data);
}

static void
window_theme_key_changed (GConfClient *client,
		   guint        cnxn_id,
		   GConfEntry  *entry,
		   gpointer     user_data)
{
  if (strcmp (entry->key, WINDOW_THEME_KEY))
    return;

  window_read_themes ((GladeXML *)user_data);
}

static void
theme_changed_func (gpointer uri,
		    gpointer user_data)
{
  read_themes ((GladeXML *)user_data);
}

static gint
sort_func (GtkTreeModel *model,
	   GtkTreeIter  *a,
	   GtkTreeIter  *b,
	   gpointer      user_data)
{
  gchar *a_str = NULL;
  gchar *b_str = NULL;

 gtk_tree_model_get (model, a, 0, &a_str, -1);
 gtk_tree_model_get (model, b, 0, &b_str, -1);

 if (a_str == NULL) a_str = g_strdup ("");
 if (b_str == NULL) b_str = g_strdup ("");

 if (!strcmp (a_str, "Default"))
   return -1;
 if (!strcmp (b_str, "Default"))
   return 1;

 return g_utf8_collate (a_str, b_str);
}

static void
transfer_cancel_cb (GtkWidget *dlg, gchar *path)
{
	gnome_vfs_unlink (path);
	g_free (path);
	gtk_widget_destroy (dlg);
}

/* this works around problems when doing fork/exec in a threaded app
 * with some locks being held/waited on in different threads.
 *
 * we do the idle callback so that the async xfer has finished and
 * cleaned up its vfs job.  otherwise it seems the slave thread gets
 * woken up and it removes itself from the job queue before it is
 * supposed to.  very strange.
 *
 * see bugzilla.gnome.org #86141 for details
 */
static gboolean
transfer_done_targz_idle_cb (gpointer data)
{
	int status;
	gchar *command;
	gchar *path = data;

	/* this should be something more clever and nonblocking */
	command = g_strdup_printf ("sh -c 'gzip -d -c < \"%s\" | tar xf - -C \"%s/.themes\"'",
				   path, g_get_home_dir ());
	if (g_spawn_command_line_sync (command, NULL, NULL, &status, NULL) && status == 0)
		gnome_vfs_unlink (path);
	g_free (command);
	g_free (path);

	return FALSE;
}

static void
window_show_manage_themes (GtkWidget *button, gpointer data)
{
	gchar *path, *command;
	GnomeVFSURI *uri;
	GnomeWindowManager *wm;
	
	wm = gnome_wm_manager_get_current (gtk_widget_get_screen (button));
	
	path = gnome_window_manager_get_user_theme_folder (wm);
	g_object_unref (G_OBJECT (wm));

	uri = gnome_vfs_uri_new (path);

	if (!gnome_vfs_uri_exists (uri)) {
		/* Create the directory */
		gnome_vfs_make_directory_for_uri (uri, 0775);
	}
	gnome_vfs_uri_unref (uri);


	command = g_strdup_printf ("nautilus --no-desktop %s", path);
	g_free (path);

	g_spawn_command_line_async (command, NULL);
	g_free (command);
}


/* this works around problems when doing fork/exec in a threaded app
 * with some locks being held/waited on in different threads.
 *
 * we do the idle callback so that the async xfer has finished and
 * cleaned up its vfs job.  otherwise it seems the slave thread gets
 * woken up and it removes itself from the job queue before it is
 * supposed to.  very strange.
 *
 * see bugzilla.gnome.org #86141 for details
 */
static gboolean
transfer_done_tarbz2_idle_cb (gpointer data)
{
	int status;
	gchar *command;
	gchar *path = data;

	/* this should be something more clever and nonblocking */
	command = g_strdup_printf ("sh -c 'bzip2 -d -c < \"%s\" | tar xf - -C \"%s/.themes\"'",
				   path, g_get_home_dir ());
	if (g_spawn_command_line_sync (command, NULL, NULL, &status, NULL) && status == 0)
		gnome_vfs_unlink (path);
	g_free (command);
	g_free (path);

	return FALSE;
}

static void
transfer_done_cb (GtkWidget *dlg, gchar *path)
{
	int len = strlen (path);
	if (path && len > 7 && !strcmp (path + len - 7, ".tar.gz"))
		g_idle_add (transfer_done_targz_idle_cb, path);
	if (path && len > 8 && !strcmp (path + len - 8, ".tar.bz2"))
		g_idle_add (transfer_done_tarbz2_idle_cb, path);
	gtk_widget_destroy (dlg);
}

static void
install_dialog_response (GtkWidget *widget, int response_id, gpointer data)
{
	GladeXML *dialog = data;
	GtkWidget *dlg;
	gchar *filename, *path, *base;
	GList *src, *target;
	GnomeVFSURI *src_uri;
	const gchar *raw;
	
	if (response_id == GTK_RESPONSE_HELP) {
		capplet_help (GTK_WINDOW (widget),
			"user-guide.xml",
			"goscustdesk-12");
		return;
	}

	if (response_id == 0) {
		raw = gtk_entry_get_text (GTK_ENTRY (gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (WID ("install_theme_picker")))));
		if (raw == NULL || strlen (raw) <= 0)
			return;

		if (strncmp (raw, "http://", 7) && strncmp (raw, "ftp://", 6) && *raw != '/')
			filename = gnome_file_entry_get_full_path (GNOME_FILE_ENTRY (WID ("install_theme_picker")), TRUE);
		else
			filename = g_strdup (raw);
		if (filename == NULL)
			return;

		src_uri = gnome_vfs_uri_new (filename);
		base = gnome_vfs_uri_extract_short_name (src_uri);
		src = g_list_append (NULL, src_uri);
		path = g_build_filename (g_get_home_dir (), ".themes",
				         base, NULL);
		target = g_list_append (NULL, gnome_vfs_uri_new (path));
		
		dlg = file_transfer_dialog_new ();
		file_transfer_dialog_wrap_async_xfer (FILE_TRANSFER_DIALOG (dlg),
						      src, target,
						      GNOME_VFS_XFER_RECURSIVE,
						      GNOME_VFS_XFER_ERROR_MODE_QUERY,
						      GNOME_VFS_XFER_OVERWRITE_MODE_QUERY,
						      GNOME_VFS_PRIORITY_DEFAULT);
		gnome_vfs_uri_list_unref (src);
		gnome_vfs_uri_list_unref (target);
		g_free (base);
		g_free (filename);
		g_signal_connect (G_OBJECT (dlg), "cancel",
				  G_CALLBACK (transfer_cancel_cb), path);
		g_signal_connect (G_OBJECT (dlg), "done",
				  G_CALLBACK (transfer_done_cb), path);
		gtk_widget_show (dlg);
	}
}

static void
real_show_install_dialog (gpointer parent, gchar *filename)
{
	GladeXML *dialog;
	GtkWidget *widget;

	if (running_theme_install)
		return;

	running_theme_install = TRUE;

	dialog = glade_xml_new (GLADEDIR "/theme-install.glade", NULL, NULL);
	widget = WID ("install_dialog");
	
	g_signal_connect (G_OBJECT (widget), "response",
		G_CALLBACK (install_dialog_response), dialog);
	gtk_window_set_transient_for (GTK_WINDOW (widget), parent);
	gtk_window_set_position (GTK_WINDOW (widget), GTK_WIN_POS_CENTER_ON_PARENT);
	if (filename)
		gnome_file_entry_set_filename (GNOME_FILE_ENTRY (WID ("install_theme_picker")), filename);

	while (gtk_dialog_run (GTK_DIALOG (widget)) == GTK_RESPONSE_HELP)
		;

	gtk_widget_destroy (widget);
	g_object_unref (G_OBJECT (dialog));

	running_theme_install = FALSE;
}

static void
show_install_dialog (GtkWidget *button, gpointer parent)
{
  real_show_install_dialog (parent, NULL);
}
/* Callback issued during drag movements */

static gboolean
drag_motion_cb (GtkWidget *widget, GdkDragContext *context,
		gint x, gint y, guint time, gpointer data)
{
	return FALSE;
}

/* Callback issued during drag leaves */

static void
drag_leave_cb (GtkWidget *widget, GdkDragContext *context,
	       guint time, gpointer data)
{
	gtk_widget_queue_draw (widget);
}

/* Callback issued on actual drops. Attempts to load the file dropped. */
static void
drag_data_received_cb (GtkWidget *widget, GdkDragContext *context,
		       gint x, gint y,
		       GtkSelectionData *selection_data,
		       guint info, guint time, gpointer data)
{
	GList *uris;
	gchar *filename = NULL;

	if (!(info == TARGET_URI_LIST || info == TARGET_NS_URL))
		return;

	uris = gnome_vfs_uri_list_parse ((gchar *) selection_data->data);
	if (uris != NULL && uris->data != NULL) {
		GnomeVFSURI *uri = (GnomeVFSURI *) uris->data;
		filename = gnome_vfs_unescape_string (
			gnome_vfs_uri_get_path (uri), G_DIR_SEPARATOR_S);
		gnome_vfs_uri_list_unref (uris);
	}

	real_show_install_dialog (widget, filename);
	g_free (filename);
}

static void
show_manage_themes (GtkWidget *button, gpointer data)
{
	gchar *path, *command;
	GnomeVFSURI *uri;

	path = g_strdup_printf ("%s/.themes", g_get_home_dir ());
	uri = gnome_vfs_uri_new (path);

	if (!gnome_vfs_uri_exists (uri)) {
		/* Create the directory */
		gnome_vfs_make_directory_for_uri (uri, 0775);
	}
	gnome_vfs_uri_unref (uri);

	command = g_strdup_printf ("nautilus --no-desktop %s", path);
	g_free (path);

	g_spawn_command_line_async (command, NULL);
	g_free (command);
}

static void
cb_dialog_response (GtkDialog *dialog, gint response_id)
{
	if (response_id == GTK_RESPONSE_HELP)
		capplet_help (GTK_WINDOW (dialog),
			"user-guide.xml",
			"goscustdesk-12");
	else
		gtk_main_quit ();
}

static void
setup_tree_view (GtkTreeView *tree_view,
		 GCallback    changed_callback)
{
  GtkTreeModel *model;
  GtkTreeSelection *selection;
 
  gtk_tree_view_insert_column_with_attributes (tree_view,
 					       -1, NULL,
 					       gtk_cell_renderer_text_new (),
 					       "text", THEME_NAME_COLUMN,
 					       NULL);
  
  model = (GtkTreeModel *) gtk_list_store_new (N_COLUMNS, G_TYPE_STRING);
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (model), 0, sort_func, NULL, NULL);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model), 0, GTK_SORT_ASCENDING);
  gtk_tree_view_set_model (tree_view, model);
  selection = gtk_tree_view_get_selection (tree_view);
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
  g_signal_connect (G_OBJECT (selection), "changed", changed_callback, NULL);
}

static void
setup_dialog (GladeXML *dialog)
{
  GConfClient *client;
  GtkWidget *parent, *widget;

  client = gconf_client_get_default ();
  parent = WID ("theme_dialog");

  setup_tree_view (GTK_TREE_VIEW (WID ("theme_treeview")),
		   (GCallback)gtk_theme_selection_changed);
  setup_tree_view (GTK_TREE_VIEW (WID ("window_theme_treeview")),
		   (GCallback)window_theme_selection_changed);

  gconf_client_add_dir (client, "/desktop/gnome/interface", GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);
  gconf_client_add_dir (client, "/desktop/gnome/applications/window_manager", 
			GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);
  gconf_client_add_dir (client, METACITY_THEME_DIR,
			GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

  gconf_client_notify_add (client,
			   GTK_THEME_KEY,
			   (GConfClientNotifyFunc) &theme_key_changed,
			   dialog, NULL, NULL);
  gconf_client_notify_add (client,
			   WINDOW_THEME_KEY,
			   (GConfClientNotifyFunc) &window_theme_key_changed,
			   dialog, NULL, NULL);
  gconf_client_notify_add (client,
			   METACITY_THEME_KEY,
			   (GConfClientNotifyFunc) &metacity_key_changed,
			   dialog, NULL, NULL);

  read_themes (dialog);
  window_read_themes (dialog);

  theme_common_register_theme_change (theme_changed_func, dialog);

  /* app themes */
  widget = WID ("install_button");
  g_signal_connect (G_OBJECT (widget), "clicked",
		    G_CALLBACK (show_install_dialog), parent);
  widget = WID ("manage_button");
  g_signal_connect (G_OBJECT (widget), "clicked",
		    G_CALLBACK (show_manage_themes), dialog);

  /* window manager themes */
  widget = WID ("window_install_button");
  g_signal_connect (G_OBJECT (widget), "clicked",
		    G_CALLBACK (show_install_dialog), parent);
  widget = WID ("window_manage_button");
  g_signal_connect (G_OBJECT (widget), "clicked",
		    G_CALLBACK (window_show_manage_themes), dialog);

  /*
  g_signal_connect (G_OBJECT (WID ("install_dialog")), "response",
		    G_CALLBACK (install_dialog_response), dialog);
  */

  g_signal_connect (G_OBJECT (parent),
		    "response",
		    G_CALLBACK (cb_dialog_response), NULL);

  gtk_drag_dest_set (parent, GTK_DEST_DEFAULT_ALL,
		     drop_types, n_drop_types,
		     GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_MOVE);

  g_signal_connect (G_OBJECT (parent), "drag-motion",
		    G_CALLBACK (drag_motion_cb), NULL);
  g_signal_connect (G_OBJECT (parent), "drag-leave",
		    G_CALLBACK (drag_leave_cb), NULL);
  g_signal_connect (G_OBJECT (parent), "drag-data-received",
		    G_CALLBACK (drag_data_received_cb),
		    dialog);

  capplet_set_icon (parent, "gnome-ccthemes.png");
  gtk_widget_show (parent);
}

int
main (int argc, char *argv[])
{
  GladeXML *dialog;

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  gnome_program_init ("gnome-theme-properties", VERSION,
		      LIBGNOMEUI_MODULE, argc, argv,
		      GNOME_PARAM_APP_DATADIR, GNOMECC_DATA_DIR,
		      NULL);

  activate_settings_daemon ();

  dialog = create_dialog ();
  gnome_wm_manager_init ();

  setup_dialog (dialog);
  gtk_main ();

  return 0;
}
