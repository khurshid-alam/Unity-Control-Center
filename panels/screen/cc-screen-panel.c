/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2010 Red Hat, Inc
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
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
 */

#include "cc-screen-panel.h"

#define WID(b, w) (GtkWidget *) gtk_builder_get_object (b, w)

G_DEFINE_DYNAMIC_TYPE (CcScreenPanel, cc_screen_panel, CC_TYPE_PANEL)

#define SCREEN_PANEL_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CC_TYPE_SCREEN_PANEL, CcScreenPanelPrivate))

struct _CcScreenPanelPrivate
{
  GSettings     *lock_settings;
  GSettings     *gsd_settings;
  GCancellable  *cancellable;
  GtkBuilder    *builder;
  GDBusProxy    *proxy;
  gboolean       setting_brightness;
};


static void
cc_screen_panel_get_property (GObject    *object,
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
cc_screen_panel_set_property (GObject      *object,
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
cc_screen_panel_dispose (GObject *object)
{
  CcScreenPanelPrivate *priv = CC_SCREEN_PANEL (object)->priv;

  if (priv->lock_settings)
    {
      g_object_unref (priv->lock_settings);
      priv->lock_settings = NULL;
    }
  if (priv->gsd_settings)
    {
      g_object_unref (priv->gsd_settings);
      priv->gsd_settings = NULL;
    }
  if (priv->cancellable != NULL)
    {
      g_object_unref (priv->cancellable);
      priv->cancellable = NULL;
    }
  if (priv->builder != NULL)
    {
      g_object_unref (priv->builder);
      priv->builder = NULL;
    }
  if (priv->proxy != NULL)
    {
      g_object_unref (priv->proxy);
      priv->proxy = NULL;
    }

  G_OBJECT_CLASS (cc_screen_panel_parent_class)->dispose (object);
}

static void
cc_screen_panel_finalize (GObject *object)
{
  CcScreenPanelPrivate *priv = CC_SCREEN_PANEL (object)->priv;
  g_cancellable_cancel (priv->cancellable);
  G_OBJECT_CLASS (cc_screen_panel_parent_class)->finalize (object);
}

static void
on_lock_settings_changed (GSettings     *settings,
                          const char    *key,
                          CcScreenPanel *panel)
{
}

static void
cc_screen_panel_class_init (CcScreenPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (CcScreenPanelPrivate));

  object_class->get_property = cc_screen_panel_get_property;
  object_class->set_property = cc_screen_panel_set_property;
  object_class->dispose = cc_screen_panel_dispose;
  object_class->finalize = cc_screen_panel_finalize;
}

static void
cc_screen_panel_class_finalize (CcScreenPanelClass *klass)
{
}

static void
on_signal (GDBusProxy *proxy,
           gchar      *sender_name,
           gchar      *signal_name,
           GVariant   *parameters,
           gpointer    user_data)
{
  CcScreenPanelPrivate *priv = CC_SCREEN_PANEL (user_data)->priv;

  if (g_strcmp0 (signal_name, "BrightnessChanged") == 0)
    {
      guint brightness;
      GtkRange *range;

      /* changed, but ignoring */
      if (priv->setting_brightness)
        return;

      /* update the bar */
      g_variant_get (parameters,
                     "(u)",
                     &brightness);
      range = GTK_RANGE (gtk_builder_get_object (priv->builder,
                                                 "screen_brightness_hscale"));
      gtk_range_set_value (range, brightness);
    }
}

static void
set_brightness_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GError *error = NULL;
  GVariant *result;

  CcScreenPanelPrivate *priv = CC_SCREEN_PANEL (user_data)->priv;

  /* not setting, so pay attention to changed signals */
  priv->setting_brightness = FALSE;
  result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);
  if (result == NULL)
    {
      g_printerr ("Error setting brightness: %s\n", error->message);
      g_error_free (error);
      return;
    }
}

static void
brightness_slider_value_changed_cb (GtkRange *range, gpointer user_data)
{
  guint percentage;
  CcScreenPanelPrivate *priv = CC_SCREEN_PANEL (user_data)->priv;

  /* do not loop */
  priv->setting_brightness = TRUE;

  /* push this to g-p-m */
  percentage = (guint) gtk_range_get_value (range);
  g_dbus_proxy_call (priv->proxy,
                     "SetBrightness",
                     g_variant_new ("(u)",
                                    percentage),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     priv->cancellable,
                     set_brightness_cb,
                     user_data);
}

static void
get_brightness_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GError *error = NULL;
  GVariant *result;
  guint brightness;
  GtkRange *range;
  CcScreenPanelPrivate *priv = CC_SCREEN_PANEL (user_data)->priv;

  result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);
  if (result == NULL)
    {
      g_printerr ("Error getting brightness: %s\n", error->message);
      g_error_free (error);
      return;
    }

  /* set the slider */
  g_variant_get (result,
                 "(u)",
                 &brightness);
  range = GTK_RANGE (gtk_builder_get_object (priv->builder, "screen_brightness_hscale"));
  gtk_range_set_range (range, 0, 100);
  gtk_range_set_increments (range, 1, 10);
  gtk_range_set_value (range, brightness);
  g_signal_connect (range,
                    "value-changed",
                    G_CALLBACK (brightness_slider_value_changed_cb),
                    user_data);
  g_variant_unref (result);
}

static void
got_power_proxy_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GError *error = NULL;
  CcScreenPanelPrivate *priv = CC_SCREEN_PANEL (user_data)->priv;

  priv->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
  if (priv->proxy == NULL)
    {
      g_printerr ("Error creating proxy: %s\n", error->message);
      g_error_free (error);
      return;
    }

  /* we want to change the bar if the user presses brightness buttons */
  g_signal_connect (priv->proxy,
                    "g-signal",
                    G_CALLBACK (on_signal),
                    user_data);

  /* get the initial state */
  g_dbus_proxy_call (priv->proxy,
                     "GetBrightness",
                     NULL,
                     G_DBUS_CALL_FLAGS_NONE,
                     200, /* we don't want to randomly move the bar */
                     priv->cancellable,
                     get_brightness_cb,
                     user_data);
}

static void
cc_screen_panel_init (CcScreenPanel *self)
{
  GError     *error;
  GtkWidget  *widget;

  self->priv = SCREEN_PANEL_PRIVATE (self);

  self->priv->builder = gtk_builder_new ();

  error = NULL;
  gtk_builder_add_from_file (self->priv->builder,
                             GNOMECC_UI_DIR "/screen.ui",
                             &error);

  if (error != NULL)
    {
      g_warning ("Could not load interface file: %s", error->message);
      g_error_free (error);
      return;
    }

  self->priv->cancellable = g_cancellable_new ();

  /* get initial brightness version */
  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            "org.gnome.PowerManager",
                            "/org/gnome/PowerManager/Backlight",
                            "org.gnome.PowerManager.Backlight",
                            self->priv->cancellable,
                            got_power_proxy_cb,
                            self);

  self->priv->lock_settings = g_settings_new ("org.gnome.desktop.interface");
  g_signal_connect (self->priv->lock_settings,
                    "changed",
                    G_CALLBACK (on_lock_settings_changed),
                    self);
  self->priv->gsd_settings = g_settings_new ("org.gnome.settings-daemon.plugins.power");

  /* bind the auto dim checkbox */
  widget = GTK_WIDGET (gtk_builder_get_object (self->priv->builder,
                                               "screen_auto_reduce_checkbutton"));
  g_settings_bind (self->priv->gsd_settings,
                   "idle-dim-battery",
                   widget, "active",
                   G_SETTINGS_BIND_DEFAULT);

  widget = WID (self->priv->builder, "screen_vbox");
  gtk_widget_reparent (widget, (GtkWidget *) self);
}

void
cc_screen_panel_register (GIOModule *module)
{
  cc_screen_panel_register_type (G_TYPE_MODULE (module));
  g_io_extension_point_implement (CC_SHELL_PANEL_EXTENSION_POINT,
                                  CC_TYPE_SCREEN_PANEL,
                                  "screen", 0);
}

