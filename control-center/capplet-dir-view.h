/* -*- mode: c; style: linux -*- */

/* capplet-dir-view.h
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

#ifndef __CAPPLET_DIR_VIEW
#define __CAPPLET_DIR_VIEW

#include <gnome.h>

#include <gtkhtml/gtkhtml.h>
#include "capplet-dir.h"
#include "preferences.h"

#define CAPPLET_DIR_VIEW_TYPE          (capplet_dir_view_get_type ())
#define CAPPLET_DIR_VIEW(obj)          (GTK_CHECK_CAST (obj, CAPPLET_DIR_VIEW_TYPE, CappletDirView))
#define CAPPLET_DIR_VIEW_CLASS(klass)  (GTK_CHECK_CLASS_CAST (klass, CAPPLET_DIR_VIEW_TYPE, CappletDirViewClass))
#define IS_CAPPLET_DIR_VIEW(obj)       (GTK_CHECK_TYPE (obj, CAPPLET_DIR_VIEW_TYPE))
#define CAPPLET_DIR_VIEW_W(obj)        (CAPPLET_DIR_VIEW (obj)->app)

typedef struct _CappletDirViewClass CappletDirViewClass;
typedef struct _CappletDirViewImpl  CappletDirViewImpl;

typedef void (*CDVFunc) (CappletDirView *);

struct _CappletDirViewImpl
{
	/* remove entries from view */
	CDVFunc clear;

	/* clean up (destroy widgets) */
	CDVFunc clean;

	CDVFunc populate;

	GtkWidget *(*create) (CappletDirView *);
};
	

struct _CappletDirView 
{
	GtkObject object;
	GnomeApp *app;

	CappletDir *capplet_dir;
	CappletDirEntry *selected;

	CappletDirViewImpl *impl;

	GtkWidget *view;
	gpointer view_data;

	gboolean destroyed;
	CappletDirViewLayout layout;

	GtkWidget *up_button;
	GtkWidget *parents_option;

	GtkWidget *html_toggle;
	GtkWidget *list_toggle;
	GtkWidget *tree_toggle;

	GtkWidget *html_menu;
	GtkWidget *list_menu;
	GtkWidget *tree_menu;

	GtkWidget *rootm_button;
	GtkWidget *rootm_locked;
	GtkWidget *rootm_unlocked;

	gboolean changing_layout;
};

struct _CappletDirViewClass 
{
	GtkObjectClass parent;
};

guint capplet_dir_view_get_type (void);

CappletDirView *capplet_dir_view_new (void);
void capplet_dir_view_destroy   (CappletDirView *view);

void capplet_dir_view_load_dir  (CappletDirView *view, CappletDir *dir);
void capplet_dir_view_show      (CappletDirView *view);

void capplet_dir_views_set_authenticated (gboolean authed);

void gnomecc_init (void);

#endif /* __CAPPLET_DIR_VIEW */
