/* -*- mode: c; style: linux -*- */

/* capplet-dir.c
 * Copyright (C) 2000 Helix Code, Inc.
 * Copyright (C) 1998 Red Hat Software, Inc.
 *
 * Written by Bradford Hovinen <hovinen@helixcode.com>,
 *            Jonathan Blandford <jrb@redhat.com>
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

#include <config.h>

#include <glib.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#include "capplet-dir.h"
#include "capplet-dir-view.h"

static void capplet_activate     (Capplet *capplet);
static void capplet_dir_activate (CappletDir *capplet_dir,
				  CappletDirView *launcher);

static void capplet_shutdown     (Capplet *capplet);
static void capplet_dir_shutdown (CappletDir *capplet_dir);

static GSList *read_entries (CappletDir *dir);

static void start_capplet_through_root_manager (GnomeDesktopEntry *gde);

CappletDirView *(*get_view_cb) (CappletDir *dir, CappletDirView *launcher);

/* nice global table for capplet lookup */
GHashTable *capplet_hash = NULL;

CappletDirEntry *
capplet_new (CappletDir *dir, gchar *desktop_path) 
{
	Capplet *capplet;
	CappletDirEntry *entry;
	GdkPixbuf *pb;
	GnomeDesktopEntry *dentry;

	g_return_val_if_fail (desktop_path != NULL, NULL);

	g_print ("reading %s\n", desktop_path);

	entry = g_hash_table_lookup (capplet_hash, desktop_path);
	if (entry) {
		g_print ("hash hit!!\n");
		return entry;
	}

	dentry = gnome_desktop_entry_load (desktop_path);
	if (!dentry || !strcmp ("gnomecc", dentry->exec[0]))
		return NULL;

	capplet = g_new0 (Capplet, 1);
	entry = CAPPLET_DIR_ENTRY (capplet);

	entry->type = TYPE_CAPPLET;
	entry->entry = dentry;

	entry->label = entry->entry->name;
	entry->icon = entry->entry->icon;
	entry->path = entry->entry->location;

	entry->dir = dir;

	if (!entry->icon)
		entry->icon = PIXMAPS_DIR "/control-center.png";

	entry->pb = gdk_pixbuf_new_from_file (entry->icon);

	g_hash_table_insert (capplet_hash, g_strdup (desktop_path), entry);

	return entry;
}

CappletDirEntry *
capplet_dir_new (CappletDir *dir, gchar *dir_path) 
{
	CappletDir *capplet_dir;
	CappletDirEntry *entry;
	gchar *desktop_path;

	g_return_val_if_fail (dir_path != NULL, NULL);

	entry = g_hash_table_lookup (capplet_hash, dir_path);
	if (entry) {
		g_print ("hash hit!\n");
		return entry;
	}

	desktop_path = g_concat_dir_and_file (dir_path, ".directory");

	capplet_dir = g_new0 (CappletDir, 1);
	entry = CAPPLET_DIR_ENTRY (capplet_dir);

	entry->type = TYPE_CAPPLET_DIR;
	entry->entry = gnome_desktop_entry_load (desktop_path);
	entry->dir = dir;
	entry->path = g_strdup (dir_path);

	g_free (desktop_path);

	if (entry->entry) {
		entry->label = entry->entry->name;
		entry->icon = entry->entry->icon;

		if (!entry->icon)
			entry->icon = PIXMAPS_DIR "/control-center.png";

		entry->pb = gdk_pixbuf_new_from_file (entry->icon);
	} else {
		/* If the .directory file could not be found or read, abort */
		g_free (capplet_dir);
		return NULL;
	}

	entry->dir = dir;

	g_hash_table_insert (capplet_hash, entry->path, entry);

	capplet_dir_load (entry);

	return entry;
}

CappletDirEntry *
capplet_lookup (const char *path)
{
	return g_hash_table_lookup (capplet_hash, path);
}

void 
capplet_dir_entry_destroy (CappletDirEntry *entry)
{
	if (entry->type == TYPE_CAPPLET) {
		capplet_shutdown (CAPPLET (entry));
	} else {
		capplet_dir_shutdown (CAPPLET_DIR (entry));
		g_free (entry->path);
	}

	gnome_desktop_entry_free (entry->entry);
	g_free (entry);
}

void 
capplet_dir_entry_activate (CappletDirEntry *entry, 
			    CappletDirView *launcher)
{
	g_return_if_fail (entry != NULL);

	if (entry->type == TYPE_CAPPLET)
		capplet_activate (CAPPLET (entry));
	else if (entry->type == TYPE_CAPPLET_DIR)
		capplet_dir_activate (CAPPLET_DIR (entry), launcher);
	else
		g_assert_not_reached ();
}

void 
capplet_dir_entry_shutdown (CappletDirEntry *entry)
{
	if (entry->type == TYPE_CAPPLET)
		capplet_shutdown (CAPPLET (entry));
	else if (entry->type == TYPE_CAPPLET_DIR)
		capplet_dir_shutdown (CAPPLET_DIR (entry));
	else
		g_assert_not_reached ();
}

static void
capplet_activate (Capplet *capplet) 
{
	GnomeDesktopEntry *entry;

	entry = CAPPLET_DIR_ENTRY (capplet)->entry;

#warning FIXME: this should probably be root-manager-helper
	if (!strncmp (entry->exec[0], "root-manager", 12))
		start_capplet_through_root_manager (entry);
	else
		gnome_desktop_entry_launch (entry);
}

void
capplet_dir_load (CappletDir *capplet_dir) 
{
	if (capplet_dir->entries) return;
	capplet_dir->entries = read_entries (capplet_dir);
}

static void
capplet_dir_activate (CappletDir *capplet_dir, CappletDirView *launcher) 
{
	capplet_dir_load (capplet_dir);
	capplet_dir->view = get_view_cb (capplet_dir, launcher);

	capplet_dir_view_load_dir (capplet_dir->view, capplet_dir);
	capplet_dir_view_show (capplet_dir->view);
}

static void
capplet_shutdown (Capplet *capplet) 
{
	/* Can't do much here ... :-( */
}

static void
cde_destroy (CappletDirEntry *e, gpointer null)
{
	capplet_dir_entry_destroy (e);
}

static void
capplet_dir_shutdown (CappletDir *capplet_dir) 
{
	if (capplet_dir->view)
		gtk_object_unref (GTK_OBJECT (capplet_dir->view));

	g_slist_foreach (capplet_dir->entries, cde_destroy, NULL);

	g_slist_free (capplet_dir->entries);
}

static gint 
node_compare (gconstpointer a, gconstpointer b) 
{
	return strcmp (CAPPLET_DIR_ENTRY (a)->entry->name, 
		       CAPPLET_DIR_ENTRY (b)->entry->name);
}

/* Adapted from the original control center... */

static GSList *
read_entries (CappletDir *dir) 
{
        DIR *parent_dir;
        struct dirent *child_dir;
        struct stat filedata;
	GSList *list = NULL;
	CappletDirEntry *entry;
	gchar *fullpath, *test;
	CappletDirEntry **entry_array;
	int i;

        parent_dir = opendir (CAPPLET_DIR_ENTRY (dir)->path);
        if (parent_dir == NULL)
                return NULL;

        while ( (child_dir = readdir (parent_dir)) ) {
                if (child_dir->d_name[0] == '.')
			continue;

		/* we check to see if it is interesting. */
		fullpath = g_concat_dir_and_file (CAPPLET_DIR_ENTRY (dir)->path, child_dir->d_name);
		
		if (stat (fullpath, &filedata) == -1) {
			g_free (fullpath);
			continue;
		}

		entry = NULL;
		
		if (S_ISDIR (filedata.st_mode)) {
			entry = capplet_dir_new (dir, fullpath);
		} else {
			test = rindex(child_dir->d_name, '.');

			/* if it's a .desktop file, it's interesting for sure! */
			if (test && !strcmp (".desktop", test))
				entry = capplet_new (dir, fullpath);
		}
		
		if (entry)
			list = g_slist_prepend (list, entry);
		
		g_free (fullpath);
        }
        
        closedir (parent_dir);

	list = g_slist_sort (list, node_compare);

	/* remove FALSE for parent entry */
	return FALSE && CAPPLET_DIR_ENTRY (dir)->dir 
		? g_slist_prepend (list, CAPPLET_DIR_ENTRY (dir)->dir) 
		: list;
}

static void
start_capplet_through_root_manager (GnomeDesktopEntry *gde) 
{
	static FILE *output = NULL;
	pid_t pid;
	char *cmdline;
	char *oldexec;

	if (!output) {
		gint pipe_fd[2];
		pipe (pipe_fd);

		pid = fork ();

		if (pid == (pid_t) -1) {
			g_error ("%s", g_strerror (errno));
		}
		else if (pid == 0) {
			char *arg[2];
			int i;

			dup2 (pipe_fd[0], STDIN_FILENO);
      
			for (i = 3; i < FOPEN_MAX; i++) close(i);

			arg[0] = gnome_is_program_in_path ("root-manager-helper");
			arg[1] = NULL;
			execv (arg[0], arg);
		}
		else
		{
			output = fdopen(pipe_fd[1], "a");
		}
	}


	oldexec = gde->exec[1];
	gde->exec[1] = g_concat_dir_and_file (GNOME_SBINDIR, oldexec);

 	cmdline = g_strjoinv (" ", gde->exec + 1);

	g_free (gde->exec[1]);
	gde->exec[1] = oldexec;

	g_print ("trying: %s\n", cmdline);

	fprintf (output, "%s\n", cmdline);
	fflush (output);
	g_free (cmdline);
}

void 
capplet_dir_init (CappletDirView *(*cb) (CappletDir *, CappletDirView *)) 
{
	get_view_cb = cb;
	capplet_hash = g_hash_table_new (g_str_hash, g_str_equal);
}

CappletDir *
get_root_capplet_dir (void)
{
	static CappletDir *root_dir = NULL;

	if (root_dir == NULL) {
		root_dir = CAPPLET_DIR (capplet_dir_new (NULL, SETTINGS_DIR));

		if (!root_dir)
			g_error ("Could not find directory of control panels");
	}

	return root_dir;
}
