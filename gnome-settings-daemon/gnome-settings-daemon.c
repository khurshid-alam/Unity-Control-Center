/*
 * Copyright © 2001 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Red Hat not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  Red Hat makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * RED HAT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL RED HAT
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN 
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:  Owen Taylor, Havoc Pennington
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include <gconf/gconf.h>
#include <libgnome/gnome-init.h>
#include <libgnomeui/gnome-ui-init.h>
#include <config.h>
#include "xsettings-manager.h"
#include "gnome-settings-daemon.h"

#include "gnome-settings-xsettings.h"
#include "gnome-settings-mouse.h"
#include "gnome-settings-keyboard.h"
#include "gnome-settings-background.h"
#include "gnome-settings-sound.h"
#include "gnome-settings-wm.h"
#include "gnome-settings-accessibility-keyboard.h"
#include "gnome-settings-screensaver.h"

#include "GNOME_SettingsDaemon.h"

static GObjectClass *parent_class = NULL;

struct _GnomeSettingsDaemonPrivate {
	int dummy;
};

static GSList *directories = NULL;
XSettingsManager *manager;

typedef struct DirElement
{
  char *dir;
  GSList *callbacks;
} DirElement;

void
gnome_settings_daemon_register_callback (const char      *dir,
					 KeyCallbackFunc  func)
{
  GSList *list;
  gboolean dir_found = FALSE;

  for (list = directories; list; list = list->next)
    {
      DirElement *dir_element = list->data;

      if (! strcmp (dir_element->dir, dir))
	{
	  dir_element->callbacks = g_slist_prepend (dir_element->callbacks, (gpointer)func);
	  dir_found = TRUE;
	  break;
	}
    }
  if (! dir_found)
    {
      DirElement *dir_element = g_new0 (DirElement, 1);

      dir_element->dir = g_strdup (dir);
      dir_element->callbacks = g_slist_prepend (dir_element->callbacks, (gpointer)func);
      directories = g_slist_prepend (directories, dir_element);
    }
}

GtkWidget *
gnome_settings_daemon_get_invisible (void)
{
	static GtkWidget *invisible = NULL;
	if (invisible == NULL)
		invisible = gtk_invisible_new ();
	return invisible;
}

static void
config_notify (GConfClient *client,
               guint        cnxn_id,
               GConfEntry  *entry,
               gpointer     user_data)
{
  GSList *list;
  
  for (list = directories; list; list = list->next)
    {
      DirElement *dir_element = list->data;

      if (! strncmp (dir_element->dir, entry->key, strlen (dir_element->dir)))
	{
	  GSList *func_list;
	  for (func_list = dir_element->callbacks; func_list; func_list = func_list->next)
	    {
	      ((KeyCallbackFunc) func_list->data) (entry);
	    }
	}
    }
}

static void
terminate_cb (void *data)
{
  gboolean *terminated = data;
  
  *terminated = TRUE;
  gtk_main_quit ();
}

static GdkFilterReturn 
manager_event_filter (GdkXEvent *xevent,
		      GdkEvent  *event,
		      gpointer   data)
{
  if (xsettings_manager_process_event (manager, (XEvent *)xevent))
    return GDK_FILTER_REMOVE;
  else
    return GDK_FILTER_CONTINUE;
}

CORBA_boolean
awake_impl (PortableServer_Servant servant,
	    const CORBA_char      *service,
	    CORBA_Environment     *ev)
{
  printf ("I received an activate request for %s\n", service);
  return TRUE;
}


static void
finalize (GObject *object)
{
	GnomeSettingsDaemon *daemon;

	daemon = GNOME_SETTINGS_DAEMON (object);
	if (daemon->private == NULL) {
	  return;
	}

	xsettings_manager_destroy (manager);

	g_free (daemon->private);
	daemon->private = NULL;

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gnome_settings_daemon_class_init (GnomeSettingsDaemonClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = finalize;

  klass->epv.awake = awake_impl;

  parent_class = g_type_class_peek_parent (klass);
}

static void
gnome_settings_daemon_init (GnomeSettingsDaemon *settings)
{
  settings->private = g_new (GnomeSettingsDaemonPrivate, 1);
}

BONOBO_TYPE_FUNC_FULL(GnomeSettingsDaemon, GNOME_SettingsDaemon,
		      BONOBO_TYPE_OBJECT, gnome_settings_daemon)

GObject *
gnome_settings_daemon_new (void)
{
  gboolean terminated = FALSE;
  GConfClient *client;
  GSList *list;
  GnomeSettingsDaemon *daemon;

  daemon = g_object_new (gnome_settings_daemon_get_type (), NULL);

  if (xsettings_manager_check_running (gdk_display, DefaultScreen (gdk_display)))
    {
      fprintf (stderr, "You can only run one xsettings manager at a time; exiting\n");
      exit (1);
    }
      
  if (!terminated)
    {
      manager = xsettings_manager_new (gdk_display, DefaultScreen (gdk_display),
				       terminate_cb, &terminated);
      if (!manager)
	{
	  fprintf (stderr, "Could not create xsettings manager!\n");
	  exit (1);
	}
    }

  /* We use GConfClient not GConfClient because a cache isn't useful
   * for us
   */
  client = gconf_client_get_default ();
  gnome_settings_xsettings_init (client);
  gnome_settings_mouse_init (client);
  gnome_settings_keyboard_init (client);
  gnome_settings_background_init (client);
  gnome_settings_sound_init (client);
  gnome_settings_wm_init (client);
  gnome_settings_accessibility_keyboard_init (client);
  gnome_settings_screensaver_init (client);

  for (list = directories; list; list = list->next)
    {
      GError *error = NULL;
      DirElement *dir_element = list->data;

      gconf_client_add_dir (client,
			    dir_element->dir,
			    GCONF_CLIENT_PRELOAD_ONELEVEL,
			    NULL);
      
      gconf_client_notify_add (client,
                               dir_element->dir,
                               config_notify,
                               NULL,
			       NULL,
                               &error);

      if (error)
        {
          fprintf (stderr, "Could not listen for changes to configuration in '%s': %s\n",
                   dir_element->dir, error->message);
          g_error_free (error);
        }
    }
  
  gdk_window_add_filter (NULL, manager_event_filter, NULL);
  gnome_settings_xsettings_load (client);
  gnome_settings_mouse_load (client);
  gnome_settings_sound_load (client);
  gnome_settings_background_load (client);
  gnome_settings_wm_load (client);
  gnome_settings_accessibility_keyboard_load (client);
  gnome_settings_screensaver_load (client);
  
  return G_OBJECT (daemon);
}
