/* -*- mode: c; style: linux -*- */

/* mime-type-info.c
 *
 * Copyright (C) 2000 Eazel, Inc.
 * Copyright (C) 2002 Ximian, Inc.
 *
 * Written by Bradford Hovinen <hovinen@ximian.com>,
 *            Jonathan Blandford <jrb@redhat.com>,
 *            Gene Z. Ragan <gzr@eazel.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <bonobo.h>
#include <libgnomevfs/gnome-vfs-application-registry.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <gconf/gconf-client.h>

#include "libuuid/uuid.h"

#include "mime-type-info.h"
#include "mime-types-model.h"

static const gchar *get_category_name (const gchar        *mime_type);
static const gchar *get_category_description (const gchar *mime_type);
static GSList *get_lang_list          (void);
static gchar  *form_extensions_string (const MimeTypeInfo *info,
				       gchar              *sep,
				       gchar              *prepend);
static void    get_icon_pixbuf        (MimeTypeInfo       *info,
				       const gchar        *icon_path,
				       gboolean            want_large);

static MimeCategoryInfo *get_category (const gchar        *category_name,
				       const gchar        *category_desc,
				       GtkTreeModel       *model);

gchar *get_real_icon_path             (const MimeTypeInfo *info,
				       const gchar        *icon_name);



void
load_all_mime_types (GtkTreeModel *model) 
{
	GList *list, *tmp;

	list = gnome_vfs_get_registered_mime_types ();

	for (tmp = list; tmp != NULL; tmp = tmp->next)
		mime_type_info_new (tmp->data, model);

	g_list_free (list);
}

MimeTypeInfo *
mime_type_info_new (const gchar *mime_type, GtkTreeModel *model)
{
	MimeTypeInfo      *info;

	info = g_new0 (MimeTypeInfo, 1);
	MODEL_ENTRY (info)->type = MODEL_ENTRY_MIME_TYPE;

	if (mime_type != NULL) {
		info->mime_type = g_strdup (mime_type);
		mime_type_info_set_category_name (info,
			get_category_name (mime_type),
			get_category_description (mime_type), model);
	} else
		info->entry.parent = get_model_entries (model);

	return info;
}

/* Fill in the remaining fields in a MimeTypeInfo structure; suitable for
 * subsequent use in an edit dialog */

void
mime_type_info_load_all (MimeTypeInfo *info)
{
	mime_type_info_get_description (info);
	mime_type_info_get_file_extensions (info);

	if (info->icon_name == NULL)
		info->icon_name = g_strdup (gnome_vfs_mime_get_icon (info->mime_type));

	if (info->default_action == NULL && info->mime_type != NULL) {
		/* DO NOT USE gnome_vfs_mime_get_default_application
		 * it will silently remove non-existant applications
		 * which will make them seem to disappear on systems that
		 * are configured differently */
		char const *app_id = gnome_vfs_mime_get_value (
			info->mime_type, "default_application_id");

		if (app_id != NULL && app_id[0] != '\0')
			info->default_action =
				gnome_vfs_application_registry_get_mime_application (app_id);
	}

	if (info->default_action == NULL)
		info->default_action = g_new0 (GnomeVFSMimeApplication, 1);

	if (info->default_component == NULL)
		info->default_component = gnome_vfs_mime_get_default_component (info->mime_type);

	mime_type_info_get_use_category (info);
}

const gchar *
mime_type_info_get_description (MimeTypeInfo *info)
{
	if (info->description == NULL)
		info->description = g_strdup (gnome_vfs_mime_get_description (info->mime_type));

	return info->description;
}

GdkPixbuf *
mime_type_info_get_icon (MimeTypeInfo *info)
{
	if (info->small_icon_pixbuf == NULL)
		get_icon_pixbuf (info, mime_type_info_get_icon_path (info), FALSE);

	if (info->small_icon_pixbuf != NULL)
		g_object_ref (G_OBJECT (info->small_icon_pixbuf));

	return info->small_icon_pixbuf;
}

const gchar *
mime_type_info_get_icon_path (MimeTypeInfo *info) 
{
	if (info->icon_name == NULL)
		info->icon_name = g_strdup (gnome_vfs_mime_get_icon (info->mime_type));

	info->icon_path = get_real_icon_path (info, info->icon_name);

	return info->icon_path;
}

const GList *
mime_type_info_get_file_extensions (MimeTypeInfo *info)
{
	if (info->file_extensions == NULL)
		info->file_extensions = gnome_vfs_mime_get_extensions_list (info->mime_type);

	return info->file_extensions;
}

gboolean
mime_type_info_get_use_category (MimeTypeInfo *info)
{
	const gchar *tmp1;

	if (!info->use_cat_loaded) {
		tmp1 = gnome_vfs_mime_get_value (info->mime_type, "use_category_default");

		if (tmp1 != NULL && !strcmp (tmp1, "yes"))
			info->use_category = TRUE;
		else
			info->use_category = FALSE;

		info->use_cat_loaded = TRUE;
	}

	return info->use_category;
}

gchar *
mime_type_info_get_file_extensions_pretty_string (MimeTypeInfo *info)
{
	mime_type_info_get_file_extensions (info);

	return form_extensions_string (info, ", ", ".");
}

gchar *
mime_type_info_get_category_name (const MimeTypeInfo *info)
{
	return mime_category_info_get_full_description (MIME_CATEGORY_INFO (info->entry.parent));
}

void
mime_type_info_set_category_name (const MimeTypeInfo *info, const gchar *category_name, const gchar *category_desc, GtkTreeModel *model) 
{
	if (MODEL_ENTRY (info)->parent != NULL)
		model_entry_remove_child (MODEL_ENTRY (info)->parent, MODEL_ENTRY (info), model);

	if (category_name != NULL) {
		MODEL_ENTRY (info)->parent = MODEL_ENTRY (get_category (category_name, category_desc, model));

		if (MODEL_ENTRY (info)->parent != NULL)
			model_entry_insert_child (MODEL_ENTRY (info)->parent, MODEL_ENTRY (info), model);
	} else {
		MODEL_ENTRY (info)->parent = NULL;
	}
}

void
mime_type_info_set_file_extensions (MimeTypeInfo *info, GList *list)
{
	gnome_vfs_mime_extensions_list_free (info->file_extensions);
	info->file_extensions = list;
}

void
mime_type_info_save (MimeTypeInfo *info)
{
	gchar  *tmp;

	gnome_vfs_mime_freeze ();
	gnome_vfs_mime_set_description (info->mime_type, info->description);
	gnome_vfs_mime_set_icon (info->mime_type, info->icon_name);

	/* Be really anal about validating this action */
	if (info->default_action != NULL) {
		if ( info->default_action->command == NULL ||
		    *info->default_action->command == '\0' ||
		     info->default_action->id == NULL ||
		    *info->default_action->id == '\0') {
			g_warning ("Invalid application");
			gnome_vfs_mime_application_free (info->default_action);
			info->default_action = NULL;
		}
	}

	if (info->default_action != NULL) {
		gnome_vfs_mime_set_default_application (info->mime_type,
			info->default_action->id);
		gnome_vfs_mime_add_application_to_short_list (info->mime_type,
			info->default_action->id);
	} else
		gnome_vfs_mime_set_default_application (info->mime_type, NULL);

	tmp = form_extensions_string (info, " ", NULL);
	gnome_vfs_mime_set_extensions_list (info->mime_type, tmp);
	g_free (tmp);

	if (info->default_component != NULL)
		gnome_vfs_mime_set_default_component (info->mime_type, info->default_component->iid);
	else
		gnome_vfs_mime_set_default_component (info->mime_type, NULL);

	tmp = mime_type_info_get_category_name (info);
	gnome_vfs_mime_set_value (info->mime_type, "category", tmp);
	g_free (tmp);

	gnome_vfs_mime_set_value (info->mime_type, "use_category_default", info->use_category ? "yes" : "no");

	gnome_vfs_application_registry_sync ();
	gnome_vfs_mime_thaw ();
}

void
mime_type_info_free (MimeTypeInfo *info)
{
	g_free (info->mime_type);
	g_free (info->description);
	g_free (info->icon_name);
	g_free (info->icon_path);
	gnome_vfs_mime_extensions_list_free (info->file_extensions);
	CORBA_free (info->default_component);
	gnome_vfs_mime_application_free (info->default_action);

	if (info->icon_pixbuf != NULL)
		g_object_unref (G_OBJECT (info->icon_pixbuf));
	if (info->small_icon_pixbuf != NULL)
		g_object_unref (G_OBJECT (info->small_icon_pixbuf));

	g_free (info);
}

MimeCategoryInfo *
mime_category_info_new (MimeCategoryInfo *parent, const gchar *name, const gchar *description, GtkTreeModel *model) 
{
	MimeCategoryInfo      *info;

	info = g_new0 (MimeCategoryInfo, 1);
	MODEL_ENTRY (info)->type = MODEL_ENTRY_CATEGORY;

	info->name = g_strdup (name);
	info->description = g_strdup (description);

	if (parent != NULL)
		MODEL_ENTRY (info)->parent = MODEL_ENTRY (parent);
	else
		MODEL_ENTRY (info)->parent = get_model_entries (model);

	return info;
}

static gchar *
get_gconf_base_name (MimeCategoryInfo *category) 
{
	gchar *tmp, *tmp1;

	tmp1 = mime_category_info_get_full_name (category);

	for (tmp = tmp1; *tmp != '\0'; tmp++)
		if (g_ascii_isspace (*tmp) || *tmp == '(' || *tmp == ')')
			*tmp = '-';

	tmp = g_strconcat ("/desktop/gnome/file-types-categories/", tmp1, NULL);

	g_free (tmp1);
	return tmp;
}

void
mime_category_info_load_all (MimeCategoryInfo *category)
{
	gchar *tmp, *tmp1;
	gchar *appid;

	tmp1 = get_gconf_base_name (category);

	if (category->default_action == NULL) {
		tmp = g_strconcat (tmp1, "/default-action-id", NULL);
		appid = gconf_client_get_string (gconf_client_get_default (), tmp, NULL);
		g_free (tmp);

		if (appid != NULL && *appid != '\0')
			category->default_action = gnome_vfs_application_registry_get_mime_application (appid);

		/* This must be non NULL, so be extra careful incase gnome-vfs
		 * spits back a NULL
		 */
		if (category->default_action == NULL)
			category->default_action = g_new0 (GnomeVFSMimeApplication, 1);
	}

	if (!category->use_parent_cat_loaded) {
		if (category->entry.parent->type == MODEL_ENTRY_CATEGORY) {
			tmp = g_strconcat (tmp1, "/use-parent-category", NULL);
			category->use_parent_category = gconf_client_get_bool (gconf_client_get_default (), tmp, NULL);
			g_free (tmp);
		} else {
			category->use_parent_category = FALSE;
		}

		category->use_parent_cat_loaded = TRUE;
	}

	g_free (tmp1);
}

static void
set_subcategory_ids (ModelEntry *entry, MimeCategoryInfo *category, gchar *app_id) 
{
	ModelEntry *tmp;

	switch (entry->type) {
	case MODEL_ENTRY_MIME_TYPE:
		if (MIME_TYPE_INFO (entry)->use_category)
			gnome_vfs_mime_set_default_application (MIME_TYPE_INFO (entry)->mime_type, app_id);

		break;

	case MODEL_ENTRY_CATEGORY:
		if (entry != MODEL_ENTRY (category) && MIME_CATEGORY_INFO (entry)->use_parent_category)
			for (tmp = entry->first_child; tmp != NULL; tmp = tmp->next)
				set_subcategory_ids (tmp, category, app_id);
		break;

	default:
		break;
	}
}

void
mime_category_info_save (MimeCategoryInfo *category) 
{
	gchar   *key, *basename;
	gboolean set_ids;

	g_warning ("Do not call this, nothing actually observes the gconf settings");
	return;

	/* Be really anal about validating this action */
	if (category->default_action != NULL) {
		if ( category->default_action->command == NULL ||
		    *category->default_action->command == '\0' ||
		     category->default_action->id == NULL ||
		    *category->default_action->id == '\0') {
			g_warning ("Invalid application");
			gnome_vfs_mime_application_free (category->default_action);
			category->default_action = NULL;
		}
	}

	basename = get_gconf_base_name (category);
	key = g_strconcat (basename, "/default-action-id", NULL);
	if ((set_ids = (category->default_action != NULL)))
		gconf_client_set_string (gconf_client_get_default (),
					 key, category->default_action->id, NULL);
	else
		gconf_client_unset (gconf_client_get_default (), key, NULL);
	g_free (key);

	key = g_strconcat (basename, "/use-parent-category", NULL);
	gconf_client_set_bool (gconf_client_get_default (), key,
		category->use_parent_category, NULL);
	g_free (key);
	g_free (basename);

	if (set_ids)
		set_subcategory_ids (MODEL_ENTRY (category), category, category->default_action->id);
}

static GList *
find_possible_supported_apps (ModelEntry *entry, gboolean top) 
{
	GList      *ret;
	ModelEntry *tmp;

	if (entry == NULL) return NULL;

	switch (entry->type) {
	case MODEL_ENTRY_CATEGORY:
		if (!top && !MIME_CATEGORY_INFO (entry)->use_parent_category)
			return NULL;

		for (tmp = entry->first_child; tmp != NULL; tmp = tmp->next) {
			ret = find_possible_supported_apps (tmp, FALSE);

			if (ret != NULL)
				return ret;
		}

		return NULL;

	case MODEL_ENTRY_MIME_TYPE:
		if (mime_type_info_get_use_category (MIME_TYPE_INFO (entry)))
			return gnome_vfs_application_registry_get_applications (MIME_TYPE_INFO (entry)->mime_type);
		else
			return NULL;

	default:
		return NULL;
	}
}

static GList *
intersect_lists (GList *list, GList *list1) 
{
	GList *tmp, *tmp1, *tmpnext;

	tmp = list;

	while (tmp != NULL) {
		tmpnext = tmp->next;

		for (tmp1 = list1; tmp1 != NULL; tmp1 = tmp1->next)
			if (!strcmp (tmp->data, tmp1->data))
				break;

		if (tmp1 == NULL)
			list = g_list_remove_link (list, tmp);

		tmp = tmpnext;
	}

	return list;
}

static GList *
reduce_supported_app_list (ModelEntry *entry, GList *list, gboolean top) 
{
	GList      *type_list;
	ModelEntry *tmp;

	switch (entry->type) {
	case MODEL_ENTRY_CATEGORY:
		if (!top && !MIME_CATEGORY_INFO (entry)->use_parent_category)
			break;

		for (tmp = entry->first_child; tmp != NULL; tmp = tmp->next)
			list = reduce_supported_app_list (tmp, list, FALSE);
		break;

	case MODEL_ENTRY_MIME_TYPE:
		if (mime_type_info_get_use_category (MIME_TYPE_INFO (entry))) {
			type_list = gnome_vfs_application_registry_get_applications (MIME_TYPE_INFO (entry)->mime_type);
			list = intersect_lists (list, type_list);
			g_list_free (type_list);
		}

		break;

	default:
		break;
	}

	return list;
}

GList *
mime_category_info_find_apps (MimeCategoryInfo *info)
{
	return reduce_supported_app_list (MODEL_ENTRY (info), 
		find_possible_supported_apps (MODEL_ENTRY (info), TRUE), TRUE);
}

gchar *
mime_category_info_get_full_name (MimeCategoryInfo *info) 
{
	GString *string;
	ModelEntry *tmp;
	gchar *ret, *s;

	string = g_string_new ("");

	for (tmp = MODEL_ENTRY (info); tmp != NULL && tmp->type != MODEL_ENTRY_NONE; tmp = tmp->parent) {
		g_string_prepend (string, MIME_CATEGORY_INFO (tmp)->name);
		g_string_prepend (string, "/");
	}

	/* work around gcc 2.96 bug */
	s = (*string->str == '\0') ? string->str : string->str + 1;
	ret = g_strdup (s);
		
	g_string_free (string, TRUE);
	return ret;
}

gchar *
mime_category_info_get_full_description (MimeCategoryInfo *info) 
{
	GString *string;
	ModelEntry *tmp;
	gchar *ret, *s;

	string = g_string_new ("");

	for (tmp = MODEL_ENTRY (info); tmp != NULL && tmp->type != MODEL_ENTRY_NONE; tmp = tmp->parent) {
		g_string_prepend (string, MIME_CATEGORY_INFO (tmp)->description);
		g_string_prepend (string, "/");
	}


	/* work around gcc 2.96 bug */
	s = (*string->str == '\0') ? string->str : string->str + 1;
	ret = g_strdup (s);

	g_string_free (string, TRUE);
	return ret;
}

char *
mime_type_get_pretty_name_for_server (Bonobo_ServerInfo *server)
{
        const char *view_as_name;       
	char       *display_name;
        GSList     *langs;

        display_name = NULL;

        langs = get_lang_list ();
        view_as_name = bonobo_server_info_prop_lookup (server, "nautilus:view_as_name", langs);
		
        if (view_as_name == NULL)
                view_as_name = bonobo_server_info_prop_lookup (server, "name", langs);

        if (view_as_name == NULL)
                view_as_name = server->iid;
       
	g_slist_foreach (langs, (GFunc) g_free, NULL);
        g_slist_free (langs);

	/* if the name is an OAFIID, clean it up for display */
	if (!strncmp (view_as_name, "OAFIID:", strlen ("OAFIID:"))) {
		char *display_name, *colon_ptr;

		display_name = g_strdup (view_as_name + strlen ("OAFIID:"));
		colon_ptr = strchr (display_name, ':');
		if (colon_ptr)
			*colon_ptr = '\0';

		return display_name;					
	}
			
        return g_strdup_printf ("View as %s", view_as_name);
}

static MimeTypeInfo *
get_mime_type_info_int (ModelEntry *entry, const gchar *mime_type) 
{
	ModelEntry *tmp;
	MimeTypeInfo *ret;

	switch (entry->type) {
	case MODEL_ENTRY_MIME_TYPE:
		if (!strcmp (MIME_TYPE_INFO (entry)->mime_type, mime_type))
			return MIME_TYPE_INFO (entry);

		return NULL;

	case MODEL_ENTRY_CATEGORY:
	case MODEL_ENTRY_NONE:
		for (tmp = entry->first_child; tmp != NULL; tmp = tmp->next)
			if ((ret = get_mime_type_info_int (tmp, mime_type)) != NULL)
				return ret;

		return NULL;

	default:
		return NULL;
	}
}

MimeTypeInfo *
get_mime_type_info (const gchar *mime_type)
{
	return get_mime_type_info_int (get_model_entries (NULL), mime_type);
}



static const gchar *
get_category_name (const gchar *mime_type) 
{
	const gchar *path;

	path = gnome_vfs_mime_get_value (mime_type, "category");

	if (path != NULL)
		return g_strdup (path);
	else if (!strncmp (mime_type, "image/", strlen ("image/")))
		return "Images";
	else if (!strncmp (mime_type, "video/", strlen ("video/")))
		return "Video";
	else if (!strncmp (mime_type, "audio/", strlen ("audio/")))
		return "Audio";
	else
		return "Misc";
}

static const gchar *
get_category_description (const gchar *mime_type) 
{
	const gchar *path;

	path = gnome_vfs_mime_get_value (mime_type, "category");

	if (path != NULL)
		return g_strdup (path);
	else if (!strncmp (mime_type, "image/", strlen ("image/")))
		return _("Images");
	else if (!strncmp (mime_type, "video/", strlen ("video/")))
		return _("Video");
	else if (!strncmp (mime_type, "audio/", strlen ("audio/")))
		return _("Audio");
	else
		return _("Misc");
}

static GSList *
get_lang_list (void)
{
        GSList *retval;
        const char *lang;
        char *equal_char;

        retval = NULL;

        lang = g_getenv ("LANGUAGE");

        if (lang == NULL)
                lang = g_getenv ("LANG");


        if (lang != NULL) {
                equal_char = strchr (lang, '=');
                if (equal_char != NULL)
                        lang = equal_char + 1;

                retval = g_slist_prepend (retval, g_strdup (lang));
        }
        
        return retval;
}

static gchar *
form_extensions_string (const MimeTypeInfo *info, gchar *sep, gchar *prepend) 
{
	gchar *tmp;
	gchar **array;
	GList *l;
	gint i = 0;

	if (prepend == NULL)
		prepend = "";

	array = g_new0 (gchar *, g_list_length (info->file_extensions) + 1);
	for (l = info->file_extensions; l != NULL; l = l->next)
		array[i++] = g_strconcat (prepend, l->data, NULL);
	tmp = g_strjoinv (sep, array);
	g_strfreev (array);

	return tmp;
}

gchar *get_real_icon_path (const MimeTypeInfo *info, const gchar *icon_name) 
{
	gchar *tmp, *tmp1, *ret, *real_icon_name;

	if (icon_name == NULL || *icon_name == '\0') {
		tmp = g_strdup (info->mime_type);
		tmp1 = strchr (tmp, '/');
		if (tmp1 != NULL) *tmp1 = '-';
		real_icon_name = g_strconcat ("document-icons/gnome-", tmp, ".png", NULL);
		g_free (tmp);
	} else {
		real_icon_name = g_strdup (icon_name);
	}

	ret = gnome_vfs_icon_path_from_filename (real_icon_name);

	if (ret == NULL && strstr (real_icon_name, ".png") == NULL) {
		tmp = g_strconcat (real_icon_name, ".png", NULL);
		ret = gnome_vfs_icon_path_from_filename (tmp);
		g_free (tmp);
	}

	if (ret == NULL) {
		tmp = g_strconcat ("nautilus/", real_icon_name, NULL);
		ret = gnome_vfs_icon_path_from_filename (tmp);
		g_free (tmp);
	}

	if (ret == NULL && strstr (real_icon_name, ".png") == NULL) {
		tmp = g_strconcat ("nautilus/", real_icon_name, ".png", NULL);
		ret = gnome_vfs_icon_path_from_filename (tmp);
		g_free (tmp);
	}

	if (ret == NULL)
		ret = gnome_vfs_icon_path_from_filename ("nautilus/i-regular-24.png");

	g_free (real_icon_name);

	return ret;
}

/* Loads a pixbuf for the icon, falling back on the default icon if
 * necessary
 */

void
get_icon_pixbuf (MimeTypeInfo *info, const gchar *icon_path, gboolean want_large) 
{
	static GHashTable *icon_table = NULL;

	if (icon_path == NULL)
		icon_path = get_real_icon_path (info, NULL);

	if (icon_path == NULL)
		return;

	if ((want_large && info->icon_pixbuf != NULL) || info->small_icon_pixbuf != NULL)
		return;

	if (icon_table == NULL)
		icon_table = g_hash_table_new (g_str_hash, g_str_equal);

	if (!want_large)
		info->small_icon_pixbuf = g_hash_table_lookup (icon_table, icon_path);

	if (info->small_icon_pixbuf != NULL) {
		g_object_ref (G_OBJECT (info->small_icon_pixbuf));
	} else {
		info->icon_pixbuf = gdk_pixbuf_new_from_file (icon_path, NULL);

		if (info->icon_pixbuf == NULL) {
			get_icon_pixbuf (info, NULL, want_large);
		}
		else if (!want_large) {
			info->small_icon_pixbuf =
				gdk_pixbuf_scale_simple (info->icon_pixbuf, 16, 16, GDK_INTERP_HYPER);

			g_hash_table_insert (icon_table, g_strdup (icon_path), info->small_icon_pixbuf);
		}
	}
}

static MimeCategoryInfo *
get_category (const gchar *category_name, const gchar *category_desc, GtkTreeModel *model)
{
	ModelEntry *current, *child;
	gchar **cf = NULL, **df = NULL;
	gchar **categories = NULL, **desc_categories = NULL;
	int i;

	if (category_name == NULL && category_desc == NULL)
		return NULL;

	if (category_name != NULL)
		categories = cf = g_strsplit (category_name, "/", -1);
	if (category_desc != NULL)
		desc_categories = df = g_strsplit (category_desc, "/", -1);
	if (category_name == NULL)
		categories = desc_categories;
	else if (category_desc == NULL)
		desc_categories = categories;

	current = get_model_entries (model);

	for (i = 0; categories[i] != NULL; i++) {
		for (child = current->first_child; child != NULL; child = child->next) {
			if (child->type != MODEL_ENTRY_CATEGORY)
				continue;

			if (!g_ascii_strcasecmp ((category_name == NULL) ?
				     MIME_CATEGORY_INFO (child)->description :
				     MIME_CATEGORY_INFO (child)->name,
				     categories[i]))
				break;
		}

		if (child == NULL) {
			child = MODEL_ENTRY (mime_category_info_new (MIME_CATEGORY_INFO (current), categories[i], 
								     desc_categories[i], model));
			model_entry_insert_child (MODEL_ENTRY (child)->parent, MODEL_ENTRY (child), model);
		}

		current = child;
	}

	g_strfreev (cf);
	g_strfreev (df);

	return MIME_CATEGORY_INFO (current);
}
