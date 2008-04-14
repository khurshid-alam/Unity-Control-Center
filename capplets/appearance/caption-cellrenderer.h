/*
 * caption-cellrenderer was based on wp-cellrenderer
 *
 * Copyright (C) 2007, 2008 The GNOME Foundation
 * Written by Denis Washington <denisw@svn.gnome.org>
 *            Thomas Wood <thos@gnome.org>
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _CAPTION_CELL_RENDERER_H
#define _CAPTION_CELL_RENDERER_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _CellRendererCaption       CellRendererCaption;
typedef struct _CellRendererCaptionClass  CellRendererCaptionClass;

struct _CellRendererCaption
{
	GtkCellRendererText parent;
};

struct _CellRendererCaptionClass
{
	GtkCellRendererTextClass parent;
};

GtkCellRenderer *cell_renderer_caption_new (void);

G_END_DECLS

#endif
