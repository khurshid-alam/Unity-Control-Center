/* -*- mode: c; style: linux -*- */

/* mime-category-edit-dialog.h
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

#ifndef __MIME_CATEGORY_EDIT_DIALOG_H
#define __MIME_CATEGORY_EDIT_DIALOG_H

#include <gnome.h>


G_BEGIN_DECLS

#define MIME_CATEGORY_EDIT_DIALOG(obj)          G_TYPE_CHECK_INSTANCE_CAST (obj, mime_category_edit_dialog_get_type (), MimeCategoryEditDialog)
#define MIME_CATEGORY_EDIT_DIALOG_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST (klass, mime_category_edit_dialog_get_type (), MimeCategoryEditDialogClass)
#define IS_MIME_CATEGORY_EDIT_DIALOG(obj)       G_TYPE_CHECK_INSTANCE_TYPE (obj, mime_category_edit_dialog_get_type ())

typedef struct _MimeCategoryEditDialog MimeCategoryEditDialog;
typedef struct _MimeCategoryEditDialogClass MimeCategoryEditDialogClass;
typedef struct _MimeCategoryEditDialogPrivate MimeCategoryEditDialogPrivate;

struct _MimeCategoryEditDialog 
{
	GObject parent;

	MimeCategoryEditDialogPrivate *p;
};

struct _MimeCategoryEditDialogClass 
{
	GObjectClass g_object_class;
};

GType mime_category_edit_dialog_get_type (void);

GObject *mime_category_edit_dialog_new   (GtkTreeModel     *model,
					  MimeCategoryInfo *info);

G_END_DECLS

#endif /* __MIME_CATEGORY_EDIT_DIALOG_H */
