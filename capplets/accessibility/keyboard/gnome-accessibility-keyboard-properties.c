/* -*- mode: c; style: linux -*- */

/* gnome-accessibility-keyboard-properties.c
 * Copyright (C) 2002 Ximian, Inc.
 *
 * Written by: Jody Goldberg <jody@gnome.org>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gnome.h>
#include <gconf/gconf-client.h>

#include <capplet-util.h>
#include <activate-settings-daemon.h>
#include "accessibility-keyboard.h"

#ifdef HAVE_X11_EXTENSIONS_XKB_H
#  include <X11/X.h>
#  include <X11/Xlib.h>
#  include <X11/XKBlib.h>
#  include <X11/extensions/XKBstr.h>
#  include <gdk/gdk.h>
#  include <gdk/gdkx.h>

static void
xkb_enabled (void)
{
	gboolean have_xkb = FALSE;
	int opcode, errorBase, major, minor, xkbEventBase;

	gdk_error_trap_push ();
	have_xkb = XkbQueryExtension (GDK_DISPLAY (),
				      &opcode, &xkbEventBase, &errorBase, &major, &minor)
		&& XkbUseExtension (GDK_DISPLAY (), &major, &minor);
	XSync (GDK_DISPLAY (), FALSE);
	gdk_error_trap_pop ();

	if (!have_xkb) {
		GtkWidget *warn = gtk_message_dialog_new (NULL, 0,
		      GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
		      _("This system does not seem to have the XKB extension.  The keyboard accessibility features will not operate without it."));
		gtk_dialog_run (GTK_DIALOG (warn));
		gtk_widget_destroy (warn);
	}
}
#endif

static void
dialog_response (GtkWidget *widget,
		 gint       response_id,
		 GConfChangeSet *changeset)
{
	switch (response_id) {
	case GTK_RESPONSE_HELP:
		capplet_help (GTK_WINDOW (widget),
			      "user-guide.xml",
			      "goscustaccess-6");
		break;
	case GTK_RESPONSE_DELETE_EVENT:
	case GTK_RESPONSE_CLOSE:
		gtk_main_quit ();
		break;
	default:
		/* Import CDE AccessX File */
		break;
	}
}

int
main (int argc, char **argv) 
{
	GtkWidget *dialog;
	GConfChangeSet *changeset;
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gnome_program_init ("gnome-accessibility-keyboard-properties", VERSION,
			    LIBGNOMEUI_MODULE, argc, argv,
			    GNOME_PARAM_APP_DATADIR, GNOMECC_DATA_DIR,
			    NULL);
	activate_settings_daemon ();

#ifdef HAVE_X11_EXTENSIONS_XKB_H
	xkb_enabled ();
#endif

	changeset = NULL;
	dialog = setup_accessX_dialog (changeset);
	g_signal_connect (G_OBJECT (dialog),
		"response",
		G_CALLBACK (dialog_response), changeset);
	capplet_set_icon (dialog, "gnome-settings-accessibility-keyboard");
	gtk_widget_show_all (dialog);
	gtk_main ();

	return 0;
}
