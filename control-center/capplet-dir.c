/* vim: set sw=8: -*- mode: c; style: linux -*- */

/* capplet-dir.c
 * Copyright (C) 2000, 2001 Ximian, Inc.
 * Copyright (C) 1998 Red Hat Software, Inc.
 *
 * Written by Bradford Hovinen <hovinen@ximian.com>,
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

#include <string.h>
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

#ifdef USE_ROOT_MANAGER
static void start_capplet_through_root_manager (GnomeDesktopItem *gde);
#endif

CappletDirView *(*get_view_cb) (CappletDir *dir, CappletDirView *launcher);

/* nice global table for capplet lookup */
GHashTable *capplet_hash = NULL;

/********************************************************************
 *
 * Stolen from nautilus to keep control center and nautilus in sync
 */
static gboolean
eel_str_has_suffix (const char *haystack, const char *needle)
{
	const char *h, *n;

	if (needle == NULL) {
		return TRUE;
	}
	if (haystack == NULL) {
		return needle[0] == '\0';
	}
		
	/* Eat one character at a time. */
	h = haystack + strlen(haystack);
	n = needle + strlen(needle);
	do {
		if (n == needle) {
			return TRUE;
		}
		if (h == haystack) {
			return FALSE;
		}
	} while (*--h == *--n);
	return FALSE;
}
static char *   
eel_str_strip_trailing_str (const char *source, const char *remove_this)
{
	const char *end;
	if (source == NULL) {
		return NULL;
	}
	if (remove_this == NULL) {
		return g_strdup (source);
	}
	end = source + strlen (source);
	if (strcmp (end - strlen (remove_this), remove_this) != 0) {
		return g_strdup (source);
	}
	else {
		return g_strndup (source, strlen (source) - strlen(remove_this));
	}
	
}
static char *
nautilus_remove_icon_file_name_suffix (const char *icon_name)
{
	guint i;
	const char *suffix;
	static const char *icon_file_name_suffixes[] = { ".svg", ".svgz", ".png", ".jpg", ".xpm" };

	for (i = 0; i < G_N_ELEMENTS (icon_file_name_suffixes); i++) {
		suffix = icon_file_name_suffixes[i];
		if (eel_str_has_suffix (icon_name, suffix)) {
			return eel_str_strip_trailing_str (icon_name, suffix);
		}
	}
	return g_strdup (icon_name);
}
/********************************************************************/

static GdkPixbuf * 
find_icon (GnomeDesktopItem *dentry) 
{
	GdkPixbuf *res;
	char const *icon;
	GtkIconTheme *icon_theme = gtk_icon_theme_get_default ();

	icon = gnome_desktop_item_get_string (dentry, GNOME_DESKTOP_ITEM_ICON);

	if (icon == NULL || icon[0] == 0)
		icon = "gnome-settings";
	else if (g_path_is_absolute (icon))
		res = gdk_pixbuf_new_from_file (icon, NULL);
	else  {
		char *no_suffix = nautilus_remove_icon_file_name_suffix (icon);
		res = gtk_icon_theme_load_icon (icon_theme, no_suffix, 48, 0, NULL);
		g_free (no_suffix);
		if (res == NULL) {
			char *path = g_build_filename (GNOMECC_ICONS_DIR, icon, NULL);
			res = gdk_pixbuf_new_from_file (path, NULL);
			g_free (path);
		}
	}
	if (res == NULL)
		res = gtk_icon_theme_load_icon (icon_theme, "gnome-unknown", 48, 0, NULL);
	if (res == NULL)
		res = gtk_icon_theme_load_icon (icon_theme, "gtk-missing-image", 48, 0, NULL);
	return res;
}

CappletDirEntry *
capplet_new (CappletDir *dir, gchar *desktop_path) 
{
	Capplet *capplet;
	CappletDirEntry *entry;
	GnomeDesktopItem *dentry;
	gchar *path, **vec;
	const gchar *execstr;

	g_return_val_if_fail (desktop_path != NULL, NULL);

	entry = g_hash_table_lookup (capplet_hash, desktop_path);
	if (entry) {
		return entry;
	}

	dentry = gnome_desktop_item_new_from_uri (desktop_path,
						  GNOME_DESKTOP_ITEM_TYPE_NULL,
						  NULL);
	if (dentry == NULL)
		return NULL;

	execstr = gnome_desktop_item_get_string (dentry,
			GNOME_DESKTOP_ITEM_EXEC);
	/* Perhaps use poptParseArgvString here */
	vec = g_strsplit (execstr, " ", 0);
	if (!(execstr && execstr[0]) || !(vec && (path = g_find_program_in_path (vec[0]))))
	{
		g_strfreev (vec);
		gnome_desktop_item_unref (dentry);
		return NULL;
	}
	g_free (path);

	capplet = g_new0 (Capplet, 1);
	capplet->launching = FALSE;

	entry = CAPPLET_DIR_ENTRY (capplet);

	entry->type = TYPE_CAPPLET;
	entry->entry = dentry;

	entry->label = g_strdup (gnome_desktop_item_get_localestring (dentry,
			GNOME_DESKTOP_ITEM_NAME));
	entry->icon = find_icon (dentry);
	entry->uri = gnome_vfs_uri_new (desktop_path);
	entry->exec = vec;
	entry->dir = dir;

	g_hash_table_insert (capplet_hash, g_strdup (desktop_path), entry);

	return entry;
}

CappletDirEntry *
capplet_dir_new (CappletDir *dir, gchar *dir_path)
{
	CappletDir *capplet_dir;
	CappletDirEntry *entry;
	GnomeVFSURI *desktop_uri;
	GnomeVFSURI *dir_uri;
	char *desktop_uri_string;

	g_return_val_if_fail (dir_path != NULL, NULL);

	dir_uri = gnome_vfs_uri_new (dir_path);

	entry = g_hash_table_lookup (capplet_hash, dir_path);
	if (entry) {
		return entry;
	}

	desktop_uri = gnome_vfs_uri_append_file_name (dir_uri, ".directory");
	desktop_uri_string = gnome_vfs_uri_to_string (desktop_uri, GNOME_VFS_URI_HIDE_NONE);

	capplet_dir = g_new0 (CappletDir, 1);
	entry = CAPPLET_DIR_ENTRY (capplet_dir);

	entry->type = TYPE_CAPPLET_DIR;
	entry->entry = gnome_desktop_item_new_from_uri (desktop_uri_string,
			GNOME_DESKTOP_ITEM_TYPE_NULL,
			NULL);
	entry->dir = dir;
	entry->uri = dir_uri;

	gnome_vfs_uri_unref (desktop_uri);
	g_free (desktop_uri_string);

	if (entry->entry) {
		entry->label = g_strdup (gnome_desktop_item_get_localestring (
				entry->entry,
				GNOME_DESKTOP_ITEM_NAME));
		entry->icon = find_icon (entry->entry);
	} else {
		/* If the .directory file could not be found or read, abort */
		g_free (capplet_dir);
		return NULL;
	}

	entry->dir = dir;

	g_hash_table_insert (capplet_hash, g_strdup (dir_path), entry);

	capplet_dir_load (CAPPLET_DIR (entry));

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
	}

	g_free (entry->label);
	g_object_unref (entry->icon);
	gnome_vfs_uri_unref (entry->uri);
	g_strfreev (entry->exec);
	gnome_desktop_item_unref (entry->entry);
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

static gint
capplet_reset_cb (Capplet *capplet) 
{
	capplet->launching = FALSE;
	return FALSE;
}

static void
capplet_activate (Capplet *capplet) 
{
	CappletDirEntry *entry;

	entry = CAPPLET_DIR_ENTRY (capplet);

	if (capplet->launching) {
		return;
	} else {
		capplet->launching = TRUE;
		gtk_timeout_add (1000, (GtkFunction) capplet_reset_cb, capplet);
		gnome_desktop_item_launch (entry->entry, NULL, 0, NULL);
	}
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
		g_object_unref (G_OBJECT (capplet_dir->view));

	g_slist_foreach (capplet_dir->entries, (GFunc) cde_destroy, NULL);

	g_slist_free (capplet_dir->entries);
}

static gint 
node_compare (gconstpointer a, gconstpointer b) 
{
	return strcmp (CAPPLET_DIR_ENTRY (a)->label, 
		       CAPPLET_DIR_ENTRY (b)->label);
}

/* Adapted from the original control center... */

static GSList *
read_entries (CappletDir *dir) 
{
	GSList *list = NULL;
	CappletDirEntry *entry;
	gchar *fullpath, *test;
	GnomeVFSURI *fulluri;
	GnomeVFSDirectoryHandle *parent_dir;
	GnomeVFSResult result;

	GnomeVFSFileInfo *child = gnome_vfs_file_info_new ();

	result = gnome_vfs_directory_open_from_uri (&parent_dir, CAPPLET_DIR_ENTRY (dir)->uri,
						    GNOME_VFS_FILE_INFO_DEFAULT);

	if (result != GNOME_VFS_OK) {
		gnome_vfs_file_info_unref (child);
	        return NULL;
	}
		
	while ( gnome_vfs_directory_read_next (parent_dir, child) == GNOME_VFS_OK ) {
      	        if (child->name[0] == '.')
	                continue;
		 
		fulluri = gnome_vfs_uri_append_path (CAPPLET_DIR_ENTRY (dir)->uri, child->name);
		fullpath = gnome_vfs_uri_to_string (fulluri, GNOME_VFS_URI_HIDE_NONE);

		entry = NULL;

		if (child->type == GNOME_VFS_FILE_TYPE_DIRECTORY) {
		        entry = capplet_dir_new (dir, fullpath);
		} else {
			test = strrchr(child->name, '.');

			/* if it's a .desktop file, it's interesting for sure! */
			if (test && !strcmp (".desktop", test))
				entry = capplet_new (dir, fullpath);
		}

		if (entry)
			list = g_slist_prepend (list, entry);
		
		g_free (fullpath);
        }
        
	gnome_vfs_file_info_unref (child);
	gnome_vfs_directory_close (parent_dir);

	list = g_slist_sort (list, node_compare);

	/* remove FALSE for parent entry */
	return FALSE && CAPPLET_DIR_ENTRY (dir)->dir 
		? g_slist_prepend (list, CAPPLET_DIR_ENTRY (dir)->dir) 
		: list;
}

#ifdef USE_ROOT_MANAGER
static void
start_capplet_through_root_manager (GnomeDesktopItem *gde) 
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
		} else if (pid == 0) {
			char *arg[2];
			int i;

			dup2 (pipe_fd[0], STDIN_FILENO);
      
			for (i = 3; i < FOPEN_MAX; i++) close(i);

			arg[0] = gnome_is_program_in_path ("root-manager-helper");
			arg[1] = NULL;
			execv (arg[0], arg);
		} else {
			output = fdopen(pipe_fd[1], "a");
		}

		capplet_dir_views_set_authenticated (TRUE);
	}


	oldexec = gde->exec[1];
	gde->exec[1] = g_concat_dir_and_file (GNOME_SBINDIR, oldexec);

 	cmdline = g_strjoinv (" ", gde->exec + 1);

	g_free (gde->exec[1]);
	gde->exec[1] = oldexec;

	fprintf (output, "%s\n", cmdline);
	fflush (output);
	g_free (cmdline);
}
#endif

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
		CappletDirEntry *entry;

		entry = capplet_dir_new (NULL, "preferences:///");

		if (entry)
			root_dir = CAPPLET_DIR (entry);
		if (!root_dir)
			g_warning ("Could not find directory of control panels [%s]", "preferences:///");
	}

	return root_dir;
}
