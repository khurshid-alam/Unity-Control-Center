#include <config.h>

#include "gnome-settings-daemon.h"
#include "gnome-settings-wm.h"
#include <gdk/gdkx.h>
#include <string.h>

#include <libwindow-settings/gnome-wm-manager.h>

static void
set_number_of_workspaces (int workspaces)
{
	XEvent xev;

	xev.xclient.type = ClientMessage;
	xev.xclient.serial = 0;
	xev.xclient.window = GDK_ROOT_WINDOW ();
	xev.xclient.send_event = True;
	xev.xclient.display = gdk_display;
	xev.xclient.message_type = gdk_x11_get_xatom_by_name ("_NET_NUMBER_OF_DESKTOPS");
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = workspaces;

	XSendEvent (gdk_display,
		    gdk_x11_get_default_root_xwindow (),
		    False,
		    SubstructureRedirectMask | SubstructureNotifyMask,
		    &xev);
}

static void
set_workspace_names (GSList *values)
{
	GConfValue *value;
	GSList *list;

	/* First delete the properties */
	XDeleteProperty (gdk_display,
			 gdk_x11_get_default_root_xwindow (),
			 gdk_x11_get_xatom_by_name ("_NET_DESKTOP_NAMES"));

	for (list = values; list; list = list->next) {
		unsigned char *str;
		value = list->data;

		str = (unsigned char *)gconf_value_get_string (value);
		
		if (!g_utf8_validate (str, -1, NULL)) {
			continue;
		}

		XChangeProperty (gdk_display,
				 gdk_x11_get_default_root_xwindow (),
				 gdk_x11_get_xatom_by_name ("_NET_DESKTOP_NAMES"),
				 gdk_x11_get_xatom_by_name ("UTF8_STRING"),
				 8,
				 PropModeAppend,
				 str,
				 strlen (str) + 1);
	}
}

static void
wm_callback (GConfEntry *entry)
{
        GnomeWindowManager *wm;

	if (!strcmp (entry->key, "/desktop/gnome/applications/window_manager/number_of_workspaces")) {
		if (entry->value->type == GCONF_VALUE_INT)
			set_number_of_workspaces (gconf_value_get_int (entry->value));
	}
	else if (!strcmp (entry->key, "/desktop/gnome/applications/window_manager/workspace_names")) {
		if (entry->value->type == GCONF_VALUE_LIST &&
		    gconf_value_get_list_type (entry->value) == GCONF_VALUE_STRING)
			set_workspace_names (gconf_value_get_list (entry->value));
	}
	else if (!strcmp (entry->key, "/desktop/gnome/applications/window_manager/theme")) {
	  wm = gnome_wm_manager_get_current ();
	  if (wm != NULL) {
	    gnome_window_manager_set_theme (wm, gconf_value_get_string (entry->value));
	  }
	}
	else if (!strcmp (entry->key, "/desktop/gnome/applications/window_manager/titlebar_font")) {
	  wm = gnome_wm_manager_get_current ();
	  gnome_window_manager_set_font (wm, gconf_value_get_string (entry->value));
	}
	else if (!strcmp (entry->key, "/desktop/gnome/applications/window_manager/focus_follows_mouse")) {
	  wm = gnome_wm_manager_get_current ();
	  gnome_window_manager_set_focus_follows_mouse (wm, gconf_value_get_bool (entry->value));
	}
}

void
gnome_settings_wm_init (GConfClient *client)
{
        gnome_wm_manager_init (NULL);
	gnome_settings_daemon_register_callback ("/desktop/gnome/applications/window_manager", wm_callback);
}

void
gnome_settings_wm_load (GConfClient *client)
{
	
}

