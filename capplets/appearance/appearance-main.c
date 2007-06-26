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
#include "appearance-desktop.h"
#include "appearance-font.h"
#include "appearance-themes.h"
#include "appearance-style.h"
#include "appearance-ui.h"
#include "theme-installer.h"
#include "theme-thumbnail.h"
#include "activate-settings-daemon.h"
#include "capplet-util.h"

/* required for gnome_program_init(): */
#include <libgnome/libgnome.h>
#include <libgnomeui/gnome-ui-init.h>
/* ---------------------------------- */

static AppearanceData *
init_appearance_data (int *argc, char ***argv)
{
  AppearanceData *data = NULL;
  gchar *gladefile;
  GladeXML *ui;

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  g_thread_init (NULL);
  theme_thumbnail_factory_init (*argc, *argv);
  gtk_init (argc, argv);
  gnome_vfs_init ();
  activate_settings_daemon ();

  /* set up the data */
  gladefile = g_build_filename (GNOMECC_GLADE_DIR, "appearance.glade", NULL);
  ui = glade_xml_new (gladefile, NULL, NULL);
  g_free (gladefile);

  if (ui) {
    data = g_new (AppearanceData, 1);
    data->client = gconf_client_get_default ();
    data->xml = ui;
  }

  return data;
}

static void
main_window_response (GtkWidget *widget,
                      gint response_id,
                      AppearanceData *data)
{
  if (response_id == GTK_RESPONSE_CLOSE ||
      response_id == GTK_RESPONSE_DELETE_EVENT)
  {
    gtk_main_quit ();

    themes_shutdown (data);
    style_shutdown (data);
    desktop_shutdown (data);
    font_shutdown (data);

    g_object_unref (data->client);
    g_object_unref (data->xml);
  }
}

int
main (int argc, char **argv)
{
  AppearanceData *data;
  GnomeProgram *program;
  GtkWidget *w;

  gchar *install_filename = NULL;
  gchar *start_page = NULL;
  GOptionContext *option_context;
  GOptionEntry option_entries[] = {
      { "install-theme",
        'i',
        G_OPTION_FLAG_IN_MAIN,
        G_OPTION_ARG_FILENAME,
        &install_filename,
        N_("Specify the filename of a theme to install"),
        N_("filename") },
      { "show-page",
        'p',
        G_OPTION_FLAG_IN_MAIN,
        G_OPTION_ARG_STRING,
        &start_page,
        N_("Specify the name of the page to show (theme|background|fonts|interface)"),
        N_("page") },
      { NULL }
    };

  /* init */
  data = init_appearance_data (&argc, &argv);
  if (!data)
    return 1;

  option_context = g_option_context_new (NULL);
  g_option_context_add_main_entries (option_context, option_entries, GETTEXT_PACKAGE);

  /* this appears to be required for gnome_wm_manager_init (), which is called
   * inside gnome_meta_theme_set ();
   * TODO: try to remove if possible
   */
  program = gnome_program_init ("appearance", VERSION,
        LIBGNOMEUI_MODULE, argc, argv,
        GNOME_PARAM_APP_DATADIR, GNOMECC_DATA_DIR,
        GNOME_PARAM_GOPTION_CONTEXT, option_context,
        NULL);

  if (install_filename != NULL)
     gnome_theme_install_from_uri (install_filename, NULL);
  g_free (install_filename);

  /* init tabs */
  themes_init (data);
  style_init (data);
  desktop_init (data);
  font_init (data);
  ui_init (data);

  /* prepare the main window */
  w = glade_xml_get_widget (data->xml, "appearance_window");
  capplet_set_icon (w, "gnome-settings-theme");
  gtk_widget_show_all (w);

  g_signal_connect_after (G_OBJECT (w), "response",
                          G_CALLBACK (main_window_response), data);

  if (start_page != NULL) {
    gchar *page_name;

    page_name = g_strconcat (start_page, "_vbox", NULL);
    g_free (start_page);

    w = glade_xml_get_widget (data->xml, page_name);
    if (w != NULL) {
      GtkNotebook *nb;
      gint pindex;

      nb = GTK_NOTEBOOK (glade_xml_get_widget (data->xml, "main_notebook"));
      pindex = gtk_notebook_page_num (nb, w);
      if (pindex != -1)
        gtk_notebook_set_current_page (nb, pindex);
    }
    g_free (page_name);
  }

  /* start the mainloop */
  gtk_main ();

  /* free stuff */
  g_free (data);
  g_object_unref (program);

  return 0;
}
