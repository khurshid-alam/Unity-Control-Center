/*
 * Copyright (C) 2010 Intel, Inc
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Author: Thomas Wood <thomas.wood@intel.com>
 *
 */

#include "cc-mouse-panel.h"
#include <gconf/gconf-client.h>
#include <gtk/gtk.h>

G_DEFINE_DYNAMIC_TYPE (CcMousePanel, cc_mouse_panel, CC_TYPE_PANEL)

#define MOUSE_PANEL_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CC_TYPE_MOUSE_PANEL, CcMousePanelPrivate))

struct _CcMousePanelPrivate
{
  GtkBuilder *builder;
  GConfClient *client;
};


static void
cc_mouse_panel_get_property (GObject    *object,
                             guint       property_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
cc_mouse_panel_set_property (GObject      *object,
                             guint         property_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
cc_mouse_panel_dispose (GObject *object)
{
  CcMousePanelPrivate *priv = CC_MOUSE_PANEL (object)->priv;
  if (priv->client)
    {
      g_object_unref (priv->client);
      priv->client = NULL;
    }

  if (priv->builder)
    {
      g_object_unref (priv->builder);
      priv->builder = NULL;
    }

  G_OBJECT_CLASS (cc_mouse_panel_parent_class)->dispose (object);
}

static void
cc_mouse_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_mouse_panel_parent_class)->finalize (object);
}

static void
cc_mouse_panel_class_init (CcMousePanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (CcMousePanelPrivate));

  object_class->get_property = cc_mouse_panel_get_property;
  object_class->set_property = cc_mouse_panel_set_property;
  object_class->dispose = cc_mouse_panel_dispose;
  object_class->finalize = cc_mouse_panel_finalize;
}

static void
cc_mouse_panel_class_finalize (CcMousePanelClass *klass)
{
}

GtkWidget *
gnome_mouse_properties_init (GConfClient *client, GtkBuilder *dialog);

static void
cc_mouse_panel_init (CcMousePanel *self)
{
  CcMousePanelPrivate *priv;
  GtkWidget *prefs_widget;
  GError *error = NULL;

  priv = self->priv = MOUSE_PANEL_PRIVATE (self);

  priv->client = gconf_client_get_default ();
  priv->builder = gtk_builder_new ();

  gtk_builder_add_from_file (priv->builder,
                             GNOMECC_UI_DIR "/gnome-mouse-properties.ui",
                             &error);
  if (error != NULL)
    {
      g_warning ("Error loading UI file: %s", error->message);
      return;
    }

  gnome_mouse_properties_init (priv->client, priv->builder);

  prefs_widget = (GtkWidget*) gtk_builder_get_object (priv->builder,
                                                      "prefs_widget");

  gtk_widget_reparent (prefs_widget, GTK_WIDGET (self));
}

void
cc_mouse_panel_register (GIOModule *module)
{
  cc_mouse_panel_register_type (G_TYPE_MODULE (module));
  g_io_extension_point_implement (CC_SHELL_PANEL_EXTENSION_POINT,
                                  CC_TYPE_MOUSE_PANEL,
                                  "gnome-settings-mouse.desktop", 0);
}
