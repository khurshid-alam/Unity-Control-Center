/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2010 Red Hat, Inc.
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
 * Authors: William Jon McCann <jmccann@redhat.com>
 *          Thomas Wood <thomas.wood@intel.com>
 *
 */

/**
 * SECTION:cc-panel
 * @short_description: An abstract class for Control Center panels
 *
 * CcPanel is an abstract class used to implement panels for the shell. A
 * panel contains a collection of related settings that are displayed within
 * the shell window.
 */

#include "config.h"

#include "cc-panel.h"

#include <stdlib.h>
#include <stdio.h>

#include <gtk/gtk.h>
#include <gio/gio.h>

#define CC_PANEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CC_TYPE_PANEL, CcPanelPrivate))

struct CcPanelPrivate
{
  gchar    *id;
  gchar    *display_name;
  gchar    *category;
  gchar    *current_location;

  gboolean  is_active;
  CcShell  *shell;
};

enum
{
    PROP_0,
    PROP_SHELL,
};

G_DEFINE_ABSTRACT_TYPE (CcPanel, cc_panel, GTK_TYPE_BIN)

static void
cc_panel_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
  CcPanel *panel;

  panel = CC_PANEL (object);

  switch (prop_id)
    {
    case PROP_SHELL:
      /* construct only property */
      panel->priv->shell = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cc_panel_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
  CcPanel *panel;

  panel = CC_PANEL (object);

  switch (prop_id)
    {
    case PROP_SHELL:
      g_value_set_object (value, panel->priv->shell);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
cc_panel_finalize (GObject *object)
{
  CcPanel *panel;

  g_return_if_fail (object != NULL);
  g_return_if_fail (CC_IS_PANEL (object));

  panel = CC_PANEL (object);

  g_free (panel->priv->id);
  g_free (panel->priv->display_name);

  G_OBJECT_CLASS (cc_panel_parent_class)->finalize (object);
}

static void
cc_panel_size_request (GtkWidget      *widget,
                       GtkRequisition *requisition)
{
  GtkBin *bin = GTK_BIN (widget);
  GtkWidget *child;
  guint border_width;

  if ((child = gtk_bin_get_child (bin)))
    {
      GtkRequisition child_requisition;

      gtk_widget_size_request (child, &child_requisition);

      requisition->width = child_requisition.width;
      requisition->height = child_requisition.height;
    }

  border_width = gtk_container_get_border_width (GTK_CONTAINER (widget));
  requisition->width += 2 * border_width;
  requisition->height += 2 * border_width;
}

static void
cc_panel_size_allocate (GtkWidget     *widget,
                             GtkAllocation *allocation)
{
  GtkAllocation child_allocation;
  guint border_width;

  child_allocation = *allocation;

  border_width = gtk_container_get_border_width (GTK_CONTAINER (widget));


  child_allocation.width -= 2 * border_width;
  child_allocation.height -= 2 * border_width;

  gtk_widget_size_allocate (gtk_bin_get_child (GTK_BIN (widget)),
                            &child_allocation);
}

static void
cc_panel_class_init (CcPanelClass *klass)
{
  GParamSpec      *pspec;
  GObjectClass    *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass  *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = cc_panel_get_property;
  object_class->set_property = cc_panel_set_property;
  object_class->finalize = cc_panel_finalize;

  widget_class->size_request = cc_panel_size_request;
  widget_class->size_allocate = cc_panel_size_allocate;

  g_type_class_add_private (klass, sizeof (CcPanelPrivate));

  pspec = g_param_spec_object ("shell",
                               "Shell",
                               "Shell the Panel resides in",
                               CC_TYPE_SHELL,
                               G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
                               | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_SHELL, pspec);
}

static void
cc_panel_init (CcPanel *panel)
{
  panel->priv = CC_PANEL_GET_PRIVATE (panel);
}

/**
 * cc_panel_get_shell:
 * @panel: A #CcPanel
 *
 * Get the shell that the panel resides in
 *
 * Returns: a #CcShell
 */
CcShell *
cc_panel_get_shell (CcPanel *panel)
{
  return panel->priv->shell;
}

