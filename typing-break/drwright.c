/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2003-2005 Imendio HB
 * Copyright (C) 2002-2003 Richard Hult <richard@imendio.com>
 * Copyright (C) 2002 CodeFactory AB
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>

#include "drwright.h"
#include "drw-break-window.h"
#include "drw-monitor.h"
#include "drw-utils.h"

#define BLINK_TIMEOUT        200
#define BLINK_TIMEOUT_MIN    120
#define BLINK_TIMEOUT_FACTOR 100

#define POPUP_ITEM_ENABLED 1
#define POPUP_ITEM_BREAK   2

typedef enum {
	STATE_START,
	STATE_IDLE,
	STATE_TYPE,
	STATE_WARN_TYPE,
	STATE_WARN_IDLE,
	STATE_BREAK_SETUP,
	STATE_BREAK,
	STATE_BREAK_DONE_SETUP,
	STATE_BREAK_DONE
} DrwState;

struct _DrWright {
	/* Widgets. */
	GtkWidget      *break_window;
	GList          *secondary_break_windows;

	DrwMonitor     *monitor;

	GtkUIManager *ui_manager;

	DrwState        state;
	GTimer         *timer;
	GTimer         *idle_timer;

	gint            last_elapsed_time;
	gint            save_last_time;

	gboolean        is_active;

	/* Time settings. */
	gint            type_time;
	gint            break_time;
	gint            warn_time;

	gboolean        enabled;

	guint           clock_timeout_id;
	guint           blink_timeout_id;

	gboolean        blink_on;

	GtkStatusIcon  *icon;

	GdkPixbuf      *neutral_bar;
	GdkPixbuf      *red_bar;
	GdkPixbuf      *green_bar;
	GdkPixbuf      *disabled_bar;
	GdkPixbuf      *composite_bar;

	GtkWidget      *warn_dialog;
};

static void     activity_detected_cb           (DrwMonitor     *monitor,
						DrWright       *drwright);
static gboolean maybe_change_state             (DrWright       *drwright);
static gboolean update_tooltip                 (DrWright       *drwright);
static void     break_window_done_cb           (GtkWidget      *window,
						DrWright       *dr);
static void     break_window_postpone_cb       (GtkWidget      *window,
						DrWright       *dr);
static void     break_window_destroy_cb        (GtkWidget      *window,
						DrWright       *dr);
static void     popup_break_cb                 (GtkAction      *action,
						DrWright       *dr);
static void     popup_preferences_cb           (GtkAction      *action,
						DrWright       *dr);
static void     popup_about_cb                 (GtkAction      *action,
						DrWright       *dr);
static void     init_tray_icon                 (DrWright       *dr);
static GList *  create_secondary_break_windows (void);

static const GtkActionEntry actions[] = {
  {"Preferences", GTK_STOCK_PREFERENCES, NULL, NULL, NULL, G_CALLBACK (popup_preferences_cb)},
  {"About", GTK_STOCK_ABOUT, NULL, NULL, NULL, G_CALLBACK (popup_about_cb)},
  {"TakeABreak", NULL, N_("_Take a Break"), NULL, NULL, G_CALLBACK (popup_break_cb)}
};

extern gboolean debug;

static void
setup_debug_values (DrWright *dr)
{
	dr->type_time = 5;
	dr->warn_time = 4;
	dr->break_time = 10;
}

static void
update_icon (DrWright *dr)
{
	GdkPixbuf *pixbuf;
	GdkPixbuf *tmp_pixbuf;
	gint       width, height;
	gfloat     r;
	gint       offset;
	gboolean   set_pixbuf;

	if (!dr->enabled) {
		gtk_status_icon_set_from_pixbuf (dr->icon,
						 dr->disabled_bar);
		return;
	}

	tmp_pixbuf = gdk_pixbuf_copy (dr->neutral_bar);

	width = gdk_pixbuf_get_width (tmp_pixbuf);
	height = gdk_pixbuf_get_height (tmp_pixbuf);

	set_pixbuf = TRUE;

	switch (dr->state) {
	case STATE_BREAK:
	case STATE_BREAK_SETUP:
		r = 1;
		break;

	case STATE_BREAK_DONE:
	case STATE_BREAK_DONE_SETUP:
	case STATE_START:
		r = 0;
		break;

	default:
		r = (float) (g_timer_elapsed (dr->timer, NULL) + dr->save_last_time) /
		    (float) dr->type_time;
		break;
	}

	offset = CLAMP ((height - 0) * (1.0 - r), 1, height - 0);

	switch (dr->state) {
	case STATE_WARN_TYPE:
	case STATE_WARN_IDLE:
		pixbuf = dr->red_bar;
		set_pixbuf = FALSE;
		break;

	case STATE_BREAK_SETUP:
	case STATE_BREAK:
		pixbuf = dr->red_bar;
		break;

	default:
		pixbuf = dr->green_bar;
	}

	gdk_pixbuf_composite (pixbuf,
			      tmp_pixbuf,
			      0,
			      offset,
			      width,
			      height - offset,
			      0,
			      0,
			      1.0,
			      1.0,
			      GDK_INTERP_BILINEAR,
			      255);

	if (set_pixbuf) {
		gtk_status_icon_set_from_pixbuf (dr->icon,
						 tmp_pixbuf);
	}

	if (dr->composite_bar) {
		g_object_unref (dr->composite_bar);
	}

	dr->composite_bar = tmp_pixbuf;
}

static gboolean
blink_timeout_cb (DrWright *dr)
{
	gfloat r;
	gint   timeout;

	r = (dr->type_time - g_timer_elapsed (dr->timer, NULL) - dr->save_last_time) / dr->warn_time;
	timeout = BLINK_TIMEOUT + BLINK_TIMEOUT_FACTOR * r;

	if (timeout < BLINK_TIMEOUT_MIN) {
		timeout = BLINK_TIMEOUT_MIN;
	}

	if (dr->blink_on || timeout == 0) {
		gtk_status_icon_set_from_pixbuf (dr->icon,
						 dr->composite_bar);
	} else {
		gtk_status_icon_set_from_pixbuf (dr->icon,
						 dr->neutral_bar);
	}

	dr->blink_on = !dr->blink_on;

	if (timeout) {
		dr->blink_timeout_id = g_timeout_add (timeout,
						      (GSourceFunc) blink_timeout_cb,
						      dr);
	} else {
		dr->blink_timeout_id = 0;
	}

	return FALSE;
}

static void
start_blinking (DrWright *dr)
{
	if (!dr->blink_timeout_id) {
		dr->blink_on = TRUE;
		blink_timeout_cb (dr);
	}

	/*gtk_widget_show (GTK_WIDGET (dr->icon));*/
}

static void
stop_blinking (DrWright *dr)
{
	if (dr->blink_timeout_id) {
		g_source_remove (dr->blink_timeout_id);
		dr->blink_timeout_id = 0;
	}

	/*gtk_widget_hide (GTK_WIDGET (dr->icon));*/
}

static gboolean
grab_keyboard_on_window (GdkWindow *window,
			 guint32    activate_time)
{
	GdkGrabStatus status;

	status = gdk_keyboard_grab (window, TRUE, activate_time);
	if (status == GDK_GRAB_SUCCESS) {
		return TRUE;
	}

	return FALSE;
}

static gboolean
break_window_map_event_cb (GtkWidget *widget,
			   GdkEvent  *event,
			   DrWright  *dr)
{
	grab_keyboard_on_window (dr->break_window->window, gtk_get_current_event_time ());

        return FALSE;
}

static gboolean
maybe_change_state (DrWright *dr)
{
	gint elapsed_time;
	gint elapsed_idle_time;

	if (debug) {
		g_timer_reset (dr->idle_timer);
	}

	elapsed_time = g_timer_elapsed (dr->timer, NULL) + dr->save_last_time;
	elapsed_idle_time = g_timer_elapsed (dr->idle_timer, NULL);

	if (elapsed_time > dr->last_elapsed_time + dr->warn_time) {
		/* If the timeout is delayed by the amount of warning time, then
		 * we must have been suspended or stopped, so we just start
		 * over.
		 */
		dr->state = STATE_START;
	}

	switch (dr->state) {
	case STATE_START:
		if (dr->break_window) {
			gtk_widget_destroy (dr->break_window);
			dr->break_window = NULL;
		}

		gtk_status_icon_set_from_pixbuf (dr->icon,
						 dr->neutral_bar);

		dr->save_last_time = 0;

		g_timer_start (dr->timer);
		g_timer_start (dr->idle_timer);

		if (dr->enabled) {
			dr->state = STATE_IDLE;
		}

		update_tooltip (dr);
		stop_blinking (dr);
		break;

	case STATE_IDLE:
		if (elapsed_idle_time >= dr->break_time) {
			dr->state = STATE_BREAK_DONE_SETUP;
		} else if (dr->is_active) {
			dr->state = STATE_TYPE;
		}
		break;

	case STATE_TYPE:
		if (elapsed_time >= dr->type_time - dr->warn_time) {
			dr->state = STATE_WARN_TYPE;

			start_blinking (dr);
 		} else if (elapsed_time >= dr->type_time) {
			dr->state = STATE_BREAK_SETUP;
		}
		else if (!dr->is_active) {
			dr->state = STATE_IDLE;
			g_timer_start (dr->idle_timer);
		}
		break;

	case STATE_WARN_TYPE:
		if (elapsed_time >= dr->type_time) {
			dr->state = STATE_BREAK_SETUP;
		}
		else if (!dr->is_active) {
			dr->state = STATE_WARN_IDLE;
		}
		break;

	case STATE_WARN_IDLE:
		if (elapsed_idle_time >= dr->break_time) {
			dr->state = STATE_BREAK_DONE_SETUP;
		}
		else if (dr->is_active) {
			dr->state = STATE_WARN_TYPE;
		}

		break;

	case STATE_BREAK_SETUP:
		/* Don't allow more than one break window to coexist, can happen
		 * if a break is manually enforced.
		 */
		if (dr->break_window) {
			dr->state = STATE_BREAK;
			break;
		}

		stop_blinking (dr);
		gtk_status_icon_set_from_pixbuf (dr->icon,
						 dr->red_bar);

		g_timer_start (dr->timer);

		dr->break_window = drw_break_window_new ();

		g_signal_connect (dr->break_window, "map_event",
				  G_CALLBACK (break_window_map_event_cb),
				  dr);

		g_signal_connect (dr->break_window,
				  "done",
				  G_CALLBACK (break_window_done_cb),
				  dr);

		g_signal_connect (dr->break_window,
				  "postpone",
				  G_CALLBACK (break_window_postpone_cb),
				  dr);

		g_signal_connect (dr->break_window,
				  "destroy",
				  G_CALLBACK (break_window_destroy_cb),
				  dr);

		dr->secondary_break_windows = create_secondary_break_windows ();

		gtk_widget_show (dr->break_window);

		dr->save_last_time = elapsed_time;
		dr->state = STATE_BREAK;
		break;

	case STATE_BREAK:
		if (elapsed_time - dr->save_last_time >= dr->break_time) {
			dr->state = STATE_BREAK_DONE_SETUP;
		}
		break;

	case STATE_BREAK_DONE_SETUP:
		stop_blinking (dr);
		gtk_status_icon_set_from_pixbuf (dr->icon,
						 dr->green_bar);

		dr->state = STATE_BREAK_DONE;
		break;

	case STATE_BREAK_DONE:
		if (dr->is_active) {
			dr->state = STATE_START;
			if (dr->break_window) {
				gtk_widget_destroy (dr->break_window);
				dr->break_window = NULL;
			}
		}
		break;
	}

	dr->is_active = FALSE;
	dr->last_elapsed_time = elapsed_time;

	update_icon (dr);

	return TRUE;
}

static gboolean
update_tooltip (DrWright *dr)
{
	gint   elapsed_time, min;
	gchar *str;

	if (!dr->enabled) {
		gtk_status_icon_set_tooltip_text (dr->icon,
					     _("Disabled"));
		return TRUE;
	}

	elapsed_time = g_timer_elapsed (dr->timer, NULL);

	min = floor (0.5 + (dr->type_time - elapsed_time - dr->save_last_time) / 60.0);

	if (min >= 1) {
		str = g_strdup_printf (ngettext("%d minute until the next break",
						"%d minutes until the next break",
						min), min);
	} else {
		str = g_strdup_printf (_("Less than one minute until the next break"));
	}

	gtk_status_icon_set_tooltip_text (dr->icon,
				     str);

	g_free (str);

	return TRUE;
}

static void
activity_detected_cb (DrwMonitor *monitor,
		      DrWright   *dr)
{
	dr->is_active = TRUE;
	g_timer_start (dr->idle_timer);
}

static void
gconf_notify_cb (GConfClient *client,
		 guint        cnxn_id,
		 GConfEntry  *entry,
		 gpointer     user_data)
{
	DrWright  *dr = user_data;
	GtkWidget *item;

	if (!strcmp (entry->key, GCONF_PATH "/type_time")) {
		if (entry->value->type == GCONF_VALUE_INT) {
			dr->type_time = 60 * gconf_value_get_int (entry->value);
			dr->warn_time = MIN (dr->type_time / 10, 5*60);

			dr->state = STATE_START;
		}
	}
	else if (!strcmp (entry->key, GCONF_PATH "/break_time")) {
		if (entry->value->type == GCONF_VALUE_INT) {
			dr->break_time = 60 * gconf_value_get_int (entry->value);
			dr->state = STATE_START;
		}
	}
	else if (!strcmp (entry->key, GCONF_PATH "/enabled")) {
		if (entry->value->type == GCONF_VALUE_BOOL) {
			dr->enabled = gconf_value_get_bool (entry->value);
			dr->state = STATE_START;

			item = gtk_ui_manager_get_widget (dr->ui_manager,
							  "/Pop/TakeABreak");
			gtk_widget_set_sensitive (item, dr->enabled);

			update_tooltip (dr);
		}
	}

	maybe_change_state (dr);
}

static void
popup_break_cb (GtkAction *action, DrWright *dr)
{
	if (dr->enabled) {
		dr->state = STATE_BREAK_SETUP;
		maybe_change_state (dr);
	}
}

static void
popup_preferences_cb (GtkAction *action, DrWright *dr)
{
	GdkScreen *screen;
	GError    *error = NULL;
	GtkWidget *menu;

	menu = gtk_ui_manager_get_widget (dr->ui_manager, "/Pop");
	screen = gtk_widget_get_screen (menu);

	if (!gdk_spawn_command_line_on_screen (screen, "gnome-keyboard-properties --typing-break", &error)) {
		GtkWidget *error_dialog;

		error_dialog = gtk_message_dialog_new (NULL, 0,
						       GTK_MESSAGE_ERROR,
						       GTK_BUTTONS_CLOSE,
						       _("Unable to bring up the typing break properties dialog with the following error: %s"),
						       error->message);
		g_signal_connect (error_dialog,
				  "response",
				  G_CALLBACK (gtk_widget_destroy), NULL);
		gtk_window_set_resizable (GTK_WINDOW (error_dialog), FALSE);
		gtk_widget_show (error_dialog);

		g_error_free (error);
	}
}

static void
popup_about_cb (GtkAction *action, DrWright *dr)
{
	gint   i;
	gchar *authors[] = {
		N_("Written by Richard Hult <richard@imendio.com>"),
		N_("Eye candy added by Anders Carlsson"),
		NULL
	};

	for (i = 0; authors [i]; i++)
		authors [i] = _(authors [i]);

	gtk_show_about_dialog (NULL,
			       "authors", authors,
			       "comments",  _("A computer break reminder."),
			       "logo-icon-name", "typing-monitor",
			       "translator-credits", _("translator-credits"),
			       "version", VERSION,
			       NULL);
}

static void
popup_menu_cb (GtkWidget *widget,
	       guint      button,
	       guint      activate_time,
	       DrWright  *dr)
{
	GtkWidget *menu;

	menu = gtk_ui_manager_get_widget (dr->ui_manager, "/Pop");

	gtk_menu_popup (GTK_MENU (menu),
			NULL,
			NULL,
			gtk_status_icon_position_menu,
			dr->icon,
			0,
			gtk_get_current_event_time());
}

static void
break_window_done_cb (GtkWidget *window,
		      DrWright  *dr)
{
	gtk_widget_destroy (dr->break_window);

	dr->state = STATE_BREAK_DONE_SETUP;
	dr->break_window = NULL;

	update_tooltip (dr);
	maybe_change_state (dr);
}

static void
break_window_postpone_cb (GtkWidget *window,
			  DrWright  *dr)
{
	gint elapsed_time;

	gtk_widget_destroy (dr->break_window);

	dr->state = STATE_TYPE;
	dr->break_window = NULL;

	elapsed_time = g_timer_elapsed (dr->timer, NULL);

	if (elapsed_time + dr->save_last_time >= dr->type_time) {
		/* Typing time has expired, but break was postponed.
		 * We'll warn again in (elapsed * sqrt (typing_time))^2 */
		gfloat postpone_time = (((float) elapsed_time) / dr->break_time)
					* sqrt (dr->type_time);
		postpone_time *= postpone_time;
		dr->save_last_time = dr->type_time - MAX (dr->warn_time, (gint) postpone_time);
	}

	g_timer_start (dr->timer);
	maybe_change_state (dr);
	update_icon (dr);
	update_tooltip (dr);
}

static void
break_window_destroy_cb (GtkWidget *window,
			 DrWright  *dr)
{
	GList *l;

	for (l = dr->secondary_break_windows; l; l = l->next) {
		gtk_widget_destroy (l->data);
	}

	g_list_free (dr->secondary_break_windows);
	dr->secondary_break_windows = NULL;
}

static void
init_tray_icon (DrWright *dr)
{
	dr->icon = gtk_status_icon_new_from_pixbuf (dr->neutral_bar);

	update_tooltip (dr);
	update_icon (dr);

	g_signal_connect (dr->icon,
			  "popup_menu",
			  G_CALLBACK (popup_menu_cb),
			  dr);
}

static GList *
create_secondary_break_windows (void)
{
	GdkDisplay *display;
	GdkScreen  *screen;
	GtkWidget  *window;
	gint        i;
	GList      *windows = NULL;

	display = gdk_display_get_default ();

	for (i = 0; i < gdk_display_get_n_screens (display); i++) {
		screen = gdk_display_get_screen (display, i);

		if (screen == gdk_screen_get_default ()) {
			/* Handled by DrwBreakWindow. */
			continue;
		}

		window = gtk_window_new (GTK_WINDOW_POPUP);

		windows = g_list_prepend (windows, window);

		gtk_window_set_screen (GTK_WINDOW (window), screen);

		gtk_window_set_default_size (GTK_WINDOW (window),
					     gdk_screen_get_width (screen),
					     gdk_screen_get_height (screen));

		gtk_widget_set_app_paintable (GTK_WIDGET (window), TRUE);
		drw_setup_background (GTK_WIDGET (window));
		gtk_window_stick (GTK_WINDOW (window));
		gtk_widget_show (window);
	}

	return windows;
}

DrWright *
drwright_new (void)
{
	DrWright  *dr;
	GtkWidget *item;
	GConfClient *client;
	GtkActionGroup *action_group;

	static const char ui_description[] =
	  "<ui>"
	  "  <popup name='Pop'>"
	  "    <menuitem action='Preferences'/>"
	  "    <menuitem action='About'/>"
	  "    <separator/>"
	  "    <menuitem action='TakeABreak'/>"
	  "  </popup>"
	  "</ui>";

        dr = g_new0 (DrWright, 1);

	client = gconf_client_get_default ();

	gconf_client_add_dir (client,
			      GCONF_PATH,
			      GCONF_CLIENT_PRELOAD_NONE,
			      NULL);

	gconf_client_notify_add (client, GCONF_PATH,
				 gconf_notify_cb,
				 dr,
				 NULL,
				 NULL);

	dr->type_time = 60 * gconf_client_get_int (
		client, GCONF_PATH "/type_time", NULL);

	dr->warn_time = MIN (dr->type_time / 12, 60*3);

	dr->break_time = 60 * gconf_client_get_int (
		client, GCONF_PATH "/break_time", NULL);

	dr->enabled = gconf_client_get_bool (
		client,
		GCONF_PATH "/enabled",
		NULL);

	g_object_unref (client);

	if (debug) {
		setup_debug_values (dr);
	}

	dr->ui_manager = gtk_ui_manager_new ();

	action_group = gtk_action_group_new ("MenuActions");
	gtk_action_group_set_translation_domain (action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions (action_group, actions, G_N_ELEMENTS (actions), dr);
	gtk_ui_manager_insert_action_group (dr->ui_manager, action_group, 0);
	gtk_ui_manager_add_ui_from_string (dr->ui_manager, ui_description, -1, NULL);

	item = gtk_ui_manager_get_widget (dr->ui_manager, "/Pop/TakeABreak");
	gtk_widget_set_sensitive (item, dr->enabled);

	dr->timer = g_timer_new ();
	dr->idle_timer = g_timer_new ();

	dr->state = STATE_START;

	dr->monitor = drw_monitor_new ();

	g_signal_connect (dr->monitor,
			  "activity",
			  G_CALLBACK (activity_detected_cb),
			  dr);

	dr->neutral_bar = gdk_pixbuf_new_from_file (IMAGEDIR "/bar.png", NULL);
	dr->red_bar = gdk_pixbuf_new_from_file (IMAGEDIR "/bar-red.png", NULL);
	dr->green_bar = gdk_pixbuf_new_from_file (IMAGEDIR "/bar-green.png", NULL);
	dr->disabled_bar = gdk_pixbuf_new_from_file (IMAGEDIR "/bar-disabled.png", NULL);

	init_tray_icon (dr);

	g_timeout_add_seconds (12,
		       (GSourceFunc) update_tooltip,
		       dr);
	g_timeout_add_seconds (1,
		       (GSourceFunc) maybe_change_state,
		       dr);

	return dr;
}
