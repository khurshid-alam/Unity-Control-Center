/* -*- mode: c; style: linux -*- */

/* applier.c
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

#include <gnome.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkprivate.h>
#include <gdk-pixbuf/gdk-pixbuf-xlib.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <unistd.h>
#include <bonobo.h>

#include "applier.h"

#define MONITOR_CONTENTS_X 20
#define MONITOR_CONTENTS_Y 10
#define MONITOR_CONTENTS_WIDTH 157
#define MONITOR_CONTENTS_HEIGHT 111

static gboolean    gdk_pixbuf_xlib_inited = FALSE;

enum {
	ARG_0,
	ARG_TYPE
};

struct _ApplierPrivate 
{
	GtkWidget          *preview_widget;        /* The widget for previewing
						    * -- this is not used for
						    * actual rendering; it is
						    * returned if requested */
	Preferences        *last_prefs;            /* A cache of the last
						    * preferences structure to
						    * be applied */

	GdkPixbuf          *wallpaper_pixbuf;      /* The "raw" wallpaper pixbuf */

	gboolean            nautilus_running;      /* TRUE iff nautilus is
						    * running, in which case we
						    * block the renderer */

	ApplierType         type;                  /* Whether we render to the
						    * root or the preview */

	/* Where on the pixmap we should render the background image. Should
         * have origin 0,0 and width and height equal to the desktop size if we
         * are rendering to the desktop. The area to which we render the pixbuf
         * will be smaller if we are rendering a centered image smaller than the
         * screen or scaling and keeping aspect ratio and the background color
         * is solid. */
	GdkRectangle        render_geom;

	/* Where to render the pixbuf, relative to the pixmap. This will be the
	 * same as render_geom above if we have no solid color area to worry
	 * about. By convention, a negative value means that the pixbuf is
	 * larger than the pixmap, so the region should be fetched from a subset
	 * of the pixbuf. */
	GdkRectangle        pixbuf_render_geom;

	/* Where to fetch the data from the pixbuf. We use this in case we are
	 * rendering a centered image that is larger than the size of the
	 * desktop. Otherwise it is (0,0) */
	GdkPoint            pixbuf_xlate;

	/* Geometry of the pixbuf used to render the gradient. If the wallpaper
	 * is not enabled, we use the following optimization: On one dimension,
	 * this should be equal to the dimension of render_geom, while on the
	 * other dimension it should be the constant 32. This avoids wasting
	 * memory with rendundant data. */
	GdkPoint            grad_geom;

	GdkPixbuf          *pixbuf;            /* "working" pixbuf - All data
						* are rendered onto this for
						* display */
	GdkPixmap          *pixmap;            /* Pixmap onto which we dump the
						* pixbuf above when we are ready
						* to render to the screen */
	gboolean            pixmap_is_set;     /* TRUE iff the pixmap above
						* has been set as the root
						* pixmap */
};

static GtkObjectClass *parent_class;

static void applier_init             (Applier *prefs);
static void applier_class_init       (ApplierClass *class);

static void applier_set_arg          (GtkObject *object, 
				      GtkArg *arg, 
				      guint arg_id);
static void applier_get_arg          (GtkObject *object, 
				      GtkArg *arg, 
				      guint arg_id);

static void applier_destroy          (GtkObject *object);
static void applier_finalize         (GtkObject *object);

static void run_render_pipeline      (Applier           *applier, 
				      const Preferences *prefs);
static void draw_disabled_message    (GtkWidget         *widget);

static void render_background        (Applier           *applier,
				      const Preferences *prefs);
static void render_wallpaper         (Applier           *applier,
				      const Preferences *prefs);
static void render_to_screen         (Applier           *applier,
				      const Preferences *prefs);
static void create_pixmap            (Applier           *applier,
				      const Preferences *prefs);
static void get_geometry             (wallpaper_type_t   wallpaper_type,
				      GdkPixbuf         *pixbuf,
				      GdkRectangle      *field_geom,
				      GdkRectangle      *virtual_geom,
				      GdkRectangle      *dest_geom,
				      GdkRectangle      *src_geom);

static GdkPixbuf *place_pixbuf       (GdkPixbuf         *dest_pixbuf,
				      GdkPixbuf         *src_pixbuf,
				      GdkRectangle      *dest_geom,
				      GdkRectangle      *src_geom,
				      guint              alpha,
				      GdkColor          *bg_color);
static GdkPixbuf *tile_pixbuf        (GdkPixbuf         *dest_pixbuf,
				      GdkPixbuf         *src_pixbuf,
				      GdkRectangle      *field_geom,
				      guint              alpha,
				      GdkColor          *bg_color);
static void fill_gradient            (GdkPixbuf         *pixbuf,
				      GdkColor          *c1,
				      GdkColor          *c2,
				      orientation_t      orientation);

static gboolean need_wallpaper_load_p  (const Applier     *applier,
					const Preferences *prefs);
static gboolean need_root_pixmap_p     (const Applier     *applier,
					const Preferences *prefs);
static gboolean wallpaper_full_cover_p (const Applier     *applier,
					const Preferences *prefs);
static gboolean render_small_pixmap_p  (const Preferences *prefs);

static GdkPixmap *make_root_pixmap   (gint               width,
				      gint               height);
static void set_root_pixmap          (GdkPixmap         *pixmap);

static gboolean is_nautilus_running  (void);

guint
applier_get_type (void)
{
	static guint applier_type = 0;

	if (!applier_type) {
		GtkTypeInfo applier_info = {
			"Applier",
			sizeof (Applier),
			sizeof (ApplierClass),
			(GtkClassInitFunc) applier_class_init,
			(GtkObjectInitFunc) applier_init,
			(GtkArgSetFunc) NULL,
			(GtkArgGetFunc) NULL
		};

		applier_type = 
			gtk_type_unique (gtk_object_get_type (), 
					 &applier_info);
	}

	return applier_type;
}

static void
applier_init (Applier *applier)
{
	applier->p                   = g_new0 (ApplierPrivate, 1);
	applier->p->last_prefs       = NULL;
	applier->p->pixbuf           = NULL;
	applier->p->wallpaper_pixbuf = NULL;
	applier->p->nautilus_running = is_nautilus_running ();
}

static void
applier_class_init (ApplierClass *class) 
{
	GtkObjectClass *object_class;
	GdkVisual *visual;

	gtk_object_add_arg_type ("Applier::type",
				 GTK_TYPE_POINTER,
				 GTK_ARG_READWRITE | GTK_ARG_CONSTRUCT_ONLY,
				 ARG_TYPE);

	object_class = GTK_OBJECT_CLASS (class);
	object_class->destroy = applier_destroy;
	object_class->finalize = applier_finalize;
	object_class->set_arg = applier_set_arg;
	object_class->get_arg = applier_get_arg;

	parent_class = 
		GTK_OBJECT_CLASS (gtk_type_class (gtk_object_get_type ()));

	if (!gdk_pixbuf_xlib_inited) {
		gdk_pixbuf_xlib_inited = TRUE;

		visual = gdk_window_get_visual (GDK_ROOT_PARENT ());

		gdk_pixbuf_xlib_init_with_depth (GDK_DISPLAY (), gdk_screen, visual->depth);
	}
}

static void
applier_set_arg (GtkObject *object, GtkArg *arg, guint arg_id) 
{
	Applier *applier;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_APPLIER (object));

	applier = APPLIER (object);

	switch (arg_id) {
	case ARG_TYPE:
		applier->p->type = GTK_VALUE_INT (*arg);

		switch (applier->p->type) {
		case APPLIER_ROOT:
			applier->p->render_geom.x = 0;
			applier->p->render_geom.y = 0;
			applier->p->render_geom.width = gdk_screen_width ();
			applier->p->render_geom.height = gdk_screen_height ();
			applier->p->pixmap = NULL;
			applier->p->pixmap_is_set = FALSE;
			break;

		case APPLIER_PREVIEW:
			applier->p->render_geom.x = MONITOR_CONTENTS_X;
			applier->p->render_geom.y = MONITOR_CONTENTS_Y;
			applier->p->render_geom.width = MONITOR_CONTENTS_WIDTH;
			applier->p->render_geom.height = MONITOR_CONTENTS_HEIGHT;
			break;

		default:
			g_critical ("Bad applier type: %d", applier->p->type);
			break;
		}

		break;

	default:
		g_warning ("Bad argument set");
		break;
	}
}

static void
applier_get_arg (GtkObject *object, GtkArg *arg, guint arg_id) 
{
	Applier *applier;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_APPLIER (object));

	applier = APPLIER (object);

	switch (arg_id) {
	case ARG_TYPE:
		GTK_VALUE_INT (*arg) = applier->p->type;
		break;

	default:
		g_warning ("Bad argument get");
		break;
	}
}

static void
applier_destroy (GtkObject *object) 
{
	Applier *applier;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_APPLIER (object));

	applier = APPLIER (object);

	g_assert (applier->p->pixbuf == NULL);

	if (applier->p->last_prefs != NULL)
		gtk_object_destroy (GTK_OBJECT (applier->p->last_prefs));

	if (applier->p->wallpaper_pixbuf != NULL)
		gdk_pixbuf_unref (applier->p->wallpaper_pixbuf);

	parent_class->destroy (object);
}

static void
applier_finalize (GtkObject *object) 
{
	Applier *applier;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_APPLIER (object));

	applier = APPLIER (object);

	g_free (applier->p);

	parent_class->finalize (object);
}

GtkObject *
applier_new (ApplierType type) 
{
	GtkObject *object;

	object = gtk_object_new (applier_get_type (),
				 "type", type,
				 NULL);

	return object;
}

void
applier_apply_prefs (Applier           *applier, 
		     const Preferences *prefs)
{
	g_return_if_fail (applier != NULL);
	g_return_if_fail (IS_APPLIER (applier));

	if (applier->p->type == APPLIER_ROOT && applier->p->nautilus_running)
		set_root_pixmap ((GdkPixmap *) -1);

	if (!prefs->enabled) {
		if (applier->p->type == APPLIER_PREVIEW)
			draw_disabled_message (applier_get_preview_widget (applier));
		return;
	}

	if (need_wallpaper_load_p (applier, prefs)) {
		if (applier->p->wallpaper_pixbuf != NULL)
			gdk_pixbuf_unref (applier->p->wallpaper_pixbuf);

		applier->p->wallpaper_pixbuf = NULL;

		if (prefs->wallpaper_enabled) {
			g_return_if_fail (prefs->wallpaper_filename != NULL);

			applier->p->wallpaper_pixbuf = 
				gdk_pixbuf_new_from_file (prefs->wallpaper_filename);

			if (applier->p->wallpaper_pixbuf == NULL)
				g_warning (_("Could not load pixbuf \"%s\"; disabling wallpaper."),
					   prefs->wallpaper_filename);
		}
	}

	if (applier->p->type == APPLIER_ROOT)
		nice (20);

	run_render_pipeline (applier, prefs);

	if (applier->p->last_prefs != NULL)
		gtk_object_destroy (GTK_OBJECT (applier->p->last_prefs));

	applier->p->last_prefs = PREFERENCES (preferences_clone (prefs));

	if (applier->p->type == APPLIER_PREVIEW && applier->p->preview_widget != NULL)
		gtk_widget_queue_draw (applier->p->preview_widget);
	else if (applier->p->type == APPLIER_ROOT)
		preferences_save (prefs);
}

gboolean
applier_render_color_p (const Applier *applier, const Preferences *prefs) 
{
	g_return_val_if_fail (applier != NULL, FALSE);
	g_return_val_if_fail (IS_APPLIER (applier), FALSE);
	g_return_val_if_fail (prefs != NULL, FALSE);
	g_return_val_if_fail (IS_PREFERENCES (prefs), FALSE);

	return prefs->enabled && !wallpaper_full_cover_p (applier, prefs);
}

GtkWidget *
applier_get_preview_widget (Applier *applier) 
{
	GdkPixbuf *pixbuf;
	GdkPixmap *pixmap;
	GdkBitmap *mask;
	GdkVisual *visual;
	GdkColormap *colormap;
	Pixmap xpixmap, xmask;
	gchar *filename;
	GdkGC *gc;

	g_return_val_if_fail (applier != NULL, NULL);
	g_return_val_if_fail (IS_APPLIER (applier), NULL);

	if (applier->p->type != APPLIER_PREVIEW)
		return NULL;

	if (applier->p->preview_widget != NULL)
		return applier->p->preview_widget;

	filename = gnome_pixmap_file ("monitor.png");
	visual = gdk_window_get_visual (GDK_ROOT_PARENT ());
	colormap = gdk_window_get_colormap (GDK_ROOT_PARENT ());

	gtk_widget_push_visual (visual);
	gtk_widget_push_colormap (colormap);

	pixbuf = gdk_pixbuf_new_from_file (filename);

	if (pixbuf == NULL) return NULL;

	gdk_pixbuf_xlib_render_pixmap_and_mask (pixbuf, &xpixmap, &xmask, 1);

	if (xpixmap) {
		pixmap = gdk_pixmap_new (GDK_ROOT_PARENT (),
					 gdk_pixbuf_get_width (pixbuf),
					 gdk_pixbuf_get_height (pixbuf),
					 visual->depth);

		gc = gdk_gc_new (GDK_ROOT_PARENT ());

		XCopyArea (GDK_DISPLAY (), xpixmap,
			   GDK_WINDOW_XWINDOW (pixmap), 
			   GDK_GC_XGC (gc), 0, 0,
			   gdk_pixbuf_get_width (pixbuf),
			   gdk_pixbuf_get_height (pixbuf),
			   0, 0);

		XFreePixmap (GDK_DISPLAY (), xpixmap);

		gdk_gc_destroy (gc);
	} else {
		pixmap = NULL;
	}

	if (xmask) {
		mask = gdk_pixmap_new (GDK_ROOT_PARENT (),
				       gdk_pixbuf_get_width (pixbuf),
				       gdk_pixbuf_get_height (pixbuf),
				       1);

		gc = gdk_gc_new (mask);

		XCopyArea (GDK_DISPLAY (), xmask, 
			   GDK_WINDOW_XWINDOW (mask), 
			   GDK_GC_XGC (gc), 0, 0,
			   gdk_pixbuf_get_width (pixbuf),
			   gdk_pixbuf_get_height (pixbuf),
			   0, 0);

		XFreePixmap (GDK_DISPLAY (), xmask);

		gdk_gc_destroy (gc);
	} else {
		mask = NULL;
	}

	applier->p->preview_widget = gtk_pixmap_new (pixmap, mask);
	gtk_widget_show (applier->p->preview_widget);
	gdk_pixbuf_unref (pixbuf);
	g_free (filename);

	gtk_widget_pop_visual ();
	gtk_widget_pop_colormap ();

	return applier->p->preview_widget;
}

GdkPixbuf *
applier_get_wallpaper_pixbuf (Applier *applier)
{
	g_return_val_if_fail (applier != NULL, NULL);
	g_return_val_if_fail (IS_APPLIER (applier), NULL);

	return applier->p->wallpaper_pixbuf;
}

static void
draw_disabled_message (GtkWidget *widget)
{
	GdkPixmap  *pixmap;
	GdkColor    color;
	GdkFont    *font;
	GdkGC      *gc;
	gint        x, y, w, h;
	gint        height, width;
	const char *disabled_string = _("Disabled");

	g_return_if_fail (widget != NULL);
	g_return_if_fail (GTK_IS_PIXMAP (widget));

	w = MONITOR_CONTENTS_WIDTH;
	h = MONITOR_CONTENTS_HEIGHT;
	x = MONITOR_CONTENTS_X;
	y = MONITOR_CONTENTS_Y;

	if (!GTK_WIDGET_REALIZED (widget))
		gtk_widget_realize (widget);

	pixmap = GTK_PIXMAP (widget)->pixmap;

	gc = gdk_gc_new (widget->window);

	gdk_color_black (gtk_widget_get_colormap (widget), &color);
	gdk_gc_set_foreground (gc, &color);
	gdk_draw_rectangle (pixmap, gc, TRUE, x, y, w, h);

	font = widget->style->font;
	width = gdk_string_width (font, disabled_string);
	height = gdk_string_height (font, disabled_string);

	gdk_color_white (gtk_widget_get_colormap (widget), &color);
	gdk_gc_set_foreground (gc, &color);
	gdk_draw_string (pixmap, font, gc, 
			 x + (w - width) / 2, y + (h - height) / 2 +
			 height / 2,
			 disabled_string);

	gdk_gc_unref (gc);
	gtk_widget_queue_draw (widget);
}

static void
run_render_pipeline (Applier *applier, const Preferences *prefs)
{
	g_return_if_fail (applier != NULL);
	g_return_if_fail (IS_APPLIER (applier));
	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_PREFERENCES (prefs));

	g_assert (applier->p->pixbuf == NULL);

	/* Initialize applier->p->render_geom */
	applier->p->pixbuf_render_geom.x = applier->p->render_geom.x;
	applier->p->pixbuf_render_geom.y = applier->p->render_geom.y;
	applier->p->pixbuf_render_geom.width = applier->p->render_geom.width;
	applier->p->pixbuf_render_geom.height = applier->p->render_geom.height;
	applier->p->pixbuf_xlate.x = 0;
	applier->p->pixbuf_xlate.y = 0;

	render_background (applier, prefs);

	if (need_root_pixmap_p (applier, prefs))
		create_pixmap (applier, prefs);

	render_wallpaper (applier, prefs);
	render_to_screen (applier, prefs);

	if (applier->p->pixbuf != NULL) {
		gdk_pixbuf_unref (applier->p->pixbuf);
		applier->p->pixbuf = NULL;
	}
}

/* Create the gradient image if necessary and put it into a fresh pixbuf
 *
 * Preconditions:
 *   1. prefs is valid
 *   2. The old applier->p->pixbuf, if it existed, has been destroyed
 *
 * Postconditions (assuming gradient is enabled):
 *   1. applier->p->pixbuf contains a newly rendered gradient
 */

static void
render_background (Applier *applier, const Preferences *prefs) 
{
	g_return_if_fail (applier != NULL);
	g_return_if_fail (IS_APPLIER (applier));
	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_PREFERENCES (prefs));

	if (prefs->gradient_enabled && !wallpaper_full_cover_p (applier, prefs)) {
		applier->p->grad_geom.x = applier->p->render_geom.width;
		applier->p->grad_geom.y = applier->p->render_geom.height;

		if (applier->p->type == APPLIER_ROOT && !prefs->wallpaper_enabled) {
			if (prefs->orientation == ORIENTATION_HORIZ)
				applier->p->grad_geom.y = 32;
			else
				applier->p->grad_geom.x = 32;
		}

		applier->p->pixbuf = 
			gdk_pixbuf_new (GDK_COLORSPACE_RGB, 
					FALSE, 8, 
					applier->p->grad_geom.x, 
					applier->p->grad_geom.y);

		fill_gradient (applier->p->pixbuf,
			       prefs->color1, prefs->color2, 
			       prefs->orientation);

		applier->p->pixbuf_render_geom.width = applier->p->grad_geom.x;
		applier->p->pixbuf_render_geom.height = applier->p->grad_geom.y;
	}
}

/* Render the wallpaper onto the pixbuf-in-progress.
 *
 * Preconditions:
 *   1. The wallpaper pixbuf has been loaded and is in
 *      applier->p->wallpaper_pixbuf.
 *   2. The structure applier->p->render_geom is filled out properly as
 *      described in the documentation above (this should be invariant).
 *   3. The various fields in prefs are valid
 *
 * Postconditions (assuming wallpaper is enabled):
 *   1. applier->p->pixbuf contains the pixbuf-in-progress with the wallpaper
 *      correctly rendered.
 *   2. applier->p->pixbuf_render_geom has been modified, if necessary,
 *      according to the requirements of the wallpaper; it should be set by
 *      default to be the same as applier->p->render_geom.
 */

static void
render_wallpaper (Applier *applier, const Preferences *prefs) 
{
	GdkRectangle  src_geom;
	GdkRectangle  dest_geom;
	GdkRectangle  virtual_geom;
	GdkPixbuf    *prescaled_pixbuf = NULL;
	guint         alpha;
	gint          tmp1, tmp2;
	gint          pwidth, pheight;

	g_return_if_fail (applier != NULL);
	g_return_if_fail (IS_APPLIER (applier));
	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_PREFERENCES (prefs));

	if (prefs->wallpaper_enabled) {
		if (applier->p->wallpaper_pixbuf == NULL)
			return;

		gdk_window_get_size (GDK_ROOT_PARENT (), &tmp1, &tmp2);
		virtual_geom.x = virtual_geom.y = 0;
		virtual_geom.width = tmp1;
		virtual_geom.height = tmp2;

		pwidth = gdk_pixbuf_get_width (applier->p->wallpaper_pixbuf);
		pheight = gdk_pixbuf_get_height (applier->p->wallpaper_pixbuf);

		get_geometry (prefs->wallpaper_type,
			      applier->p->wallpaper_pixbuf,
			      &(applier->p->render_geom),
			      &virtual_geom, &dest_geom, &src_geom);

		/* Modify applier->p->pixbuf_render_geom if necessary */
		if (applier->p->pixbuf == NULL) {   /* This means we didn't render a gradient */
			applier->p->pixbuf_render_geom.x = dest_geom.x + applier->p->render_geom.x;
			applier->p->pixbuf_render_geom.y = dest_geom.y + applier->p->render_geom.y;
			applier->p->pixbuf_render_geom.width = dest_geom.width;
			applier->p->pixbuf_render_geom.height = dest_geom.height;
		}

		if (prefs->wallpaper_type == WPTYPE_TILED) {
			if (dest_geom.width != pwidth || dest_geom.height != pheight) {
				prescaled_pixbuf = gdk_pixbuf_scale_simple
					(applier->p->wallpaper_pixbuf,
					 pwidth * applier->p->render_geom.width / virtual_geom.width,
					 pheight * applier->p->render_geom.height / virtual_geom.height,
					 GDK_INTERP_BILINEAR);
			} else {
				prescaled_pixbuf = applier->p->wallpaper_pixbuf;
				gdk_pixbuf_ref (prescaled_pixbuf);
			}
		}

		if (prefs->adjust_opacity) {
			alpha = 2.56 * prefs->opacity;
			alpha = alpha * alpha / 256;
			alpha = CLAMP (alpha, 0, 255);
		} else {
			alpha = 255;
		}

		if (prefs->wallpaper_type == WPTYPE_TILED)
			applier->p->pixbuf = tile_pixbuf (applier->p->pixbuf,
							  prescaled_pixbuf,
							  &(applier->p->render_geom),
							  alpha, prefs->color1);
		else
			applier->p->pixbuf = place_pixbuf (applier->p->pixbuf,
							   applier->p->wallpaper_pixbuf,
							   &dest_geom, &src_geom,
							   alpha, prefs->color1);

		if (applier->p->pixbuf == applier->p->wallpaper_pixbuf) {
			applier->p->pixbuf_xlate.x = src_geom.x;
			applier->p->pixbuf_xlate.y = src_geom.y;
		}

		if (prescaled_pixbuf != NULL)
			gdk_pixbuf_unref (prescaled_pixbuf);
	}
}

/* Take whatever we have rendered and transfer it to the display.
 *
 * Preconditions:
 *   1. We have already rendered the gradient and wallpaper, and
 *      applier->p->pixbuf is a valid GdkPixbuf containing that rendered data.
 *   2. The structure applier->p->pixbuf_render_geom contains the coordonites on
 *      the destination visual to which we should render the contents of
 *      applier->p->pixbuf
 *   3. The structure applier->p->render_geom contains the total area that the
 *      background should cover (i.e. the whole desktop if we are rendering to
 *      the root window, or the region inside the monitor if we are rendering to
 *      the preview).
 *   4. The strucutre prefs->color1 contains the background color to be used on
 *      areas not covered by the above pixbuf.
 */

static void
render_to_screen (Applier *applier, const Preferences *prefs) 
{
	GdkGC *gc;

	g_return_if_fail (applier != NULL);
	g_return_if_fail (IS_APPLIER (applier));
	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_PREFERENCES (prefs));

	gc = gdk_gc_new (GDK_ROOT_PARENT ());

	if (applier->p->pixbuf != NULL) {
		if (applier->p->pixbuf_render_geom.x != 0 ||
		    applier->p->pixbuf_render_geom.y != 0 ||
		    applier->p->pixbuf_render_geom.width != applier->p->render_geom.width ||
		    applier->p->pixbuf_render_geom.height != applier->p->render_geom.height)
		{
			gdk_color_alloc (gdk_window_get_colormap (GDK_ROOT_PARENT ()), prefs->color1);
			gdk_gc_set_foreground (gc, prefs->color1);
			gdk_draw_rectangle (applier->p->pixmap, gc, TRUE,
					    applier->p->render_geom.x,
					    applier->p->render_geom.y, 
					    applier->p->render_geom.width,
					    applier->p->render_geom.height);
		}

		gdk_pixbuf_render_to_drawable
			(applier->p->pixbuf,
			 applier->p->pixmap, gc,
			 applier->p->pixbuf_xlate.x,
			 applier->p->pixbuf_xlate.y,
			 applier->p->pixbuf_render_geom.x,
			 applier->p->pixbuf_render_geom.y,
			 applier->p->pixbuf_render_geom.width,
			 applier->p->pixbuf_render_geom.height,
			 GDK_RGB_DITHER_MAX, 0, 0);
	} else {
		if (applier->p->type == APPLIER_ROOT) {
			gdk_color_alloc (gdk_window_get_colormap (GDK_ROOT_PARENT()), prefs->color1);
			gdk_window_set_background (GDK_ROOT_PARENT (), prefs->color1);
			gdk_window_clear (GDK_ROOT_PARENT ());
		}
		else if (applier->p->type == APPLIER_PREVIEW) {
			gdk_color_alloc (gdk_window_get_colormap (applier->p->preview_widget->window), prefs->color1);
			gdk_gc_set_foreground (gc, prefs->color1);
			gdk_draw_rectangle (applier->p->pixmap, gc, TRUE,
					    applier->p->render_geom.x,
					    applier->p->render_geom.y, 
					    applier->p->render_geom.width,
					    applier->p->render_geom.height);
		}
	}

	if (applier->p->type == APPLIER_ROOT && !applier->p->pixmap_is_set &&
	    (prefs->wallpaper_enabled || prefs->gradient_enabled))
		set_root_pixmap (applier->p->pixmap);
	else if (applier->p->type == APPLIER_ROOT && !applier->p->pixmap_is_set)
		set_root_pixmap (NULL);

	gdk_gc_destroy (gc);
}

/* Create a pixmap that will replace the current root pixmap. This function has
 * no effect if the applier is for the preview window
 */

static void
create_pixmap (Applier *applier, const Preferences *prefs) 
{
	gint width, height;

	g_return_if_fail (applier != NULL);
	g_return_if_fail (IS_APPLIER (applier));
	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_PREFERENCES (prefs));

	switch (applier->p->type) {
	case APPLIER_ROOT:
		if (prefs->gradient_enabled && !prefs->wallpaper_enabled) {
			width = applier->p->grad_geom.x;
			height = applier->p->grad_geom.y;
		} else {
			width = applier->p->render_geom.width;
			height = applier->p->render_geom.height;
		}

		applier->p->pixmap = make_root_pixmap (width, height);
		applier->p->pixmap_is_set = FALSE;
		break;

	case APPLIER_PREVIEW:
		applier_get_preview_widget (applier);

		if (!GTK_WIDGET_REALIZED (applier->p->preview_widget))
			gtk_widget_realize (applier->p->preview_widget);

		applier->p->pixmap = GTK_PIXMAP (applier->p->preview_widget)->pixmap;
		applier->p->pixmap_is_set = TRUE;
		break;
	}
}

/* Compute geometry information based on the wallpaper type. In particular,
 * determine where on the destination visual the wallpaper should be rendered
 * and where the data from the source pixbuf the image should be fetched.
 */

static void
get_geometry (wallpaper_type_t  wallpaper_type,
	      GdkPixbuf        *pixbuf,
	      GdkRectangle     *field_geom,
	      GdkRectangle     *virtual_geom,
	      GdkRectangle     *dest_geom,
	      GdkRectangle     *src_geom)
{
	gdouble asp, xfactor, yfactor;
	gint pwidth, pheight;
	gint st = 0;

	if (field_geom->width != virtual_geom->width)
		xfactor = (gdouble) field_geom->width / (gdouble) virtual_geom->width;
	else
		xfactor = 1.0;

	if (field_geom->height != virtual_geom->height)
		yfactor = (gdouble) field_geom->height / (gdouble) virtual_geom->height;
	else
		yfactor = 1.0;

	pwidth = gdk_pixbuf_get_width (pixbuf);
	pheight = gdk_pixbuf_get_height (pixbuf);

	switch (wallpaper_type) {
	case WPTYPE_TILED:
		src_geom->x = src_geom->y = 0;
		dest_geom->x = dest_geom->y = 0;

		src_geom->width = pwidth;
		src_geom->height = pheight;

		dest_geom->width = field_geom->width;
		dest_geom->height = field_geom->height;

		break;

	case WPTYPE_CENTERED:
		if (virtual_geom->width < pwidth) {
			src_geom->width = virtual_geom->width;
			src_geom->x = (pwidth - virtual_geom->width) / 2;
			dest_geom->width = field_geom->width;
			dest_geom->x = 0;
		} else {
			src_geom->width = pwidth;
			src_geom->x = 0;
			dest_geom->width = MIN ((gdouble) src_geom->width * xfactor, field_geom->width);
			dest_geom->x = (field_geom->width - dest_geom->width) / 2;
		}

		if (virtual_geom->height < pheight) {
			src_geom->height = virtual_geom->height;
			src_geom->y = (pheight - virtual_geom->height) / 2;
			dest_geom->height = field_geom->height;
			dest_geom->y = 0;
		} else {
			src_geom->height = pheight;
			src_geom->y = 0;
			dest_geom->height = MIN ((gdouble) src_geom->height * yfactor, field_geom->height);
			dest_geom->y = (field_geom->height - dest_geom->height) / 2;
		}

		break;

	case WPTYPE_SCALED:
		asp = (gdouble) pwidth / (gdouble) virtual_geom->width;

		if (asp < (gdouble) pheight / virtual_geom->height) {
			asp = (gdouble) pheight / (gdouble) virtual_geom->height;
			st = 1;
		}

		if (st) {
			dest_geom->width = pwidth / asp * xfactor;
			dest_geom->height = field_geom->height;
			dest_geom->x = (field_geom->width - dest_geom->width) / 2;
			dest_geom->y = 0;
		} else {
			dest_geom->height = pheight / asp * yfactor;
			dest_geom->width = field_geom->width;
			dest_geom->x = 0;
			dest_geom->y = (field_geom->height - dest_geom->height) / 2;
		}

		src_geom->x = src_geom->y = 0;
		src_geom->width = pwidth;
		src_geom->height = pheight;

		break;

	case WPTYPE_STRETCHED:
		dest_geom->width = field_geom->width;
		dest_geom->height = field_geom->height;
		dest_geom->x = 0;
		dest_geom->y = 0;
		src_geom->x = src_geom->y = 0;
		src_geom->width = pwidth;
		src_geom->height = pheight;
		break;

	default:
		g_error ("Bad wallpaper type");
		break;
	}
}

/* Place one pixbuf onto another, compositing and scaling as necessary */

static GdkPixbuf *
place_pixbuf (GdkPixbuf *dest_pixbuf,
	      GdkPixbuf *src_pixbuf,
	      GdkRectangle *dest_geom,
	      GdkRectangle *src_geom,
	      guint alpha,
	      GdkColor *bg_color) 
{
	gboolean need_composite;
	gboolean need_scaling;
	gdouble scale_x, scale_y;
	gint real_dest_x, real_dest_y;
	guint colorv;

	need_composite = (alpha < 255 || gdk_pixbuf_get_has_alpha (src_pixbuf));
	need_scaling = ((dest_geom->width != src_geom->width) || (dest_geom->height != src_geom->height));

	if (need_scaling) {
		scale_x = (gdouble) dest_geom->width / (gdouble) src_geom->width;
		scale_y = (gdouble) dest_geom->height / (gdouble) src_geom->height;
	} else {
		scale_x = scale_y = 1.0;
	}

	if (need_composite && dest_pixbuf != NULL) {
		gdk_pixbuf_composite
			(src_pixbuf, dest_pixbuf,
			 dest_geom->x, dest_geom->y,
			 dest_geom->width, 
			 dest_geom->height,
			 dest_geom->x - src_geom->x * scale_x,
			 dest_geom->y - src_geom->y * scale_y,
			 scale_x, scale_y,
			 GDK_INTERP_BILINEAR,
			 alpha);
	}
	else if (need_composite) {
		dest_pixbuf = gdk_pixbuf_new
			(GDK_COLORSPACE_RGB, FALSE, 8,
			 dest_geom->width, dest_geom->height);

		colorv = ((bg_color->red & 0xff00) << 8) |
			(bg_color->green & 0xff00) |
			((bg_color->blue & 0xff00) >> 8);

		gdk_pixbuf_composite_color 
			(src_pixbuf, dest_pixbuf,
			 0, 0,
			 dest_geom->width, 
			 dest_geom->height,
			 -src_geom->x * scale_x,
			 -src_geom->y * scale_y,
			 scale_x, scale_y,
			 GDK_INTERP_BILINEAR,
			 alpha, 0, 0, 65536,
			 colorv, colorv);
	}
	else if (need_scaling) {
		if (dest_pixbuf == NULL) {
			dest_pixbuf = gdk_pixbuf_new
				(GDK_COLORSPACE_RGB, FALSE, 8,
				 dest_geom->width, dest_geom->height);
			real_dest_x = real_dest_y = 0;
		} else {
			real_dest_x = dest_geom->x;
			real_dest_y = dest_geom->y;
		}

		gdk_pixbuf_scale 
			(src_pixbuf, dest_pixbuf,
			 real_dest_x, real_dest_y,
			 dest_geom->width,
			 dest_geom->height,
			 real_dest_x - src_geom->x * scale_x,
			 real_dest_y - src_geom->y * scale_y,
			 scale_x, scale_y,
			 GDK_INTERP_BILINEAR);
	}
	else if (dest_pixbuf != NULL) {
		gdk_pixbuf_copy_area
			(src_pixbuf,
			 src_geom->x, src_geom->y, 
			 src_geom->width,
			 src_geom->height,
			 dest_pixbuf,
			 dest_geom->x, dest_geom->y);
	} else {
		dest_pixbuf = src_pixbuf;
		gdk_pixbuf_ref (dest_pixbuf);
	}

	return dest_pixbuf;
}

/* Tile one pixbuf repeatedly onto another, compositing as necessary. Assumes
 * that the source pixbuf has already been scaled properly
 */

static GdkPixbuf *
tile_pixbuf (GdkPixbuf *dest_pixbuf,
	     GdkPixbuf *src_pixbuf,
	     GdkRectangle *field_geom,
	     guint alpha,
	     GdkColor *bg_color) 
{
	gboolean need_composite;
	gboolean use_simple;
	gdouble cx, cy;
	gdouble colorv;
	gint pwidth, pheight;

	need_composite = (alpha < 255 || gdk_pixbuf_get_has_alpha (src_pixbuf));
	use_simple = (dest_pixbuf == NULL);

	if (dest_pixbuf == NULL)
		dest_pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, field_geom->width, field_geom->height);

	if (need_composite && use_simple)
		colorv = ((bg_color->red & 0xff00) << 8) |
			(bg_color->green & 0xff00) |
			((bg_color->blue & 0xff00) >> 8);
	else
		colorv = 0;

	pwidth = gdk_pixbuf_get_width (src_pixbuf);
	pheight = gdk_pixbuf_get_height (src_pixbuf);

	for (cy = 0; cy < field_geom->height; cy += pheight) {
		for (cx = 0; cx < field_geom->width; cx += pwidth) {
			if (need_composite && !use_simple)
				gdk_pixbuf_composite
					(src_pixbuf, dest_pixbuf,
					 cx, cy,
					 MIN (pwidth, field_geom->width - cx), 
					 MIN (pheight, field_geom->height - cy),
					 cx, cy,
					 1.0, 1.0,
					 GDK_INTERP_BILINEAR,
					 alpha);
			else if (need_composite && use_simple)
				gdk_pixbuf_composite_color
					(src_pixbuf, dest_pixbuf,
					 cx, cy,
					 MIN (pwidth, field_geom->width - cx), 
					 MIN (pheight, field_geom->height - cy),
					 cx, cy,
					 1.0, 1.0,
					 GDK_INTERP_BILINEAR,
					 alpha,
					 65536, 65536, 65536,
					 colorv, colorv);
			else
				gdk_pixbuf_copy_area
					(src_pixbuf,
					 0, 0,
					 MIN (pwidth, field_geom->width - cx),
					 MIN (pheight, field_geom->height - cy),
					 dest_pixbuf,
					 cx, cy);
		}
	}

	return dest_pixbuf;
}

/* Fill a raw character array with gradient data; the data may then be imported
 * into a GdkPixbuf
 */

static void
fill_gradient (GdkPixbuf     *pixbuf,
	       GdkColor      *c1,
	       GdkColor      *c2,
	       orientation_t  orientation)
{
	int i, j;
	int dr, dg, db;
	int gs1;
	int vc = ((orientation == ORIENTATION_HORIZ) || (c1 == c2));
	int w = gdk_pixbuf_get_width (pixbuf);
	int h = gdk_pixbuf_get_height (pixbuf);
	guchar *b, *row;
	guchar *d = gdk_pixbuf_get_pixels (pixbuf);
	int rowstride = gdk_pixbuf_get_rowstride (pixbuf);

#define R1 c1->red
#define G1 c1->green
#define B1 c1->blue
#define R2 c2->red
#define G2 c2->green
#define B2 c2->blue

	dr = R2 - R1;
	dg = G2 - G1;
	db = B2 - B1;

	gs1 = (orientation == ORIENTATION_VERT) ? h-1 : w-1;

	row = g_new (unsigned char, rowstride);

	if (vc) {
		b = row;
		for (j = 0; j < w; j++) {
			*b++ = (R1 + (j * dr) / gs1) >> 8;
			*b++ = (G1 + (j * dg) / gs1) >> 8;
			*b++ = (B1 + (j * db) / gs1) >> 8;
		}
	}

	for (i = 0; i < h; i++) {
		if (!vc) {
			unsigned char cr, cg, cb;
			cr = (R1 + (i * dr) / gs1) >> 8;
			cg = (G1 + (i * dg) / gs1) >> 8;
			cb = (B1 + (i * db) / gs1) >> 8;
			b = row;
			for (j = 0; j < w; j++) {
				*b++ = cr;
				*b++ = cg;
				*b++ = cb;
			}
		}
		memcpy (d, row, w * 3);
		d += rowstride;
	}

#undef R1
#undef G1
#undef B1
#undef R2
#undef G2
#undef B2

	g_free (row);
}

/* Boolean predicates to assist optimization and rendering */

/* Return TRUE iff the wallpaper filename or enabled settings have changed
 * between old_prefs and new_prefs
 */

static gboolean
need_wallpaper_load_p (const Applier *applier, const Preferences *prefs)
{
	if (applier->p->last_prefs == NULL)
		return TRUE;
	else if (applier->p->last_prefs->wallpaper_enabled != prefs->wallpaper_enabled)
		return TRUE;
	else if (!applier->p->last_prefs->wallpaper_enabled && !prefs->wallpaper_enabled)
		return FALSE;
	else if (strcmp (applier->p->last_prefs->wallpaper_filename, prefs->wallpaper_filename))
		return TRUE;
	else
		return FALSE;
}

/* Return TRUE iff we need to create a new root pixmap */

static gboolean
need_root_pixmap_p (const Applier *applier, const Preferences *prefs) 
{
	if (applier->p->last_prefs == NULL)
		return TRUE;
	else if (render_small_pixmap_p (applier->p->last_prefs) != render_small_pixmap_p (prefs))
		return TRUE;
	else if (!render_small_pixmap_p (applier->p->last_prefs) &&
		 !render_small_pixmap_p (prefs))
		return FALSE;
	else if (applier->p->last_prefs->orientation != prefs->orientation)
		return TRUE;
	else
		return FALSE;
}

/* Return TRUE iff the colors are equal */

/* Return TRUE iff the wallpaper completely covers the colors in the given
 * preferences structure, assuming we have already loaded the wallpaper pixbuf */

static gboolean
wallpaper_full_cover_p (const Applier *applier, const Preferences *prefs) 
{
	gint swidth, sheight;
	gint pwidth, pheight;
	gdouble asp1, asp2;

	/* We can't make this determination until the wallpaper is loaded, if
	 * wallpaper is enabled */
	g_return_val_if_fail (!prefs->wallpaper_enabled || applier->p->wallpaper_pixbuf != NULL, TRUE);

	if (!prefs->wallpaper_enabled)
		return FALSE;
	else if (gdk_pixbuf_get_has_alpha (applier->p->wallpaper_pixbuf))
		return FALSE;
	else if (prefs->wallpaper_type == WPTYPE_TILED)
		return TRUE;
	else if (prefs->wallpaper_type == WPTYPE_STRETCHED)
		return TRUE;

	gdk_window_get_size (GDK_ROOT_PARENT (), &swidth, &sheight);
	pwidth = gdk_pixbuf_get_width (applier->p->wallpaper_pixbuf);
	pheight = gdk_pixbuf_get_height (applier->p->wallpaper_pixbuf);

	if (prefs->wallpaper_type == WPTYPE_CENTERED) {
		if (pwidth >= swidth && pheight >= sheight)
			return TRUE;
		else
			return FALSE;
	}
	else if (prefs->wallpaper_type == WPTYPE_SCALED) {
		asp1 = (gdouble) swidth / (gdouble) sheight;
		asp2 = (gdouble) pwidth / (gdouble) pheight;

		if (swidth * (asp1 - asp2) < 1 && swidth * (asp2 - asp1) < 1)
			return TRUE;
		else
			return FALSE;
	}

	return FALSE;
}

/* Return TRUE if we can optimize the rendering by using a small thin pixmap */

static gboolean
render_small_pixmap_p (const Preferences *prefs) 
{
	return prefs->gradient_enabled && !prefs->wallpaper_enabled;
}

/* Create a persistant pixmap. We create a separate display
 * and set the closedown mode on it to RetainPermanent
 */
static GdkPixmap *
make_root_pixmap (gint width, gint height)
{
	Display *display;
	Pixmap xpixmap;
	
	display = XOpenDisplay (gdk_display_name);
	XSetCloseDownMode (display, RetainPermanent);

	xpixmap = XCreatePixmap (display,
				 DefaultRootWindow (display),
				 width, height,
				 DefaultDepthOfScreen (DefaultScreenOfDisplay (GDK_DISPLAY ())));

	XCloseDisplay (display);

	return gdk_pixmap_foreign_new (xpixmap);
}

/* Set the root pixmap, and properties pointing to it. We
 * do this atomically with XGrabServer to make sure that
 * we won't leak the pixmap if somebody else it setting
 * it at the same time. (This assumes that they follow the
 * same conventions we do)
 */

static void 
set_root_pixmap (GdkPixmap *pixmap) 
{
	GdkAtom type;
	gulong nitems, bytes_after;
	gint format;
	guchar *data_esetroot;
	Pixmap pixmap_id;

	pixmap_id = GDK_WINDOW_XWINDOW (pixmap);

	XGrabServer (GDK_DISPLAY ());

	XGetWindowProperty (GDK_DISPLAY (), GDK_ROOT_WINDOW (),
			    gdk_atom_intern ("ESETROOT_PMAP_ID", FALSE),
			    0L, 1L, False, XA_PIXMAP,
			    &type, &format, &nitems, &bytes_after,
			    &data_esetroot);

	if (type == XA_PIXMAP) {
		if (format == 32 && nitems == 1) {
			Pixmap old_pixmap;

			old_pixmap = *((Pixmap *) data_esetroot);

			if (pixmap != (GdkPixmap *) -1 && old_pixmap != pixmap_id)
				XKillClient (GDK_DISPLAY (), old_pixmap);
			else if (pixmap == (GdkPixmap *) -1)
				pixmap_id = old_pixmap;
		}

		XFree (data_esetroot);
	}

	if (pixmap != NULL && pixmap != (GdkPixmap *) -1) {
		XChangeProperty (GDK_DISPLAY (), GDK_ROOT_WINDOW (),
				 gdk_atom_intern ("ESETROOT_PMAP_ID", FALSE),
				 XA_PIXMAP, 32, PropModeReplace,
				 (guchar *) &pixmap_id, 1);
		XChangeProperty (GDK_DISPLAY (), GDK_ROOT_WINDOW (),
				 gdk_atom_intern ("_XROOTPMAP_ID", FALSE),
				 XA_PIXMAP, 32, PropModeReplace,
				 (guchar *) &pixmap_id, 1);

		XSetWindowBackgroundPixmap (GDK_DISPLAY (), GDK_ROOT_WINDOW (),
					    pixmap_id);
	} else if (pixmap == NULL) {
		XDeleteProperty (GDK_DISPLAY (), GDK_ROOT_WINDOW (),
				 gdk_atom_intern ("ESETROOT_PMAP_ID", FALSE));
		XDeleteProperty (GDK_DISPLAY (), GDK_ROOT_WINDOW (),
				 gdk_atom_intern ("_XROOTPMAP_ID", FALSE));
	}

	XClearWindow (GDK_DISPLAY (), GDK_ROOT_WINDOW ());
	XUngrabServer (GDK_DISPLAY ());
	XFlush (GDK_DISPLAY ());
}

static gboolean
is_nautilus_running (void)
{
	Atom window_id_atom;
	Window nautilus_xid;
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	unsigned char *data;
	int retval;
	Atom wmclass_atom;
	gboolean running;
	gint error;

	window_id_atom = XInternAtom (GDK_DISPLAY (), 
				      "NAUTILUS_DESKTOP_WINDOW_ID", True);

	if (window_id_atom == None) return FALSE;

	retval = XGetWindowProperty (GDK_DISPLAY (), GDK_ROOT_WINDOW (),
				     window_id_atom, 0, 1, False, XA_WINDOW,
				     &actual_type, &actual_format, &nitems,
				     &bytes_after, &data);

	if (data != NULL) {
		nautilus_xid = *(Window *) data;
		XFree (data);
	} else {
		return FALSE;
	}

	if (actual_type != XA_WINDOW) return FALSE;
	if (actual_format != 32) return FALSE;

	wmclass_atom = XInternAtom (GDK_DISPLAY (), "WM_CLASS", False);

	gdk_error_trap_push ();

	retval = XGetWindowProperty (GDK_DISPLAY (), nautilus_xid,
				     wmclass_atom, 0, 24, False, XA_STRING,
				     &actual_type, &actual_format, &nitems,
				     &bytes_after, &data);

	error = gdk_error_trap_pop ();

	if (error == BadWindow) return FALSE;

	if (actual_type == XA_STRING &&
	    nitems == 24 &&
	    bytes_after == 0 &&
	    actual_format == 8 &&
	    data != NULL &&
	    !strcmp (data, "desktop_window") &&
	    !strcmp (data + strlen (data) + 1, "Nautilus"))
		running = TRUE;
	else
		running = FALSE;

	if (data != NULL)
		XFree (data);

	return running;
}
