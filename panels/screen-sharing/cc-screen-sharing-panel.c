/*
 * Copyright (C) 2003 Sun Microsystems, Inc.
 * Copyright (C) 2006 Jonh Wendell <wendell@bani.com.br> 
 * Copyright © 2010 Codethink Limited
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *      Mark McLoughlin <mark@skynet.ie>
 *      Jonh Wendell <wendell@bani.com.br>
 *      Ryan Lortie <desrt@desrt.ca>
 */

#include "config.h"

#include "cc-screen-sharing-panel.h"
#include "vino-radio-button.h"
#include "vino-message-box.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <shell/cc-panel.h>

typedef struct
{
  CcPanel parent;
} CcScreenSharingPanel;

typedef struct
{
  CcPanelClass parent_class;
} CcScreenSharingPanelClass;

static GType cc_screen_sharing_panel_get_type (void);

CC_PANEL_REGISTER (CcScreenSharingPanel, cc_screen_sharing_panel)

#define VINO_SCHEMA_ID "org.gnome.Vino"

/* We define three GSettings mappings here:
 *
 * First, a relatively boring boolean inversion mapping.
 */
static gboolean
get_inverted (GValue   *value,
              GVariant *variant,
              gpointer  user_data)
{
  g_value_set_boolean (value, !g_variant_get_boolean (variant));
  return TRUE;
}

static GVariant *
set_inverted (const GValue       *value,
              const GVariantType *type,
              gpointer            user_data)
{
  return g_variant_new_boolean (!g_value_get_boolean (value));
}

/* Next, one that maps between the array-of-strings list of
 * authentication mechanisms and a boolean that is FALSE if the 'none'
 * and TRUE otherwise (ie: for 'vnc' in the list).
 */
static gboolean
get_authtype (GValue   *value,
              GVariant *variant,
              gpointer  user_data)
{
  GVariantIter iter;
  const gchar *type;

  g_variant_iter_init (&iter, variant);
  g_value_set_boolean (value, TRUE);

  while (g_variant_iter_next (&iter, "s", &type))
    if (strcmp (type, "none") == 0)
      g_value_set_boolean (value, FALSE);

  return TRUE;
}

static GVariant *
set_authtype (const GValue       *value,
              const GVariantType *type,
              gpointer            user_data)
{
  const gchar *authtype;

  if (g_value_get_boolean (value))
    authtype = "vnc";
  else
    authtype = "none";

  return g_variant_new_strv (&authtype, 1);
}


/* Finally, a somewhat evil mapping for the password setting:
 *
 * If the setting is 'keyring' then we load the password from the
 * keyring.  Else, it is assumed to be a base64-encoded string which is
 * the actual password.
 *
 * On setting the password, we always first try to use the keyring.  If
 * that is successful we write 'keyring' to the settings.  If it fails
 * then we base64-encode the password and write it to the settings.
 *
 * Doing it this way ensures that there is no ambiguity about what the
 * password is in the event that gnome-keyring becomes available then
 * unavailable again.
 */
static gboolean
get_password (GValue   *value,
              GVariant *variant,
              gpointer  user_data)
{
  const gchar *setting;

  setting = g_variant_get_string (variant, NULL);

  if (strcmp (setting, "keyring") == 0)
    {
      /* "keyring" is the default value, even though vino doesn't support it at the moment */
      g_value_set_static_string (value, "");
      return TRUE;
    }
  else
    {
      gchar *decoded;
      gsize length;
      gboolean ok;

      decoded = (gchar *) g_base64_decode (setting, &length);

      if ((ok = g_utf8_validate (decoded, length, NULL)))
        g_value_take_string (value, g_strndup (decoded, length));

      return ok;
    }
}

static GVariant *
set_password (const GValue       *value,
              const GVariantType *type,
              gpointer            user_data)
{
  const gchar *string;
  gchar *base64;

  string = g_value_get_string (value);

  /* if that failed, store it in GSettings, base64 */
  base64 = g_base64_encode ((guchar *) string, strlen (string));
  return g_variant_new_from_data (G_VARIANT_TYPE_STRING,
                                  base64, strlen (base64) + 1,
                                  TRUE, g_free, base64);
}

typedef enum
{
  VINO_ICON_VISIBILITY_NEVER,
  VINO_ICON_VISIBILITY_ALWAYS,
  VINO_ICON_VISIBILITY_CLIENT
} VinoIconVisibility;

static gboolean
get_icon_visibility (GValue   *value,
                     GVariant *variant,
                     gpointer  user_data)
{
  const char *setting;
  char *name;

  setting = g_variant_get_string (variant, NULL);

  g_object_get (user_data, "name", &name, NULL);

  /* If the button name matches the setting, it should be active. */
  if (g_strcmp0 (name, setting) == 0)
    g_value_set_boolean (value, TRUE);

  g_free (name);

  return TRUE;
}

static GVariant *
set_icon_visibility (const GValue       *value,
                     const GVariantType *type,
                     gpointer            user_data)
{
  GVariant *variant = NULL;
  char *name;

  /* Don't act unless the button has been activated (turned ON). */
  if (!g_value_get_boolean (value))
    return NULL;

  /* GtkToggleButton *button = GTK_TOGGLE_BUTTON(user_data); */
  g_object_get (user_data, "name", &name, NULL);
  variant = g_variant_new_string (name);

  g_free (name);

  return variant;
}

static void
cc_screen_sharing_panel_init (CcScreenSharingPanel *panel)
{
  struct {
    const gchar             *setting;
    const gchar             *name;
    const gchar             *property;
    GSettingsBindFlags       flags;
    GSettingsBindGetMapping  get_mapping;
    GSettingsBindSetMapping  set_mapping;
  } bindings[] = {
    { "enabled",                "allowed_toggle",        "active",    G_SETTINGS_BIND_DEFAULT, NULL,                NULL                },

    { "enabled",                "control_settings",      "sensitive", G_SETTINGS_BIND_GET,     NULL,                NULL                },
    { "view-only",              "view_only_toggle",      "active",    G_SETTINGS_BIND_DEFAULT, get_inverted,        set_inverted        },

    { "enabled",                "security_settings",     "sensitive", G_SETTINGS_BIND_GET,     NULL,                NULL                },
    { "prompt-enabled",         "prompt_enabled_toggle", "active",    G_SETTINGS_BIND_DEFAULT, NULL,                NULL                },
    { "authentication-methods", "password_toggle",       "active",    G_SETTINGS_BIND_DEFAULT, get_authtype,        set_authtype        },
    { "authentication-methods", "password_box",          "sensitive", G_SETTINGS_BIND_GET,     get_authtype,        NULL                },
    { "vnc-password",           "password_entry",        "text",      G_SETTINGS_BIND_DEFAULT, get_password,        set_password        },
    { "use-upnp",               "use_upnp_toggle",       "active",    G_SETTINGS_BIND_DEFAULT, NULL,                NULL                },

    { "enabled",                "notification_settings", "sensitive", G_SETTINGS_BIND_GET,     NULL,                NULL                },

    { "icon-visibility",        "icon_always_radio",     "active",    G_SETTINGS_BIND_DEFAULT, get_icon_visibility, set_icon_visibility },
    { "icon-visibility",        "icon_client_radio",     "active",    G_SETTINGS_BIND_DEFAULT, get_icon_visibility, set_icon_visibility },
    { "icon-visibility",        "icon_never_radio",      "active",    G_SETTINGS_BIND_DEFAULT, get_icon_visibility, set_icon_visibility }
  };
  GSettingsSchemaSource *source;

  source = g_settings_schema_source_get_default ();
  if (source && g_settings_schema_source_lookup (source, VINO_SCHEMA_ID, TRUE))
    {
      GError     *error = NULL;
      GtkBuilder *builder;
      GSettings *settings;
      gint i;

      vino_radio_button_get_type ();
      vino_message_box_get_type ();

      builder = gtk_builder_new ();
      if (!gtk_builder_add_from_file (builder, GNOMECC_UI_DIR "/screen-sharing-panel.ui", &error))
        {
          g_warning ("Unable to load interface file: %s", error->message);
          g_error_free (error);
        }
      gtk_widget_reparent (GTK_WIDGET (gtk_builder_get_object (builder, "screen_sharing_vbox")), GTK_WIDGET (panel));

      settings = g_settings_new (VINO_SCHEMA_ID);

      for (i = 0; i < G_N_ELEMENTS (bindings); i++)
        {
          GObject *object =  gtk_builder_get_object (builder, bindings[i].name);
          g_settings_bind_with_mapping (settings, bindings[i].setting,
                                        object,
                                        bindings[i].property,
                                        bindings[i].flags,
                                        bindings[i].get_mapping,
                                        bindings[i].set_mapping,
                                        object, NULL);
        }

      g_object_unref (settings);
      g_object_unref (builder);
    }
  else
    {
      GtkWidget *label;
      gchar *text;

      label = gtk_label_new (NULL);
      text = g_strdup_printf ("<big>%s</big>", _("Desktop sharing support not installed"));
      gtk_label_set_markup (GTK_LABEL (label), text);
      g_free (text);
      gtk_widget_show (label);
      gtk_container_add (GTK_CONTAINER (panel), label);
    }
}

static void
cc_screen_sharing_panel_class_init (CcScreenSharingPanelClass *class)
{
}

void
cc_screen_sharing_panel_register (GIOModule *module)
{
  cc_screen_sharing_panel_register_type (G_TYPE_MODULE (module));
  g_io_extension_point_implement (CC_SHELL_PANEL_EXTENSION_POINT,
                                  cc_screen_sharing_panel_get_type (),
                                  "screen-sharing", 0);
}
