/* -*- mode: c; style: linux -*- */

/* archive.c
 * Copyright (C) 2000-2001 Ximian, Inc.
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

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

#include "archive.h"

typedef struct _GRealTree  GRealTree;
typedef struct _GTreeNode  GTreeNode;

typedef struct _foreach_t foreach_t;

struct _foreach_t 
{
	Archive     *archive;
	LocationCB   callback;
	Location    *parent;
	gpointer     user_data;
};

static GtkObjectClass *parent_class;

static Archive *user_archive;
static Archive *global_archive;

enum {
	ARG_0,
	ARG_PREFIX
};

static void archive_init          (Archive *archive);
static void archive_class_init    (ArchiveClass *klass);

static void archive_destroy       (GtkObject *object);

static void archive_set_arg       (GtkObject *object,
				   GtkArg *arg,
				   guint arg_id);

static void archive_get_arg       (GtkObject *object,
				   GtkArg *arg,
				   guint arg_id);

static gboolean do_load           (Archive *archive);
static void load_all_locations    (Archive *archive);

guint
archive_get_type (void) 
{
	static guint archive_type;

	if (!archive_type) {
		GtkTypeInfo archive_info = {
			"Archive",
			sizeof (Archive),
			sizeof (ArchiveClass),
			(GtkClassInitFunc) archive_class_init,
			(GtkObjectInitFunc) archive_init,
			(GtkArgSetFunc) NULL,
			(GtkArgGetFunc) NULL
		};

		archive_type = 
			gtk_type_unique (gtk_object_get_type (),
					 &archive_info);
	}

	return archive_type;
}

static void
archive_init (Archive *archive) 
{
	archive->prefix = NULL;
	archive->locations = g_tree_new ((GCompareFunc) strcmp);
	archive->current_location_id = NULL;
}

static void
archive_class_init (ArchiveClass *klass) 
{
	GtkObjectClass *object_class;

	object_class = GTK_OBJECT_CLASS (klass);

	object_class->destroy = archive_destroy;
	object_class->set_arg = archive_set_arg;
	object_class->get_arg = archive_get_arg;

	gtk_object_add_arg_type ("Archive::prefix",
				 GTK_TYPE_POINTER,
				 GTK_ARG_READWRITE,
				 ARG_PREFIX);

	parent_class = gtk_type_class (gtk_object_get_type ());
}

static void
archive_set_arg (GtkObject *object, GtkArg *arg, guint arg_id) 
{
	Archive *archive;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_ARCHIVE (object));
	g_return_if_fail (arg != NULL);

	archive = ARCHIVE (object);

	switch (arg_id) {
	case ARG_PREFIX:
		if (GTK_VALUE_POINTER (*arg) != NULL)
			archive->prefix = g_strdup (GTK_VALUE_POINTER (*arg));
		break;
	default:
		break;
	}
}

static void 
archive_get_arg (GtkObject *object, GtkArg *arg, guint arg_id) 
{
	Archive *archive;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_ARCHIVE (object));
	g_return_if_fail (arg != NULL);

	archive = ARCHIVE (object);

	switch (arg_id) {
	case ARG_PREFIX:
		GTK_VALUE_POINTER (*arg) = archive->prefix;
		break;
	default:
		arg->type = GTK_TYPE_INVALID;
		break;
	}
}

/**
 * archive_load:
 * @is_global: TRUE iff we should load the global archive
 * 
 * Load either the global or per-user configuration archive
 * 
 * Return value: Reference to archive
 **/

GtkObject *
archive_load (gboolean is_global) 
{
	GtkObject *object;
	gchar *prefix;

	if (is_global && global_archive != NULL)
		return GTK_OBJECT (global_archive);
	else if (user_archive != NULL)
		return GTK_OBJECT (user_archive);

	if (is_global)
		prefix = CONFIGDIR "/helix-config";
	else
		prefix = g_concat_dir_and_file (g_get_home_dir (),
						".gnome/helix-config");

	object = gtk_object_new (archive_get_type (),
				 "prefix", prefix,
				 NULL);

	if (is_global)
		g_free (prefix);

	if (do_load (ARCHIVE (object)) == FALSE) {
		gtk_object_destroy (object);
		return NULL;
	}

	ARCHIVE (object)->is_global = is_global;
	ARCHIVE (object)->backend_list = 
		BACKEND_LIST (backend_list_new (is_global));

	if (is_global)
		global_archive = ARCHIVE (object);
	else
		user_archive = ARCHIVE (object);

	return object;
}

static gint
free_location_cb (gchar *locid, Location *location) 
{
	location_close (location);
	g_free (locid);

	return FALSE;
}

static void 
archive_destroy (GtkObject *object) 
{
	Archive *archive;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_ARCHIVE (object));

	archive = ARCHIVE (object);

	g_tree_traverse (archive->locations, 
			 (GTraverseFunc) free_location_cb,
			 G_IN_ORDER,
			 NULL);

	g_tree_destroy (archive->locations);

	if (archive->current_location_id != NULL)
		g_free (archive->current_location_id);

	GTK_OBJECT_CLASS (parent_class)->destroy (GTK_OBJECT (archive));
}

/**
 * archive_close:
 * @archive: 
 * 
 * Closes the given archive handle. Also closes all locations under this
 * archive.
 **/

void
archive_close (Archive *archive) 
{
	g_return_if_fail (archive != NULL);
	g_return_if_fail (IS_ARCHIVE (archive));

	gtk_object_destroy (GTK_OBJECT (archive));
}

/**
 * archive_get_location:
 * @archive: 
 * @locid: 
 * 
 * Get a reference to the location with the given name. 
 * 
 * Return value: Reference to location, NULL if no such location exists
 **/

Location *
archive_get_location (Archive *archive, const gchar *locid) 
{
	GtkObject *loc_obj;

	g_return_val_if_fail (archive != NULL, NULL);
	g_return_val_if_fail (IS_ARCHIVE (archive), NULL);
	g_return_val_if_fail (locid != NULL, NULL);

	loc_obj = g_tree_lookup (archive->locations, locid);

	if (!loc_obj) {
		loc_obj = location_open (archive, locid);

		if (!loc_obj) return NULL;

		if (loc_obj)
			g_tree_insert (archive->locations, 
				       g_strdup (locid), loc_obj);
	}

	if (loc_obj) {
		gtk_object_ref (loc_obj);
		return LOCATION (loc_obj);
	} else {
		return NULL;
	}
}

/**
 * archive_register_location:
 * @archive: 
 * @location: 
 * 
 * Register a location with the archive; invoked by location_new 
 **/

void
archive_register_location (Archive *archive, Location *location) 
{
	g_return_if_fail (archive != NULL);
	g_return_if_fail (IS_ARCHIVE (archive));
	g_return_if_fail (location != NULL);
	g_return_if_fail (IS_LOCATION (location));

	g_tree_insert (archive->locations,
		       g_strdup (location_get_id (location)),
		       location);
}

/**
 * archive_unregister_location:
 * @archive: 
 * @location: 
 * 
 * Unregisters a location from the archive
 **/

void
archive_unregister_location (Archive *archive, Location *location) 
{
	g_return_if_fail (archive != NULL);
	g_return_if_fail (IS_ARCHIVE (archive));
	g_return_if_fail (location != NULL);
	g_return_if_fail (IS_LOCATION (location));

	/* FIXME: We might be screwing things up here if we're traversing... */
	g_tree_remove (archive->locations, location);
}

/**
 * archive_get_current_location:
 * @archive: object
 * 
 * Convenience function to get a pointer to the current location
 * 
 * Return value: Pointer to current location
 **/

Location *
archive_get_current_location (Archive *archive)
{
	g_return_val_if_fail (archive != NULL, NULL);
	g_return_val_if_fail (IS_ARCHIVE (archive), NULL);

	return archive_get_location (archive,
				     archive_get_current_location_id
				     (archive));
}

static int
add_location_cb (Location *location, gchar *backend_id, GList *backends)
{
	g_list_insert (backends, backend_id, 1);
	return 0;
}

/**
 * archive_set_current_location:
 * @archive: object
 * @location: Location to which to set archive
 * 
 * Set the current location in an archive to the location given; apply
 * configuration for the new location to all backends necessary
 **/

void
archive_set_current_location (Archive *archive, Location *location) 
{
	GList *location_path, *backends;
	Location *old_location = archive_get_current_location (archive);

	g_return_if_fail (archive != NULL);
	g_return_if_fail (IS_ARCHIVE (archive));
	g_return_if_fail (location != NULL);
	g_return_if_fail (IS_LOCATION (location));

	archive_set_current_location_id (archive, location_get_id (location));

	location_path = location_find_path_from_common_parent
		(location, old_location);

	backends = g_list_append (NULL, NULL);

	while (location_path != NULL) {
		if (location_path->data != NULL) {
			location_foreach_backend
				(LOCATION (location_path->data), 
				 (LocationBackendCB) add_location_cb,
				 backends);
		}

		location_path = location_path->next;
	}

	location_rollback_backends_to (location, NULL, backends->next, TRUE);

	g_list_free (backends);
}

/**
 * archive_set_current_location_id: 
 * @archive: 
 * @name: 
 * 
 * Sets the current location's name, but does not invoke any rollback
 **/

void
archive_set_current_location_id (Archive *archive, const gchar *locid) 
{
	g_return_if_fail (archive != NULL);
	g_return_if_fail (IS_ARCHIVE (archive));
	g_return_if_fail (locid != NULL);

	if (archive->current_location_id != NULL)
		g_free (archive->current_location_id);

	archive->current_location_id = g_strdup (locid);

	if (archive->is_global)
		gnome_config_push_prefix ("=" LOCATION_DIR "=");
	else
		gnome_config_push_prefix ("helix-config/");

	gnome_config_set_string ("config/current/location",
				 archive->current_location_id);

	gnome_config_pop_prefix ();
	gnome_config_sync ();
}

/**
 * archive_get_current_location_id:
 * @archive: object
 * 
 * Get the name of the current location
 * 
 * Return value: String containing current location, should not be freed
 **/

const gchar *
archive_get_current_location_id (Archive *archive) 
{
	g_return_val_if_fail (archive != NULL, NULL);
	g_return_val_if_fail (IS_ARCHIVE (archive), NULL);

	if (archive->current_location_id == NULL) {
		if (archive->is_global)
			gnome_config_push_prefix ("=" LOCATION_DIR "=");
		else
			gnome_config_push_prefix ("helix-config/");

		archive->current_location_id =
			gnome_config_get_string
			("config/current/location=default");

		gnome_config_pop_prefix ();
	}

	return archive->current_location_id;
}

/**
 * archive_get_prefix:
 * @archive: 
 * 
 * Get the prefix for locations in this archive
 * 
 * Return value: String containing prefix; should not be freed
 **/

const gchar *
archive_get_prefix (Archive *archive) 
{
	g_return_val_if_fail (archive != NULL, FALSE);
	g_return_val_if_fail (IS_ARCHIVE (archive), FALSE);

	return archive->prefix;
}

/**
 * archive_is_global:
 * @archive: 
 * 
 * Tell whether the archive is global or per-user
 * 
 * Return value: TRUE if global, FALSE if per-user
 **/

gboolean
archive_is_global (Archive *archive) 
{
	g_return_val_if_fail (archive != NULL, FALSE);
	g_return_val_if_fail (IS_ARCHIVE (archive), FALSE);

	return archive->is_global;
}

/**
 * archive_get_backend_list:
 * @archive: 
 * 
 * Get the master backend list for this archive
 * 
 * Return value: Reference to the master backend list
 **/

BackendList *
archive_get_backend_list (Archive *archive)
{
	g_return_val_if_fail (archive != NULL, FALSE);
	g_return_val_if_fail (IS_ARCHIVE (archive), FALSE);

	return archive->backend_list;
}

static gint
foreach_cb (gchar *key, Location *value, foreach_t *data) 
{
	if (location_get_parent (value) == data->parent)
		return data->callback (data->archive, value, 
				       data->user_data);
	else
		return 0;
}

/**
 * archive_foreach_child_location:
 * @archive:
 * @callback: Callback to invoke
 * @parent: Iterate through the children of this location; iterate through
 * toplevel locations if this is NULL
 * @data: Arbitrary data to pass to the callback
 *
 * Invoke the given callback for each location that inherits the given
 * location, or for each toplevel location if the parent given is
 * NULL. Terminate the iteration if any child returns a nonzero value
 **/

void
archive_foreach_child_location (Archive *archive, LocationCB callback,
				Location *parent, gpointer data)
{
	foreach_t f_data;

	g_return_if_fail (archive != NULL);
	g_return_if_fail (IS_ARCHIVE (archive));

	load_all_locations (archive);

	f_data.archive = archive;
	f_data.callback = callback;
	f_data.parent = parent;
	f_data.user_data = data;

	g_tree_traverse (archive->locations,
			 (GTraverseFunc) foreach_cb,
			 G_IN_ORDER,
			 &f_data);
}

/* Load the archive information from disk; return TRUE on success and FALSE on 
 * failure
 */

static gboolean
do_load (Archive *archive)
{
	gint ret = 0;

	g_return_val_if_fail (archive != NULL, FALSE);
	g_return_val_if_fail (IS_ARCHIVE (archive), FALSE);
	g_return_val_if_fail (archive->prefix != NULL, FALSE);

	if (g_file_test (archive->prefix, G_FILE_TEST_ISDIR) == FALSE)
		ret = mkdir (archive->prefix, S_IREAD | S_IWRITE | S_IEXEC);

	if (ret == -1) return FALSE;

	return TRUE;
}

/* Load and register all the locations for this archive */

static void
load_all_locations (Archive *archive) 
{
	DIR *archive_dir;
	struct dirent entry, *entryp;
	gchar *filename;

	archive_dir = opendir (archive->prefix);

	if (archive_dir == NULL) {
		g_warning ("load_all_locations: %s", g_strerror (errno));
		return;
	}

	while (1) {
		if (readdir_r (archive_dir, &entry, &entryp)) {
			g_warning ("load_all_locations: %s",
				   g_strerror (errno));
			break;
		}

		if (entryp == NULL) break;

		if (strcmp (entry.d_name, ".") &&
		    strcmp (entry.d_name, ".."))
		{
			filename = g_concat_dir_and_file (archive->prefix,
							  entry.d_name);
			if (g_file_test (filename, G_FILE_TEST_ISDIR))
				archive_get_location (archive, entry.d_name);
		}
	}
}
