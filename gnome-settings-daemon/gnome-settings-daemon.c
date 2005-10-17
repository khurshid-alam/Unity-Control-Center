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
#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>


#include <gconf/gconf.h>
#include <libgnome/gnome-init.h>
#include <libgnomeui/gnome-ui-init.h>

#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "xsettings-manager.h"
#include "gnome-settings-daemon.h"
#include "gnome-settings-xmodmap.h"

/*#include "gnome-settings-disk.h"*/
#include "gnome-settings-font.h"
#include "gnome-settings-xsettings.h"
#include "gnome-settings-mouse.h"
#include "gnome-settings-keyboard-xkb.h"
#include "gnome-settings-keyboard.h"
#include "gnome-settings-background.h"
#include "gnome-settings-sound.h"
#include "gnome-settings-accessibility-keyboard.h"
#include "gnome-settings-screensaver.h"
#include "gnome-settings-default-editor.h"
#include "gnome-settings-keybindings.h"
#include "gnome-settings-multimedia-keys.h"
#include "gnome-settings-gtk1theme.h"
#include "gnome-settings-xrdb.h"
#include "gnome-settings-typing-break.h"

#include "GNOME_SettingsDaemon.h"

#include "clipboard-manager.h"

static GObjectClass *parent_class = NULL;

struct _GnomeSettingsDaemonPrivate {
	int dummy;
};

static GSList *directories = NULL;
XSettingsManager **managers = NULL;
static ClipboardManager *clipboard_manager;

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
clipboard_manager_terminate_cb (void *data)
{
  /* Do nothing */
}

static GdkFilterReturn 
clipboard_manager_event_filter (GdkXEvent *xevent,
				GdkEvent  *event,
				gpointer   data)
{
  if (clipboard_manager_process_event (clipboard_manager,
				       (XEvent *)xevent))
    return GDK_FILTER_REMOVE;
  else
    return GDK_FILTER_CONTINUE;
}

static void
clipboard_manager_watch_cb (Window  window,
			    Bool    is_start,
			    long    mask,
			    void   *cb_data)
{
  GdkWindow *gdkwin;
  GdkDisplay *display;

  display = gdk_display_get_default ();
  gdkwin = gdk_window_lookup_for_display (display, window);

  if (is_start)
    {
      if (!gdkwin)
	gdkwin = gdk_window_foreign_new_for_display (display, window);
      else
	g_object_ref (gdkwin);
      
      gdk_window_add_filter (gdkwin, clipboard_manager_event_filter, NULL);
    }
  else
    {
      if (!gdkwin)
	return;
      gdk_window_remove_filter (gdkwin, clipboard_manager_event_filter, NULL);
      g_object_unref (gdkwin);
    }
}

static void
terminate_cb (void *data)
{
  gboolean *terminated = data;

  if (*terminated)
    return;
  
  *terminated = TRUE;
  gtk_main_quit ();
}

static GdkFilterReturn 
manager_event_filter (GdkXEvent *xevent,
		      GdkEvent  *event,
		      gpointer   data)
{
  int screen_num = GPOINTER_TO_INT (data);

  g_return_val_if_fail (managers != NULL, GDK_FILTER_CONTINUE);

  if (xsettings_manager_process_event (managers [screen_num], (XEvent *)xevent))
    return GDK_FILTER_REMOVE;
  else
    return GDK_FILTER_CONTINUE;
}

CORBA_boolean
awake_impl (PortableServer_Servant servant,
	    const CORBA_char      *service,
	    CORBA_Environment     *ev)
{
#if 0
  printf ("I received an activate request for %s\n", service);
#endif
  return TRUE;
}


static void
finalize (GObject *object)
{
	GnomeSettingsDaemon *daemon;
	int                  i;

	daemon = GNOME_SETTINGS_DAEMON (object);
	if (daemon->private == NULL) {
	  return;
	}

	for (i = 0; managers && managers [i]; i++)
		xsettings_manager_destroy (managers [i]);

	clipboard_manager_destroy (clipboard_manager);

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
  GdkDisplay *display;
  int i;
  int n_screens;

  display = gdk_display_get_default ();
  n_screens = gdk_display_get_n_screens (display);

  daemon = g_object_new (gnome_settings_daemon_get_type (), NULL);

  if (xsettings_manager_check_running (
		gdk_x11_display_get_xdisplay (display),
		gdk_screen_get_number (gdk_screen_get_default ())))
    {
      fprintf (stderr, "You can only run one xsettings manager at a time; exiting\n");
      exit (1);
    }

  
  if (!terminated)
    {
      managers = g_new (XSettingsManager *, n_screens + 1);

      for (i = 0; i < n_screens; i++)
        {
          GdkScreen *screen;

          screen = gdk_display_get_screen (display, i);

          managers [i] = xsettings_manager_new (
				gdk_x11_display_get_xdisplay (display),
				gdk_screen_get_number (screen),
				terminate_cb, &terminated);
          if (!managers [i])
	    {
	      fprintf (stderr, "Could not create xsettings manager for screen %d!\n", i);
	      exit (1);
	    }
        }

      g_assert (i == n_screens);
      managers [i] = NULL;
    }

  if (!clipboard_manager_check_running (GDK_DISPLAY_XDISPLAY (display)))
    {
      clipboard_manager = clipboard_manager_new (GDK_DISPLAY_XDISPLAY (display),
						 gdk_error_trap_push,
						 gdk_error_trap_pop,
						 clipboard_manager_terminate_cb,
						 clipboard_manager_watch_cb,
						 NULL);
    }

  /* We use GConfClient not GConfClient because a cache isn't useful
   * for us
   */
  client = gnome_settings_daemon_get_conf_client ();

/*  gnome_settings_disk_init (client);*/
  gnome_settings_font_init (client);
  gnome_settings_xsettings_init (client);
  gnome_settings_mouse_init (client);
/* Essential - xkb initialization should happen before */
  gnome_settings_keyboard_xkb_set_post_activation_callback ((PostActivationCallback)gnome_settings_load_modmap_files, NULL);
  gnome_settings_keyboard_xkb_init (client);
  gnome_settings_keyboard_init (client);
  gnome_settings_multimedia_keys_init (client);
/* */
  gnome_settings_sound_init (client);
  gnome_settings_accessibility_keyboard_init (client);
  gnome_settings_screensaver_init (client);
  gnome_settings_default_editor_init (client);
  gnome_settings_background_init (client);
  gnome_settings_keybindings_init (client);
  gnome_settings_gtk1_theme_init (client);
  gnome_settings_xrdb_init (client);
  gnome_settings_typing_break_init (client);

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

  for (i = 0; i < n_screens; i++) 
    {
      GdkScreen *screen;

      screen = gdk_display_get_screen (display, i);
      gdk_window_add_filter (
		gdk_screen_get_root_window (screen),
		manager_event_filter, GINT_TO_POINTER (i));
    }

/*  gnome_settings_disk_load (client);*/
  gnome_settings_font_load (client);
  gnome_settings_xsettings_load (client);
  gnome_settings_mouse_load (client);
/* Essential - xkb initialization should happen before */
  gnome_settings_keyboard_xkb_load (client);
  gnome_settings_keyboard_load (client);
  gnome_settings_multimedia_keys_load (client);
/* */
  gnome_settings_sound_load (client);
  gnome_settings_accessibility_keyboard_load (client);
  gnome_settings_screensaver_load (client);
  gnome_settings_default_editor_load (client);
  gnome_settings_background_load (client);
  gnome_settings_keybindings_load (client);
  gnome_settings_gtk1_theme_load (client);
  gnome_settings_xrdb_load (client);
  gnome_settings_typing_break_load (client);

  return G_OBJECT (daemon);
}


/* Helper functions */

/*
 * Helper function for spawn_with_input() - wait for a child
 * to exit.
 */
gboolean
wait_for_child (int  pid,
		int *status)
{
  gint ret;

 again:
  ret = waitpid (pid, status, 0);

  if (ret < 0)
    {
      if (errno == EINTR)
        goto again;
      else
        {
	  g_warning ("Unexpected error in waitpid() (%s)",
		     g_strerror (errno));
	  return FALSE;
        }
    }

  return TRUE;
}

static void
child_watch_cb (GPid pid, gint status, gpointer user_data)
{
  gchar *command = user_data;

  if (!WIFEXITED (status) || WEXITSTATUS (status))
    {
      g_warning ("Command %s failed", command);
    }

  g_free (command);

}

/*
 * Helper function for spawn_with_input() - write an entire
 * string to a fd.
 */
static gboolean
write_all (int         fd,
	   const char *buf,
	   gsize       to_write)
{
  while (to_write > 0)
    {
      gssize count = write (fd, buf, to_write);
      if (count < 0)
	{
	  if (errno != EINTR)
	    return FALSE;
	}
      else
	{
	  to_write -= count;
	  buf += count;
	}
    }

  return TRUE;
}

/**
 * gnome_settings_daemon-spawn_with_input:
 * @argv: command line to run
 * @input: string to write to the child process. 
 * 
 * Spawns a child process specified by @argv, writes the text in
 * @input to it, then waits for the child to exit. Any failures
 * are output through g_warning(); if you wanted to use this in
 * cases where errors need to be presented to the user, some
 * modification would be needed.
 **/
void
gnome_settings_daemon_spawn_with_input (char       **argv,
					const char  *input)
{
  int exit_status;
  int child_pid;
  int inpipe;
  GError *err = NULL;
  gchar *command;
  
  if (!g_spawn_async_with_pipes (NULL /* working directory */, argv, NULL /* envp */,
				 G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
				 NULL, NULL, /* child setup and data */
				 &child_pid,
				 &inpipe, NULL, NULL, /* stdin, stdout, stderr */
				 &err))
    {
      gchar *command = g_strjoinv (" ", argv);
      g_warning ("Could not execute %s: %s", command, err->message);
      g_error_free (err);
      g_free (command);

      return;
    }

  command = g_strjoinv (" ", argv);
  if (input)
    {
      if (!write_all (inpipe, input, strlen (input)))
	{
	  g_warning ("Could not write input to %s", command);
	}

      close (inpipe);
    }
  
  g_child_watch_add (child_pid, (GChildWatchFunc) child_watch_cb, command);
}

extern GConfClient *conf_client;

GConfClient *
gnome_settings_daemon_get_conf_client (void)
{
	if (!conf_client)
		conf_client = gconf_client_get_default ();

	return conf_client;
}
