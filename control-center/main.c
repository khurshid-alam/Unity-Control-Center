/* -*- mode: c; style: linux -*- */

/* main.c
 * Copyright (C) 2000 Helix Code, Inc.
 *
 * Written by Bradford Hovinen (hovinen@helixcode.com)
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
#include <glade/glade.h>

#include "capplet-dir.h"
#include "capplet-dir-view.h"

int
main (int argc, char **argv) 
{
        bindtextdomain (PACKAGE, GNOMELOCALEDIR);
        textdomain (PACKAGE);

	gnome_init ("control-center", VERSION, argc, argv);
	glade_gnome_init ();
	gnomecc_init ();

	capplet_dir_entry_activate 
		(CAPPLET_DIR_ENTRY (get_root_capplet_dir ()), NULL);

	gtk_main ();

	return 0;
}
