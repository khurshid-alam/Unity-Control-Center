/* -*- mode: c; style: linux -*- */

/* prefs-widget-mdi.h
 * Copyright (C) 2000 Helix Code, Inc.
 *
 * Written by Bradford Hovinen <hovinen@helixcode.com>
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

#ifndef __PREFS_WIDGET_MDI_H
#define __PREFS_WIDGET_MDI_H

#include <gtk/gtk.h>
#include <glade/glade.h>

#include "preferences.h"
#include "prefs-widget.h"

#define PREFS_WIDGET_MDI(obj)          GTK_CHECK_CAST (obj, prefs_widget_mdi_get_type (), PrefsWidgetMDI)
#define PREFS_WIDGET_MDI_CLASS(klass)  GTK_CHECK_CLASS_CAST (klass, prefs_widget_mdi_get_type (), PrefsWidgetMDIClass)
#define IS_PREFS_WIDGET_MDI(obj)       GTK_CHECK_TYPE (obj, prefs_widget_mdi_get_type ())

typedef struct _PrefsWidgetMDI PrefsWidgetMDI;
typedef struct _PrefsWidgetMDIClass PrefsWidgetMDIClass;

struct _PrefsWidgetMDI 
{
	PrefsWidget prefs_widget;
};

struct _PrefsWidgetMDIClass 
{
	PrefsWidgetClass parent_class;
};

guint prefs_widget_mdi_get_type (void);

GtkWidget *prefs_widget_mdi_new         (Preferences *prefs);

#endif /* __PREFS_WIDGET_MDI_H */
