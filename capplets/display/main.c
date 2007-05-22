#include <config.h>

#include <string.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <glade/glade.h>
#include <gdk/gdkx.h>

#include <X11/extensions/Xrandr.h>

#include "capplet-util.h"

enum {
	COL_NAME,
	COL_ID,
	N_COLUMNS
};

#define REVERT_COUNT 20

static struct {
	Rotation  rotation;
	gchar const    * name;
} const rotations[] = {
	{RR_Rotate_0,   N_("Normal")},
	{RR_Rotate_90,  N_("Left")},
	{RR_Rotate_180, N_("Inverted")},
	{RR_Rotate_270, N_("Right")}
};

static Rotation
display_rotation_from_text(gchar const* text) {
	int i = 0;
	g_return_val_if_fail(text, RR_Rotate_0);

	for(; i < G_N_ELEMENTS(rotations); i++) {
		if(!strcmp(text, _(rotations[i].name))) {
			break;
		}
	}

	g_return_val_if_fail(i < G_N_ELEMENTS(rotations), RR_Rotate_0);

	return rotations[i].rotation;
}

struct ScreenInfo
{
  int current_width;
  int current_height;
  SizeID current_size;
  short current_rate;
  
  Rotation current_rotation;
  Rotation old_rotation;
  Rotation rotations;

  SizeID old_size;
  short old_rate;
  
  XRRScreenConfiguration *config;
  XRRScreenSize *sizes;
  int n_sizes;
  
  GtkWidget *resolution_widget;
  GtkWidget *rate_widget;
  GtkWidget *rotate_widget;
};

struct DisplayInfo {
  int n_screens;
  struct ScreenInfo *screens;

  GtkWidget *per_computer_check;
  gboolean was_per_computer;
};


static void generate_rate_menu (struct ScreenInfo *screen_info);
static void generate_resolution_menu(struct ScreenInfo* screen_info);

static struct DisplayInfo *
read_display_info (GdkDisplay *display)
{
  struct DisplayInfo *info;
  struct ScreenInfo *screen_info;
  GdkScreen *screen;
  GdkWindow *root_window;
  int i;

  info = g_new (struct DisplayInfo, 1);
  info->n_screens = gdk_display_get_n_screens (display);
  info->screens = g_new (struct ScreenInfo, info->n_screens);

  for (i = 0; i < info->n_screens; i++)
    {
      screen = gdk_display_get_screen (display, i);
      
      screen_info = &info->screens[i];
      screen_info->current_width = gdk_screen_get_width (screen);
      screen_info->current_height = gdk_screen_get_height (screen);

      root_window = gdk_screen_get_root_window (screen);
      screen_info->config = XRRGetScreenInfo (gdk_x11_display_get_xdisplay (display),
					      gdk_x11_drawable_get_xid (GDK_DRAWABLE (root_window)));
      
      screen_info->current_rate = XRRConfigCurrentRate (screen_info->config);
      screen_info->current_size = XRRConfigCurrentConfiguration (screen_info->config, &screen_info->current_rotation);
      screen_info->sizes        = XRRConfigSizes (screen_info->config, &screen_info->n_sizes);
      screen_info->rotations    = XRRConfigRotations (screen_info->config, &screen_info->current_rotation);
    }

  return info;
}

static void
update_display_info (struct DisplayInfo *info, GdkDisplay *display)
{
  struct ScreenInfo *screen_info;
  GdkScreen *screen;
  GdkWindow *root_window;
  int i;

  g_assert (info->n_screens == gdk_display_get_n_screens (display));
  
  for (i = 0; i < info->n_screens; i++)
    {
      screen = gdk_display_get_screen (display, i);
      
      screen_info = &info->screens[i];

      screen_info->old_rate = screen_info->current_rate;
      screen_info->old_size = screen_info->current_size;
      screen_info->old_rotation = screen_info->current_rotation;
      
      screen_info->current_width = gdk_screen_get_width (screen);
      screen_info->current_height = gdk_screen_get_height (screen);

      root_window = gdk_screen_get_root_window (screen);
      XRRFreeScreenConfigInfo (screen_info->config);
      screen_info->config = XRRGetScreenInfo (gdk_x11_display_get_xdisplay (display),
					      gdk_x11_drawable_get_xid (GDK_DRAWABLE (root_window)));

      screen_info->current_rate = XRRConfigCurrentRate (screen_info->config);
      screen_info->current_size = XRRConfigCurrentConfiguration (screen_info->config, &screen_info->current_rotation);
      screen_info->sizes        = XRRConfigSizes (screen_info->config, &screen_info->n_sizes);
      screen_info->rotations    = XRRConfigRotations (screen_info->config, &screen_info->current_rotation);
    }
}

static int
get_current_resolution (struct ScreenInfo *screen_info)
{
  GtkComboBox* combo = GTK_COMBO_BOX (screen_info->resolution_widget);
  GtkTreeIter iter;
  int i = 0;

  if (gtk_combo_box_get_active_iter (combo, &iter))
  {
    gtk_tree_model_get (gtk_combo_box_get_model (combo), &iter,
			COL_ID, &i,
			-1);
  }

  return i;
}

static int
get_current_rate (struct ScreenInfo *screen_info)
{
  GtkComboBox* combo;
  GtkTreeIter  iter;
  int i = 0;

  combo = GTK_COMBO_BOX (screen_info->rate_widget);

  if (gtk_combo_box_get_active_iter (combo, &iter))
  {
    gtk_tree_model_get (gtk_combo_box_get_model (combo), &iter,
  		        COL_ID, &i,
  		        -1);
  }

  return i;
}

static Rotation
get_current_rotation(struct ScreenInfo* screen_info) {
	gchar* text;
	Rotation rot;
	text = gtk_combo_box_get_active_text (GTK_COMBO_BOX (screen_info->rotate_widget));
	rot = display_rotation_from_text (text);
	g_free (text);
	return rot;
}

static gboolean
apply_config (struct DisplayInfo *info)
{
  int i;
  GdkDisplay *display;
  Display *xdisplay;
  GdkScreen *screen;
  gboolean changed;

  display = gdk_display_get_default ();
  xdisplay = gdk_x11_display_get_xdisplay (display);

  changed = FALSE;
  for (i = 0; i < info->n_screens; i++)
    {
      struct ScreenInfo *screen_info = &info->screens[i];
      Status status;
      GdkWindow *root_window;
      int new_res, new_rate;
      Rotation new_rot;

      screen = gdk_display_get_screen (display, i);
      root_window = gdk_screen_get_root_window (screen);

      new_res  = get_current_resolution (screen_info);
      new_rate = get_current_rate (screen_info);
      new_rot  = get_current_rotation (screen_info);
      
      if (new_res  != screen_info->current_size ||
	  new_rate != screen_info->current_rate ||
	  new_rot  != screen_info->current_rotation)
	{
	  changed = TRUE; 
	  status = XRRSetScreenConfigAndRate (xdisplay, 
					      screen_info->config,
					      gdk_x11_drawable_get_xid (GDK_DRAWABLE (root_window)),
					      new_res,
					      new_rot,
					      new_rate > 0 ? new_rate : 0,
					      GDK_CURRENT_TIME);
	}
    }

  update_display_info (info, display);
  
  if (changed) {
    gchar *cmd;

    if ((cmd = g_find_program_in_path ("gnome-screensaver-command")))
      g_free (cmd);
    else {
      /* xscreensaver should handle this itself, but does not currently so we hack
       * it.  Ignore failures in case xscreensaver is not installed */
      g_spawn_command_line_async ("xscreensaver-command -restart", NULL);
    }
  }

  return changed;
}

static int
revert_config (struct DisplayInfo *info)
{
  int i;
  GdkDisplay *display;
  Display *xdisplay;
  GdkScreen *screen;
  char *cmd;

  display = gdk_display_get_default ();
  xdisplay = gdk_x11_display_get_xdisplay (display);
  
  for (i = 0; i < info->n_screens; i++)
    {
      struct ScreenInfo *screen_info = &info->screens[i];
      Status status;
      GdkWindow *root_window;

      screen = gdk_display_get_screen (display, i);
      root_window = gdk_screen_get_root_window (screen);

      status = XRRSetScreenConfigAndRate (xdisplay, 
					  screen_info->config,
					  gdk_x11_drawable_get_xid (GDK_DRAWABLE (root_window)),
					  screen_info->old_size,
					  screen_info->old_rotation,
					  screen_info->old_rate > 0 ? screen_info->old_rate : 0,
					  GDK_CURRENT_TIME);
      
    }

  update_display_info (info, display);

  /* Need to update the menus to the new settings */
  for (i = 0; i < info->n_screens; i++)
    {
      struct ScreenInfo *screen_info = &info->screens[i];
      
      generate_resolution_menu (screen_info);
      generate_rate_menu (screen_info);
    }

  if ((cmd = g_find_program_in_path ("gnome-screensaver-command")))
    g_free (cmd);
  else {
    /* xscreensaver should handle this itself, but does not currently so we hack
     * it.  Ignore failures in case xscreensaver is not installed */
    g_spawn_command_line_async ("xscreensaver-command -restart", NULL);
  }

  return 0;
}

static GtkWidget *
wrap_in_label (GtkWidget *child, char *text)
{
  GtkWidget *vbox, *hbox;
  GtkWidget *label;
  char *str;

  vbox = gtk_vbox_new (FALSE, 6);
  label = NULL;

  label = gtk_label_new (NULL);

  str = g_strdup_printf ("<b>%s</b>", text);
  gtk_label_set_markup (GTK_LABEL (label), str);
  g_free (str);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_widget_show (label);
  gtk_box_pack_start (GTK_BOX (vbox),
		      label,
		      FALSE, FALSE, 0);

  hbox = gtk_hbox_new (FALSE, 0);

  label = gtk_label_new ("    ");
  gtk_widget_show (label);
  gtk_box_pack_start (GTK_BOX (hbox),
		      label,
		      FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (hbox),
		      child,
		      TRUE, TRUE, 0);

  gtk_widget_show (hbox);
  
  gtk_box_pack_start (GTK_BOX (vbox),
		      hbox,
		      FALSE, FALSE, 0);

  gtk_widget_show (vbox);

  return vbox;
}

static gboolean
show_resolution (int width, int height)
{
  return (width >= 800 && height >= 600) ||
         (width == 640 && height == 480) ||
         (width == 720 && height == 576);
}

static void
resolution_changed_callback (GtkWidget *optionmenu, struct ScreenInfo *screen_info)
{
  generate_rate_menu(screen_info);
}

static void
generate_rate_menu (struct ScreenInfo *screen_info)
{
  GtkListStore *store;
  GtkTreeIter  iter;
  short *rates;
  int nrates, i;
  int size_nr;
  char *str;
  int closest_rate_nr;
  
  store = gtk_list_store_new (N_COLUMNS, G_TYPE_STRING, G_TYPE_INT);
  gtk_combo_box_set_model (GTK_COMBO_BOX (screen_info->rate_widget), GTK_TREE_MODEL (store));

  size_nr = get_current_resolution (screen_info);
  
  closest_rate_nr = -1;
  rates = XRRConfigRates (screen_info->config, size_nr, &nrates);
  for (i = 0; i < nrates; i++)
    {
      str = g_strdup_printf (_("%d Hz"), rates[i]);

      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter,
		          COL_NAME, str,
			  COL_ID, (int)rates[i],
			  -1);

      if ((closest_rate_nr < 0) ||
	  (ABS (rates[i] - screen_info->current_rate) <
	   ABS (rates[closest_rate_nr] - screen_info->current_rate)))
      {
	gtk_combo_box_set_active_iter (GTK_COMBO_BOX (screen_info->rate_widget), &iter);
	closest_rate_nr = i;
      }

      g_free (str);
    }

  g_object_unref (store);
}

static void
generate_resolution_menu(struct ScreenInfo* screen_info)
{
  GtkComboBox *combo;
  GtkListStore* store;
  GtkTreeIter iter;
  int i, item, current_item;
  XRRScreenSize *sizes;
  char *str;
  SizeID current_size;
  Rotation rot;

  combo = GTK_COMBO_BOX (screen_info->resolution_widget);
  store = gtk_list_store_new(N_COLUMNS, G_TYPE_STRING, G_TYPE_INT);

  current_size = XRRConfigCurrentConfiguration (screen_info->config, &rot);

  gtk_combo_box_set_model (combo, GTK_TREE_MODEL (store));

  current_item = 0;
  item = 0;
  sizes = screen_info->sizes;
  for (i = 0; i < screen_info->n_sizes; i++)
    {
      if (i == current_size || show_resolution (sizes[i].width, sizes[i].height))
	{
	  str = g_strdup_printf ("%dx%d", sizes[i].width, sizes[i].height);

	  if (i == current_size)
	    current_item = item;

	  gtk_list_store_append(store, &iter);
	  gtk_list_store_set(store, &iter,
			     COL_NAME, str,
			     COL_ID, i,
			     -1);

	  g_free (str);
	  item++;
	}
    }
  
  gtk_combo_box_set_active (combo, current_item);

	g_signal_connect (screen_info->resolution_widget, "changed", G_CALLBACK (resolution_changed_callback), screen_info);
  
	gtk_widget_show (screen_info->resolution_widget);
  g_object_unref (store);
}

static void
initialize_combo_layout (GtkCellLayout *layout) {
  GtkCellRenderer *cell = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start (layout, cell, TRUE);
  gtk_cell_layout_add_attribute (layout, cell, "text", COL_NAME);
}

static GtkWidget *
create_resolution_menu (struct ScreenInfo *screen_info)
{
  screen_info->resolution_widget = gtk_combo_box_new ();
  generate_resolution_menu (screen_info);

  initialize_combo_layout (GTK_CELL_LAYOUT (screen_info->resolution_widget));
  return screen_info->resolution_widget;
}

static GtkWidget *
create_rate_menu (struct ScreenInfo *screen_info)
{
  screen_info->rate_widget = gtk_combo_box_new ();

  generate_rate_menu (screen_info);

  initialize_combo_layout (GTK_CELL_LAYOUT (screen_info->rate_widget));
  gtk_widget_show (screen_info->rate_widget);
  return screen_info->rate_widget;
}

static GtkWidget *
create_rotate_menu (struct ScreenInfo *screen_info)
{
  GtkComboBox* combo = NULL;
  int i, item = 0, current_item = -1;

  screen_info->rotate_widget = gtk_combo_box_new_text ();
  combo = GTK_COMBO_BOX(screen_info->rotate_widget);

  for (i = 0; i < G_N_ELEMENTS (rotations); i++)
  {
    if ((screen_info->rotations & rotations[i].rotation) != 0)
    {
      gtk_combo_box_append_text (combo, _(rotations[i].name));
      if (screen_info->current_rotation == rotations[i].rotation) {
	current_item = item;
      }
      item++;
    }
  }

  /* it makes no sense to support only one selection element */
  gtk_widget_set_sensitive (screen_info->rotate_widget,
		  gtk_tree_model_iter_n_children (gtk_combo_box_get_model (combo), NULL) > 1);

  gtk_combo_box_set_active (combo, current_item);

  gtk_widget_show (screen_info->rotate_widget);
  return screen_info->rotate_widget;
}

static GtkWidget *
create_screen_widgets (struct ScreenInfo *screen_info, int nr, gboolean no_header)
{
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *option_menu;
  GtkWidget *ret;
  char *str;

  table = gtk_table_new (2, 2, FALSE);

  gtk_table_set_row_spacings ( GTK_TABLE (table), 6);
  gtk_table_set_col_spacings ( GTK_TABLE (table), 12);
  
  label = gtk_label_new_with_mnemonic (_("_Resolution:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_widget_show (label);
  gtk_table_attach (GTK_TABLE (table),
		    label,
		    0, 1,
		    0, 1,
		    GTK_FILL, 0,
		    0, 0);

  option_menu = create_resolution_menu (screen_info);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), option_menu);
  gtk_table_attach (GTK_TABLE (table),
		    option_menu,
		    1, 2,
		    0, 1,
		    GTK_FILL | GTK_EXPAND, 0,
		    0, 0);
  
  label = gtk_label_new_with_mnemonic (_("Re_fresh rate:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_widget_show (label);
  gtk_table_attach (GTK_TABLE (table),
		    label,
		    0, 1,
		    1, 2,
		    GTK_FILL, 0,
		    0, 0);
  gtk_widget_show (table);
  
  option_menu = create_rate_menu (screen_info);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), option_menu);
  gtk_table_attach (GTK_TABLE (table),
		    option_menu,
		    1, 2,
		    1, 2,
		    GTK_FILL | GTK_EXPAND, 0,
		    0, 0);
  
  label = gtk_label_new_with_mnemonic (_("R_otation:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_widget_show (label);
  gtk_table_attach (GTK_TABLE (table),
		    label,
		    0, 1,
		    2, 3,
		    GTK_FILL, 0,
		    0, 0);
  
  option_menu = create_rotate_menu (screen_info);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), option_menu);
  gtk_table_attach (GTK_TABLE (table),
		    option_menu,
		    1, 2,
		    2, 3,
		    GTK_FILL | GTK_EXPAND, 0,
		    0, 0);
  
  if (nr == 0)
    str = g_strdup (_("Default Settings"));
  else
    str = g_strdup_printf (_("Screen %d Settings\n"), nr+1);
  ret = wrap_in_label (table, str);
  g_free (str);
  return ret;
}


static GtkWidget *
create_dialog (struct DisplayInfo *info)
{
  GtkWidget *dialog;
  GtkWidget *screen_widget;
  GtkWidget *per_computer_check;
  int i;
  GtkWidget *wrapped;
  GtkWidget *vbox;
  GConfClient *client;
  char *key;
  char *resolution;
  char *str;
#ifdef HOST_NAME_MAX
  char hostname[HOST_NAME_MAX + 1];
#else
  char hostname[256];
#endif
  
  dialog = gtk_dialog_new_with_buttons (_("Screen Resolution Preferences"),
					NULL,
					GTK_DIALOG_NO_SEPARATOR,
 					"gtk-close",
 					GTK_RESPONSE_CLOSE,
					"gtk-apply",
					GTK_RESPONSE_APPLY,
					"gtk-help",
					GTK_RESPONSE_HELP,
					NULL);
					
  gtk_window_set_resizable(GTK_WINDOW (dialog), FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);  
  gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);
  capplet_set_icon (dialog, "display-capplet.png");
  
  vbox = gtk_vbox_new (FALSE, 18);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
                      vbox, FALSE, FALSE, 0);
  gtk_widget_show (vbox);
  
  for (i = 0; i < info->n_screens; i++)
    {
      screen_widget = create_screen_widgets (&info->screens[i], i, info->n_screens == 1);
      gtk_box_pack_start (GTK_BOX (vbox),
			  screen_widget, FALSE, FALSE, 0);
      gtk_widget_show (screen_widget);
    }

  per_computer_check = NULL;
  info->was_per_computer = FALSE;
  if (gethostname (hostname, sizeof (hostname)) == 0 &&
      strcmp (hostname, "localhost") != 0 &&
      strcmp (hostname, "localhost.localdomain") != 0)
    {
      
      str = g_strdup_printf (_("_Make default for this computer (%s) only"), hostname);
      per_computer_check = gtk_check_button_new_with_mnemonic (str);

      /* If we previously set the resolution specifically for this hostname, default
	 to it on */
      client = gconf_client_get_default ();
      key = g_strconcat ("/desktop/gnome/screen/", hostname,  "/0/resolution",NULL);
      resolution = gconf_client_get_string (client, key, NULL);
      g_free (key);
      g_object_unref (client);

      info->was_per_computer = resolution != NULL;
      g_free (resolution);
      
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (per_computer_check),
				    info->was_per_computer);
      
      gtk_widget_show (per_computer_check);
      
      wrapped = wrap_in_label (per_computer_check, _("Options"));
      gtk_box_pack_start (GTK_BOX (vbox),
			  wrapped, FALSE, FALSE, 0);
      gtk_widget_show (wrapped);
    }

  info->per_computer_check = per_computer_check;
  
  return dialog;
}

struct TimeoutData {
  int time;
  GtkLabel *label;
  GtkDialog *dialog;
  gboolean timed_out;
};

static char *
timeout_string (int time)
{
  return g_strdup_printf (ngettext ("Testing the new settings. If you don't respond in %d second the previous settings will be restored.", "Testing the new settings. If you don't respond in %d seconds the previous settings will be restored.", time), time);
}

static gboolean
save_timeout_callback (gpointer _data)
{
  struct TimeoutData *data = _data;
  char *str;
  
  data->time--;

  if (data->time == 0)
    {
      gtk_dialog_response (data->dialog, GTK_RESPONSE_NO);
      data->timed_out = TRUE;
      return FALSE;
    }

  str = timeout_string (data->time);
  gtk_label_set_text (data->label, str);
  g_free (str);
  
  return TRUE;
}

static int
run_revert_dialog (struct DisplayInfo *info,
		   GtkWidget *parent)
{
  GtkWidget *dialog;
  GtkWidget *hbox;
  GtkWidget *vbox;
  GtkWidget *label;
  GtkWidget *label_sec;
  GtkWidget *image;
  int res;
  struct TimeoutData timeout_data;
  guint timeout;
  char *str;

  dialog = gtk_dialog_new ();
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (parent));
  gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);
  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
  gtk_window_set_title (GTK_WINDOW (dialog), _("Keep Resolution"));
  gtk_window_set_position(GTK_WINDOW(dialog),GTK_WIN_POS_CENTER_ALWAYS);
  
  label = gtk_label_new (NULL);
  str = g_strdup_printf ("<b>%s</b>", _("Do you want to keep this resolution?"));
  gtk_label_set_markup (GTK_LABEL (label), str);
  g_free (str);
  image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_QUESTION, GTK_ICON_SIZE_DIALOG);
  gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
  
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_selectable (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

  str = timeout_string (REVERT_COUNT);
  label_sec = gtk_label_new (str);
  g_free (str);
  gtk_label_set_line_wrap (GTK_LABEL (label_sec), TRUE);
  gtk_label_set_selectable (GTK_LABEL (label_sec), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label_sec), 0.0, 0.5);

  hbox = gtk_hbox_new (FALSE, 6);
  vbox = gtk_vbox_new (FALSE, 6);

  gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), label_sec, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), hbox, FALSE, FALSE, 0);
  gtk_dialog_add_buttons (GTK_DIALOG (dialog),_("Use _previous resolution"), GTK_RESPONSE_NO, _("_Keep resolution"), GTK_RESPONSE_YES, NULL);
  
  gtk_widget_show_all (hbox);

  timeout_data.time = REVERT_COUNT;
  timeout_data.label = GTK_LABEL (label_sec);
  timeout_data.dialog = GTK_DIALOG (dialog);
  timeout_data.timed_out = FALSE;
  
  timeout = g_timeout_add (1000, save_timeout_callback, &timeout_data);
  res = gtk_dialog_run (GTK_DIALOG (dialog));

  if (!timeout_data.timed_out)
    g_source_remove (timeout);

  gtk_widget_destroy (dialog);
  
  return res == GTK_RESPONSE_YES;
}

static void
save_to_gconf (struct DisplayInfo *info, gboolean save_computer, gboolean clear_computer)
{
  GConfClient    *client;
  gboolean res;
#ifdef HOST_NAME_MAX
  char hostname[HOST_NAME_MAX + 1];
#else
  char hostname[256];
#endif
  char *path, *key, *str;
  int i;

  gethostname (hostname, sizeof(hostname));
  
  client = gconf_client_get_default ();

  if (clear_computer)
    {
      for (i = 0; i < info->n_screens; i++)
	{
	  key = g_strdup_printf ("/desktop/gnome/screen/%s/%d/resolution",
				 hostname, i);
	  gconf_client_unset (client, key, NULL);
	  g_free (key);
	  key = g_strdup_printf ("/desktop/gnome/screen/%s/%d/rate",
				 hostname, i);
	  gconf_client_unset (client, key, NULL);
	  g_free (key);
	}
    }
  
  if (save_computer)
    {
      path = g_strconcat ("/desktop/gnome/screen/",
			  hostname,
			  "/",
			  NULL);
    }
  else
    path = g_strdup ("/desktop/gnome/screen/default/");
	       
  for (i = 0; i < info->n_screens; i++)
    {
      struct ScreenInfo *screen_info = &info->screens[i];
      int new_res, new_rate;

      new_res = get_current_resolution (screen_info);
      new_rate = get_current_rate (screen_info);

      key = g_strdup_printf ("%s%d/resolution", path, i);
      str = g_strdup_printf ("%dx%d",
			     screen_info->sizes[new_res].width,
			     screen_info->sizes[new_res].height);
      
      res = gconf_client_set_string  (client, key, str, NULL);
      g_free (str);
      g_free (key);
      
      key = g_strdup_printf ("%s%d/rate", path, i);
      res = gconf_client_set_int  (client, key, new_rate, NULL);
      g_free (key);
    }

  g_free (path);
  g_object_unref (client);
}

static void
cb_dialog_response (GtkDialog *dialog, gint response_id, struct DisplayInfo *info)
{
  gboolean save_computer, clear_computer;
  switch (response_id)
    {
    case GTK_RESPONSE_DELETE_EVENT:
      gtk_main_quit ();
      break;
    case GTK_RESPONSE_HELP:
      capplet_help (GTK_WINDOW (dialog), "user-guide.xml", "goscustdesk-70");
      break;
    case GTK_RESPONSE_APPLY:
      save_computer = info->per_computer_check != NULL && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->per_computer_check));
      clear_computer = !save_computer && info->was_per_computer;
	  
      if (apply_config (info))
	{
	  gtk_widget_hide(GTK_WIDGET(dialog));
	  if (!run_revert_dialog (info, GTK_WIDGET (dialog)))
	    {
	      gtk_widget_show(GTK_WIDGET(dialog));
	      revert_config (info);
	      return;
	    }
	}
      
      save_to_gconf (info, save_computer, clear_computer);
      gtk_main_quit ();
      break;
	case GTK_RESPONSE_CLOSE:
		gtk_main_quit ();
		break;
    }
}

int
main (int argc, char *argv[])
{
  int major, minor;
  int event_base, error_base;
  GdkDisplay *display;
  GtkWidget *dialog;
  struct DisplayInfo *info;
  Display *xdisplay;
 
  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  gnome_program_init ("gnome-display-properties", VERSION,
		      LIBGNOMEUI_MODULE, argc, argv,
		      GNOME_PARAM_APP_DATADIR, GNOMECC_DATA_DIR,
		      NULL);

  display = gdk_display_get_default ();
  xdisplay = gdk_x11_display_get_xdisplay (display);
  
  if (!XRRQueryExtension (xdisplay, &event_base, &error_base) ||
      XRRQueryVersion (xdisplay, &major, &minor) == 0)
    {
      GtkWidget *msg_dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, 
						   _("The X Server does not support the XRandR extension.  Runtime resolution changes to the display size are not available."));
      gtk_dialog_run (GTK_DIALOG (msg_dialog));
      gtk_widget_destroy (msg_dialog);
      exit (0);
    }
  else if (major != 1 || minor < 1)
    {
      GtkWidget *msg_dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, 
						      _("The version of the XRandR extension is incompatible with this program. Runtime changes to the display size are not available."));
      gtk_dialog_run (GTK_DIALOG (msg_dialog));
      gtk_widget_destroy (msg_dialog);
      exit (0);
    }
  
  info = read_display_info (display);
  dialog = create_dialog (info);

  g_signal_connect (G_OBJECT (dialog), "response", G_CALLBACK (cb_dialog_response), info);
  gtk_widget_show (dialog);

  gtk_main ();
  return 0;
}
