/* -*- mode: c; style: linux -*- */

/* mime-category-edit-dialog.c
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Written by Bradford Hovinen <hovinen@ximian.com>
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
# include "config.h"
#endif

#include <string.h>
#include <glade/glade.h>
#include <libgnomevfs/gnome-vfs-application-registry.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>

#include "mime-types-model.h"
#include "mime-type-info.h"
#include "mime-category-edit-dialog.h"
#include "mime-edit-dialog.h"

#define WID(x) (glade_xml_get_widget (dialog->p->dialog_xml, x))

enum {
	PROP_0,
	PROP_MODEL,
	PROP_INFO
};

struct _MimeCategoryEditDialogPrivate 
{
	GladeXML         *dialog_xml;
	GtkWidget        *dialog_win;
	MimeCategoryInfo *info;

	GtkTreeModel     *model;

	gboolean      default_action_active    : 1;
	gboolean      custom_action            : 1;
	gboolean      use_cat_dfl              : 1;
};

static GObjectClass *parent_class;

static void mime_category_edit_dialog_init        (MimeCategoryEditDialog *dialog,
						   MimeCategoryEditDialogClass *class);
static void mime_category_edit_dialog_class_init  (MimeCategoryEditDialogClass *class);
static void mime_category_edit_dialog_base_init   (MimeCategoryEditDialogClass *class);

static void mime_category_edit_dialog_set_prop    (GObject      *object, 
						   guint         prop_id,
						   const GValue *value, 
						   GParamSpec   *pspec);
static void mime_category_edit_dialog_get_prop    (GObject      *object,
						   guint         prop_id,
						   GValue       *value,
						   GParamSpec   *pspec);

static void mime_category_edit_dialog_dispose     (GObject *object);
static void mime_category_edit_dialog_finalize    (GObject *object);

static void populate_application_list             (MimeCategoryEditDialog *dialog);

static void fill_dialog                           (MimeCategoryEditDialog *dialog);
static void store_data                            (MimeCategoryEditDialog *dialog);

static void default_action_changed_cb             (MimeCategoryEditDialog *dialog);
static void use_category_toggled_cb               (MimeCategoryEditDialog *dialog,
						   GtkToggleButton        *tb);
static void response_cb                           (MimeCategoryEditDialog *dialog,
						   gint                    response_id);

static void update_sensitivity                    (MimeCategoryEditDialog *dialog);

GType
mime_category_edit_dialog_get_type (void)
{
	static GType mime_category_edit_dialog_type = 0;

	if (!mime_category_edit_dialog_type) {
		GTypeInfo mime_category_edit_dialog_info = {
			sizeof (MimeCategoryEditDialogClass),
			(GBaseInitFunc) mime_category_edit_dialog_base_init,
			NULL, /* GBaseFinalizeFunc */
			(GClassInitFunc) mime_category_edit_dialog_class_init,
			NULL, /* GClassFinalizeFunc */
			NULL, /* user-supplied data */
			sizeof (MimeCategoryEditDialog),
			0, /* n_preallocs */
			(GInstanceInitFunc) mime_category_edit_dialog_init,
			NULL
		};

		mime_category_edit_dialog_type = 
			g_type_register_static (G_TYPE_OBJECT, 
						"MimeCategoryEditDialog",
						&mime_category_edit_dialog_info, 0);
	}

	return mime_category_edit_dialog_type;
}

static void
mime_category_edit_dialog_init (MimeCategoryEditDialog *dialog, MimeCategoryEditDialogClass *class)
{
	GtkSizeGroup *size_group;

	dialog->p = g_new0 (MimeCategoryEditDialogPrivate, 1);
	dialog->p->dialog_xml = glade_xml_new
		(GNOMECC_DATA_DIR "/interfaces/file-types-properties.glade", "mime_category_edit_widget", NULL);

	dialog->p->model = NULL;
	dialog->p->info = NULL;

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	gtk_size_group_add_widget (size_group, WID ("default_action_label"));
	gtk_size_group_add_widget (size_group, WID ("program_label"));

	gtk_widget_set_sensitive (WID ("name_box"), FALSE);

	dialog->p->dialog_win = gtk_dialog_new_with_buttons
		(_("Edit file category"), NULL, -1,
		 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		 GTK_STOCK_OK,     GTK_RESPONSE_OK,
		 NULL);

	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog->p->dialog_win)->vbox), WID ("mime_category_edit_widget"), TRUE, TRUE, 0);

	g_signal_connect_swapped (G_OBJECT (WID ("default_action_select")), "changed", (GCallback) default_action_changed_cb, dialog);
	g_signal_connect_swapped (G_OBJECT (WID ("use_category_toggle")), "toggled",
				  (GCallback) use_category_toggled_cb, dialog);

	g_signal_connect_swapped (G_OBJECT (dialog->p->dialog_win), "response", (GCallback) response_cb, dialog);
}

static void
mime_category_edit_dialog_base_init (MimeCategoryEditDialogClass *class) 
{
}

static void
mime_category_edit_dialog_class_init (MimeCategoryEditDialogClass *class) 
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);

	object_class->dispose = mime_category_edit_dialog_dispose;
	object_class->finalize = mime_category_edit_dialog_finalize;
	object_class->set_property = mime_category_edit_dialog_set_prop;
	object_class->get_property = mime_category_edit_dialog_get_prop;

	g_object_class_install_property
		(object_class, PROP_MODEL,
		 g_param_spec_pointer ("model",
				      _("Model"),
				      _("GtkTreeModel that contains the category data"),
				      G_PARAM_READWRITE));
	g_object_class_install_property
		(object_class, PROP_INFO,
		 g_param_spec_pointer ("mime-cat-info",
				      _("MIME category info"),
				      _("Structure containing information on the MIME category"),
				      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	parent_class = G_OBJECT_CLASS
		(g_type_class_ref (G_TYPE_OBJECT));
}

static void
mime_category_edit_dialog_set_prop (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
	MimeCategoryEditDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_MIME_CATEGORY_EDIT_DIALOG (object));

	dialog = MIME_CATEGORY_EDIT_DIALOG (object);

	switch (prop_id) {
	case PROP_MODEL:
		dialog->p->model = g_value_get_pointer (value);

		if (dialog->p->info != NULL)
			fill_dialog (dialog);

		break;

	case PROP_INFO:
		dialog->p->info = g_value_get_pointer (value);

		if (dialog->p->model != NULL)
			fill_dialog (dialog);

		break;

	default:
		g_warning ("Bad property set");
		break;
	}
}

static void
mime_category_edit_dialog_get_prop (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) 
{
	MimeCategoryEditDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_MIME_CATEGORY_EDIT_DIALOG (object));

	dialog = MIME_CATEGORY_EDIT_DIALOG (object);

	switch (prop_id) {
	case PROP_MODEL:
		g_value_set_pointer (value, dialog->p->model);
		break;

	case PROP_INFO:
		g_value_set_pointer (value, dialog->p->info);
		break;

	default:
		g_warning ("Bad property get");
		break;
	}
}

static void
mime_category_edit_dialog_dispose (GObject *object) 
{
	MimeCategoryEditDialog *dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_MIME_CATEGORY_EDIT_DIALOG (object));

	dialog = MIME_CATEGORY_EDIT_DIALOG (object);

	if (dialog->p->dialog_xml != NULL) {
		g_object_unref (G_OBJECT (dialog->p->dialog_xml));
		dialog->p->dialog_xml = NULL;
	}

	if (dialog->p->dialog_win != NULL) {
		gtk_widget_destroy (GTK_WIDGET (dialog->p->dialog_win));
		dialog->p->dialog_win = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
mime_category_edit_dialog_finalize (GObject *object) 
{
	MimeCategoryEditDialog *mime_category_edit_dialog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_MIME_CATEGORY_EDIT_DIALOG (object));

	mime_category_edit_dialog = MIME_CATEGORY_EDIT_DIALOG (object);

	g_free (mime_category_edit_dialog->p);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

GObject *
mime_category_edit_dialog_new (GtkTreeModel *model, MimeCategoryInfo *info) 
{
	return g_object_new (mime_category_edit_dialog_get_type (),
			     "model", model,
			     "mime-cat-info", info,
			     NULL);
}

static void
fill_dialog (MimeCategoryEditDialog *dialog)
{
	mime_category_info_load_all (dialog->p->info);

	gtk_entry_set_text (GTK_ENTRY (WID ("name_entry")), dialog->p->info->description);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (WID ("use_category_toggle")), dialog->p->info->use_parent_category);

	if (dialog->p->info->entry.parent->type == MODEL_ENTRY_NONE)
		gtk_widget_set_sensitive (WID ("use_category_toggle"), FALSE);

	populate_application_list (dialog);

	gtk_widget_show_all (dialog->p->dialog_win);
}

/* FIXME: This should be factored with corresponding functions in mime-edit-dialog.c and service-edit-dialog.c */

static void
populate_application_list (MimeCategoryEditDialog *dialog) 
{
	GList                   *app_list, *tmp;
	GtkMenu                 *menu;
	GtkWidget               *menu_item;
	GtkOptionMenu           *app_select;
	GnomeVFSMimeApplication *app;
	int                      found_idx = -1, i;

	menu = GTK_MENU (gtk_menu_new ());

	app_list = mime_category_info_find_apps (dialog->p->info);

	for (tmp = app_list, i = 0; tmp != NULL; tmp = tmp->next, i++) {
		app = gnome_vfs_application_registry_get_mime_application (tmp->data);
		if (dialog->p->info->default_action != NULL &&
		    dialog->p->info->default_action->id != NULL &&
		    !strcmp (tmp->data, dialog->p->info->default_action->id))
			found_idx = i;

		menu_item = gtk_menu_item_new_with_label (app->name);

		/* Store copy of application in item; free when item destroyed. */
		g_object_set_data_full (G_OBJECT (menu_item),
					"app", app,
					(GDestroyNotify) gnome_vfs_mime_application_free);

		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
		gtk_widget_show (menu_item);
	}

	dialog->p->default_action_active = !(i == 0);
	dialog->p->custom_action = (found_idx < 0);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_menu_item_new_with_label (_("Custom")));

	if (found_idx < 0) {
		gboolean req_terminal = FALSE;
		found_idx = i;
		if (dialog->p->info->default_action != NULL) {
			if (dialog->p->info->default_action->command != NULL)
				gnome_file_entry_set_filename (GNOME_FILE_ENTRY (WID ("program_entry")),
							       dialog->p->info->default_action->command);
			req_terminal = dialog->p->info->default_action->requires_terminal;
		}

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (WID ("needs_terminal_toggle")),
					      req_terminal);
	} else {
		gtk_widget_set_sensitive (WID ("program_entry_box"), FALSE);
	}

	app_select = GTK_OPTION_MENU (WID ("default_action_select"));
	gtk_option_menu_set_menu (app_select, GTK_WIDGET (menu));
	gtk_option_menu_set_history (app_select, found_idx);

	g_list_free (app_list);

	update_sensitivity (dialog);
}

static void
update_subcategories (ModelEntry *entry, MimeCategoryInfo *category) 
{
	ModelEntry *tmp;

	switch (entry->type) {
	case MODEL_ENTRY_MIME_TYPE:
		if (MIME_TYPE_INFO (entry)->use_category) {
			gnome_vfs_mime_application_free (MIME_TYPE_INFO (entry)->default_action);

			if (category->default_action == NULL)
				MIME_TYPE_INFO (entry)->default_action = NULL;
			else
				MIME_TYPE_INFO (entry)->default_action = gnome_vfs_mime_application_copy (category->default_action);
		}

		break;

	case MODEL_ENTRY_CATEGORY:
		if (entry == MODEL_ENTRY (category) || MIME_CATEGORY_INFO (entry)->use_parent_category)
			for (tmp = entry->first_child; tmp != NULL; tmp = tmp->next)
				update_subcategories (tmp, category);
		break;

	default:
		break;
	}
}

static void
store_data (MimeCategoryEditDialog *dialog)
{
	mime_edit_dialog_get_app (dialog->p->dialog_xml,
		dialog->p->info->description,
		&(dialog->p->info->default_action));
	dialog->p->info->use_parent_category = gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (WID ("use_category_toggle")));
	model_entry_save (MODEL_ENTRY (dialog->p->info));
	update_subcategories (MODEL_ENTRY (dialog->p->info), dialog->p->info);
}

static void
default_action_changed_cb (MimeCategoryEditDialog *dialog)
{
	int id;
	GtkOptionMenu *option_menu;
	GtkMenuShell *menu;

	option_menu = GTK_OPTION_MENU (WID ("default_action_select"));
	menu = GTK_MENU_SHELL (gtk_option_menu_get_menu (option_menu));
	id = gtk_option_menu_get_history (option_menu);

	dialog->p->custom_action = (id == g_list_length (menu->children) - 1);
	update_sensitivity (dialog);
}

static void
use_category_toggled_cb (MimeCategoryEditDialog *dialog, GtkToggleButton *tb)
{
	dialog->p->use_cat_dfl = gtk_toggle_button_get_active (tb);
	update_sensitivity (dialog);
}

static void
response_cb (MimeCategoryEditDialog *dialog, gint response_id)
{
	if (response_id == GTK_RESPONSE_OK)
		store_data (dialog);

	g_object_unref (G_OBJECT (dialog));
}

static void
update_sensitivity (MimeCategoryEditDialog *dialog) 
{
	gtk_widget_set_sensitive (WID ("default_action_box"), dialog->p->default_action_active && !dialog->p->use_cat_dfl);
	gtk_widget_set_sensitive (WID ("program_entry_box"), dialog->p->custom_action && !dialog->p->use_cat_dfl);
	gtk_widget_set_sensitive (WID ("needs_terminal_toggle"), dialog->p->custom_action && !dialog->p->use_cat_dfl);
}
