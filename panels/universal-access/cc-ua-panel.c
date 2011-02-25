/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2010 Intel, Inc
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
 * Authors: Thomas Wood <thomas.wood@intel.com>
 *          Rodrigo Moya <rodrigo@gnome.org>
 *
 */

#include <math.h>
#include "cc-ua-panel.h"

#include <gconf/gconf-client.h>
#include <gio/gdesktopappinfo.h>

#include "eggaccelerators.h"
#include "gconf-property-editor.h"

#define WID(b, w) (GtkWidget *) gtk_builder_get_object (b, w)


G_DEFINE_DYNAMIC_TYPE (CcUaPanel, cc_ua_panel, CC_TYPE_PANEL)

#define UA_PANEL_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CC_TYPE_UA_PANEL, CcUaPanelPrivate))

struct _CcUaPanelPrivate
{
  GtkBuilder *builder;
  GConfClient *client;
  GSettings *interface_settings;
  GSettings *kb_settings;
  GSettings *mouse_settings;
  GSettings *font_settings;
  GSettings *application_settings;
  GSettings *mediakeys_settings;
  GSettings *mobility_settings;

  GtkWidget *typing_assistant_prefs;

  GSList *notify_list;
};


static void
cc_ua_panel_get_property (GObject    *object,
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
cc_ua_panel_set_property (GObject      *object,
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
cc_ua_panel_dispose (GObject *object)
{
  CcUaPanelPrivate *priv = CC_UA_PANEL (object)->priv;
  GSList *l;

  /* remove the notify callbacks, since they rely on builder/client being
   * available */
  if (priv->notify_list)
    {
      for (l = priv->notify_list; l; l = g_slist_next (l))
        {
          gconf_client_notify_remove (priv->client,
                                      GPOINTER_TO_INT (l->data));
        }
      g_slist_free (priv->notify_list);
      priv->notify_list = NULL;
    }


  if (priv->builder)
    {
      g_object_unref (priv->builder);
      priv->builder = NULL;
    }

  if (priv->client)
    {
      g_object_unref (priv->client);
      priv->client = NULL;
    }

  if (priv->interface_settings)
    {
      g_object_unref (priv->interface_settings);
      priv->interface_settings = NULL;
    }

  if (priv->kb_settings)
    {
      g_object_unref (priv->kb_settings);
      priv->kb_settings = NULL;
    }

  if (priv->mouse_settings)
    {
      g_object_unref (priv->mouse_settings);
      priv->mouse_settings = NULL;
    }

  if (priv->font_settings)
    {
      g_object_unref (priv->font_settings);
      priv->font_settings = NULL;
    }

  if (priv->application_settings)
    {
      g_object_unref (priv->application_settings);
      priv->application_settings = NULL;
    }

  if (priv->mediakeys_settings)
    {
      g_object_unref (priv->mediakeys_settings);
      priv->mediakeys_settings = NULL;
    }

  if (priv->mobility_settings)
    {
      g_object_unref (priv->mobility_settings);
      priv->mobility_settings = NULL;
    }

  G_OBJECT_CLASS (cc_ua_panel_parent_class)->dispose (object);
}

static void
cc_ua_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_ua_panel_parent_class)->finalize (object);
}

static void
cc_ua_panel_class_init (CcUaPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (CcUaPanelPrivate));

  object_class->get_property = cc_ua_panel_get_property;
  object_class->set_property = cc_ua_panel_set_property;
  object_class->dispose = cc_ua_panel_dispose;
  object_class->finalize = cc_ua_panel_finalize;
}

static void
cc_ua_panel_class_finalize (CcUaPanelClass *klass)
{
}

static gchar *typing_assistant_section[] = {
    "typing_assistant_preferences_button",
    NULL
};

static gchar *sticky_keys_section[] = {
    "typing_sticky_keys_disable_two_keys_checkbutton",
    "typing_sticky_keys_beep_modifier_checkbutton",
    NULL
};

static gchar *slow_keys_section[]= {
    "typing_slowkeys_delay_box",
    "typing_slow_keys_beeb_box",
    NULL
};

static gchar *bounce_keys_section[] = {
    "typing_bouncekeys_delay_box",
    "typing_bounce_keys_beep_rejected_checkbutton",
    NULL
};

static gchar *secondary_click_section[] = {
    "pointing_secondary_click_scale_box",
    NULL
};

static gchar *dwell_click_section[] = {
    "pointing_hover_click_delay_scale_box",
    "pointing_hover_click_threshold_scale_box",
    NULL
};

static gchar *visual_alerts_section[] = {
    "hearing_test_flash_button",
    "hearing_flash_window_title_button",
    "hearing_flash_screen_button",
    NULL
};

static void
cc_ua_panel_section_switched (GObject    *object,
                              GParamSpec *pspec,
                              GtkBuilder *builder)
{
  GtkWidget *w;
  gboolean enabled;
  gchar **widgets, **s;

  widgets = g_object_get_data (object, "section-widgets");

  enabled = gtk_switch_get_active (GTK_SWITCH (object));

  for (s = widgets; *s; s++)
    {
      w = WID (builder, *s);
      gtk_widget_set_sensitive (w, enabled);
    }
}

static void
settings_on_off_editor_new (CcUaPanelPrivate  *priv,
                            GSettings         *settings,
                            const gchar       *key,
                            GtkWidget         *widget,
                            gchar            **section)
{
  /* set data to enable/disable the section this on/off switch controls */
  if (section)
    {
      g_object_set_data (G_OBJECT (widget), "section-widgets", section);
      g_signal_connect (widget, "notify::active",
                        G_CALLBACK (cc_ua_panel_section_switched),
                        priv->builder);
    }

  /* set up the boolean editor */
  g_settings_bind (settings, key, widget, "active", G_SETTINGS_BIND_DEFAULT);
}

static GConfValue *
cc_ua_panel_toggle_switch (GConfPropertyEditor *peditor,
                           const GConfValue    *value)
{
  GtkWidget *sw;
  gboolean enabled;

  enabled = gconf_value_get_bool (value);
  sw = (GtkWidget*) gconf_property_editor_get_ui_control (peditor);

  gtk_switch_set_active (GTK_SWITCH (sw), enabled);

  return gconf_value_copy (value);
}

static void
gconf_on_off_peditor_new (CcUaPanelPrivate  *priv,
                          const gchar       *key,
                          GtkWidget         *widget,
                          gchar            **section)
{
  GObject *peditor;

  /* set data to enable/disable the section this on/off switch controls */
  if (section)
    {
      g_object_set_data (G_OBJECT (widget), "section-widgets", section);
      g_signal_connect (widget, "notify::active",
                        G_CALLBACK (cc_ua_panel_section_switched),
                        priv->builder);
    }

  /* set up the boolean editor */
  peditor = gconf_peditor_new_switch (NULL, key, widget, NULL);
  g_object_set (peditor, "conv-to-widget-cb", cc_ua_panel_toggle_switch, NULL);

  /* emit the notify on the key, so that the conv-to-widget-cb callback is run */
  gconf_client_notify (priv->client, key);
}

/* seeing section */
#define GTK_THEME_KEY "gtk-theme"
#define ICON_THEME_KEY "icon-theme"
#define CONTRAST_MODEL_THEME_COLUMN 3
#define DPI_MODEL_FACTOR_COLUMN 2

/* The following two functions taken from gsd-a11y-preferences-dialog.c
 *
 * Copyright (C)  2008 William Jon McCann <jmccann@redhat.com>
 *
 * Licensed under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* X servers sometimes lie about the screen's physical dimensions, so we cannot
 * compute an accurate DPI value.  When this happens, the user gets fonts that
 * are too huge or too tiny.  So, we see what the server returns:  if it reports
 * something outside of the range [DPI_LOW_REASONABLE_VALUE,
 * DPI_HIGH_REASONABLE_VALUE], then we assume that it is lying and we use
 * DPI_FALLBACK instead.
 *
 * See get_dpi_from_x_server() below, and also
 * https://bugzilla.novell.com/show_bug.cgi?id=217790
 */
#define DPI_LOW_REASONABLE_VALUE 50
#define DPI_HIGH_REASONABLE_VALUE 500
#define DPI_DEFAULT        96

static gdouble
dpi_from_pixels_and_mm (gint pixels,
                        gint mm)
{
  if (mm >= 1)
    return pixels / (mm / 25.4);
  else
    return 0;
}

static gdouble
get_dpi_from_x_server ()
{
  GdkScreen *screen;
  gdouble dpi;

  screen = gdk_screen_get_default ();

  if (screen)
    {
      gdouble width_dpi, height_dpi;

      width_dpi = dpi_from_pixels_and_mm (gdk_screen_get_width (screen),
                                          gdk_screen_get_width_mm (screen));
      height_dpi = dpi_from_pixels_and_mm (gdk_screen_get_height (screen),
                                           gdk_screen_get_height_mm (screen));

      if (width_dpi < DPI_LOW_REASONABLE_VALUE
          || width_dpi > DPI_HIGH_REASONABLE_VALUE
          || height_dpi < DPI_LOW_REASONABLE_VALUE
          || height_dpi > DPI_HIGH_REASONABLE_VALUE)
        {
          dpi = DPI_DEFAULT;
        }
      else
        {
          dpi = (width_dpi + height_dpi) / 2.0;
        }
    }
  else
    dpi = DPI_DEFAULT;

  return dpi;
}

static void dpi_combo_box_changed (GtkComboBox *box, CcUaPanel *panel);

static void
dpi_notify_cb (GSettings   *settings,
               const gchar *key,
               CcUaPanel   *panel)
{
  CcUaPanelPrivate *priv = panel->priv;
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkWidget *combo;
  gboolean valid;
  gdouble conf_value;
  gdouble x_dpi;
  GtkTreeIter best;
  gdouble distance;

  if (!g_str_equal (key, "dpi"))
    return;

  conf_value = g_settings_get_double (settings, key);

  combo = WID (priv->builder, "seeing_text_size_combobox");
  model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));

  /* get current value from screen */
  x_dpi = get_dpi_from_x_server ();

  /* find the closest match in the combobox model */
  distance = 1e6;
  valid = gtk_tree_model_get_iter_first (model, &iter);
  while (valid)
    {
      gfloat factor;
      gdouble d;

      gtk_tree_model_get (model, &iter,
                          DPI_MODEL_FACTOR_COLUMN, &factor,
                          -1);

      d = fabs (conf_value - factor * x_dpi);
      if (d < distance)
        {
          best = iter;
          distance = d;
        }

      valid = gtk_tree_model_iter_next (model, &iter);
    }

  g_signal_handlers_block_by_func (combo, dpi_combo_box_changed, panel);
  gtk_combo_box_set_active_iter (GTK_COMBO_BOX (combo), &best);
  g_signal_handlers_unblock_by_func (combo, dpi_combo_box_changed, panel);
}

static void
dpi_combo_box_changed (GtkComboBox *box,
                       CcUaPanel *panel)
{
  CcUaPanelPrivate *priv = panel->priv;
  GtkTreeIter iter;
  gfloat factor;

  gtk_combo_box_get_active_iter (box, &iter);

  gtk_tree_model_get (gtk_combo_box_get_model (box), &iter,
                      DPI_MODEL_FACTOR_COLUMN, &factor,
                      -1);

  if (factor == 1.0)
    g_settings_reset (priv->font_settings, "dpi");
  else
    {
      gdouble x_dpi, u_dpi;

      x_dpi = get_dpi_from_x_server ();
      u_dpi = (gdouble) factor * x_dpi;

      g_settings_set_double (priv->font_settings, "dpi", u_dpi);
    }
}


static void
interface_settings_changed_cb (GSettings   *settings,
                               const gchar *key,
                               CcUaPanel   *panel)
{
  CcUaPanelPrivate *priv = panel->priv;

  if (g_str_equal (key, "gtk-theme")) {
    GtkTreeIter iter;
    GtkTreeModel *model;
    GtkWidget *combo;
    gboolean valid;
    gchar *theme_value;

    theme_value = g_settings_get_string (settings, GTK_THEME_KEY);

    combo = WID (priv->builder, "seeing_contrast_combobox");
    model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));

    /* see if there is a matching theme name in the combobox model */
    valid = gtk_tree_model_get_iter_first (model, &iter);
    while (valid)
      {
        gchar *value;

        gtk_tree_model_get (model, &iter,
                            CONTRAST_MODEL_THEME_COLUMN, &value,
                            -1);

        if (!g_strcmp0 (value, theme_value))
          {
            gtk_combo_box_set_active_iter (GTK_COMBO_BOX (combo), &iter);
            g_free (value);
            break;
          }

        g_free (value);
        valid = gtk_tree_model_iter_next (model, &iter);
      }

    /* if a value for the current theme was not found in the combobox, set to the
     * "normal" option */
    if (!valid)
      {
        gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 1);
      }
  }
}

static void
contrast_combobox_changed_cb (GtkComboBox *box,
                              CcUaPanel   *panel)
{
  CcUaPanelPrivate *priv = panel->priv;
  gchar *theme_name = NULL;
  GtkTreeIter iter;

  gtk_combo_box_get_active_iter (box, &iter);

  gtk_tree_model_get (gtk_combo_box_get_model (box), &iter,
                      CONTRAST_MODEL_THEME_COLUMN, &theme_name,
                      -1);

  if (g_strcmp0 (theme_name, ""))
    {
      g_settings_set_string (priv->interface_settings, GTK_THEME_KEY, theme_name);
      g_settings_set_string (priv->interface_settings, ICON_THEME_KEY, theme_name);
    }
  else
    {
      g_settings_reset (priv->interface_settings, GTK_THEME_KEY);
      g_settings_reset (priv->interface_settings, ICON_THEME_KEY);
    }

  g_free (theme_name);
}

static void
cc_ua_panel_set_shortcut_label (CcUaPanel  *self,
				const char *label,
				const char *key)
{
	GtkWidget *widget;
	char *value;
	char *text;
	guint accel_key, keycode;
	EggVirtualModifierType mods;

	widget = WID (self->priv->builder, label);
	value = g_settings_get_string (self->priv->mediakeys_settings, key);

	if (value == NULL || *value == '\0') {
		gtk_label_set_text (GTK_LABEL (widget), _("No shortcut set"));
		g_free (value);
		return;
	}
	if (egg_accelerator_parse_virtual (value, &accel_key, &keycode, &mods) == FALSE) {
		gtk_label_set_text (GTK_LABEL (widget), _("No shortcut set"));
		g_free (value);
		g_warning ("Failed to parse keyboard shortcut: '%s'", value);
		return;
	}
	g_free (value);

	text = egg_virtual_accelerator_label (accel_key, keycode, mods);
	gtk_label_set_text (GTK_LABEL (widget), text);
	g_free (text);
}

static void
cc_ua_panel_init_seeing (CcUaPanel *self)
{
  CcUaPanelPrivate *priv = self->priv;

  g_signal_connect (priv->font_settings, "changed", G_CALLBACK (dpi_notify_cb), self);

  g_signal_connect (WID (priv->builder, "seeing_contrast_combobox"), "changed",
                    G_CALLBACK (contrast_combobox_changed_cb), self);

  g_signal_connect (WID (priv->builder, "seeing_text_size_combobox"), "changed",
                    G_CALLBACK (dpi_combo_box_changed), self);

  g_settings_bind (priv->kb_settings, "togglekeys-enable",
                   WID (priv->builder, "seeing_enable_toggle_keys_checkbutton"), "active",
                   G_SETTINGS_BIND_DEFAULT);

  settings_on_off_editor_new (priv, priv->application_settings,
                              "screen-magnifier-enabled",
                              WID (priv->builder, "seeing_zoom_switch"),
                              NULL);

  cc_ua_panel_set_shortcut_label (self, "seeing_contrast_toggle_keybinding_label", "toggle-contrast");
  cc_ua_panel_set_shortcut_label (self, "seeing_increase_size_keybinding_label", "increase-text-size");
  cc_ua_panel_set_shortcut_label (self, "seeing_decrease_size_keybinding_label", "decrease-text-size");
  cc_ua_panel_set_shortcut_label (self, "seeing_zoom_enable_keybinding_label", "magnifier");
  cc_ua_panel_set_shortcut_label (self, "seeing_zoom_in_keybinding_label", "magnifier-zoom-in");
  cc_ua_panel_set_shortcut_label (self, "seeing_zoom_out_keybinding_label", "magnifier-zoom-out");
  cc_ua_panel_set_shortcut_label (self, "seeing_reader_enable_keybinding_label", "screenreader");
}


/* hearing/sound section */
static void
visual_bell_type_notify_cb (GConfClient *client,
                            guint        cnxn_id,
                            GConfEntry  *entry,
                            CcUaPanel   *panel)
{
  GtkWidget *widget;
  const gchar *value = gconf_value_get_string (entry->value);

  if (!strcmp ("frame_flash", value))
    widget = WID (panel->priv->builder, "hearing_flash_window_title_button");
  else
    widget = WID (panel->priv->builder, "hearing_flash_screen_button");

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
}

static void
visual_bell_type_toggle_cb (GtkWidget *button,
                            CcUaPanel *panel)
{
  const gchar *key = "/apps/metacity/general/visual_bell_type";
  gboolean window_title;

  window_title = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));

  if (window_title)
    gconf_client_set_string (panel->priv->client, key, "frame_flash", NULL);
  else
    gconf_client_set_string (panel->priv->client, key, "fullscreen", NULL);
}

static void
hearing_sound_preferences_clicked (GtkButton *button,
                                   CcUaPanel *panel)
{
  CcShell *shell;

  shell = cc_panel_get_shell (CC_PANEL (panel));
  cc_shell_set_active_panel_from_id (shell, "sound", NULL);
}

static void
cc_ua_panel_init_hearing (CcUaPanel *self)
{
  CcUaPanelPrivate *priv = self->priv;
  GtkWidget *w;
  GConfEntry *entry;
  guint id;

  gconf_client_add_dir (priv->client, "/apps/metacity/general",
                        GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

  w = WID (priv->builder, "hearing_visual_alerts_switch");
  gconf_on_off_peditor_new (priv, "/apps/metacity/general/visual_bell",
                            w, visual_alerts_section);

  /* visual bell type */
  id = gconf_client_notify_add (priv->client,
                                "/apps/metacity/general/visual_bell_type",
                                (GConfClientNotifyFunc)
                                visual_bell_type_notify_cb,
                                self, NULL, NULL);
  priv->notify_list = g_slist_prepend (priv->notify_list, GINT_TO_POINTER (id));

  /* set the initial value */
  entry = gconf_client_get_entry (priv->client,
                                  "/apps/metacity/general/visual_bell_type",
                                  NULL, TRUE, NULL);
  visual_bell_type_notify_cb (priv->client, 0, entry, self);

  g_signal_connect (WID (priv->builder, "hearing_flash_window_title_button"),
                    "toggled", G_CALLBACK (visual_bell_type_toggle_cb), self);

  /* test flash */
  g_signal_connect (WID (priv->builder, "hearing_test_flash_button"),
                    "clicked", G_CALLBACK (gdk_beep), NULL);

  g_signal_connect (WID (priv->builder, "hearing_sound_preferences_button"),
                    "clicked",
                    G_CALLBACK (hearing_sound_preferences_clicked), self);
}

/* typing/keyboard section */
static void
typing_keyboard_preferences_clicked (GtkButton *button,
                                     CcUaPanel *panel)
{
  CcShell *shell;

  shell = cc_panel_get_shell (CC_PANEL (panel));
  cc_shell_set_active_panel_from_id (shell, "keyboard", NULL);
}

static void
cc_ua_panel_typing_assistant_prefs_changed (GtkAppChooser *chooser,
					    const char    *name,
					    CcUaPanel     *self)
{
  g_settings_set_string (self->priv->mobility_settings, "exec", name);
}

static char *
cc_ua_panel_typing_assistant_add_app (GtkAppChooserButton *button,
				      const char          *name)
{
  GAppInfo *info;
  GDesktopAppInfo *desktop_info;
  char *retval;

  desktop_info = g_desktop_app_info_new (name);
  if (desktop_info == NULL)
    return NULL;
  info = G_APP_INFO (desktop_info);
  gtk_app_chooser_button_append_custom_item (GTK_APP_CHOOSER_BUTTON (button),
					     g_app_info_get_executable (info),
					     g_app_info_get_display_name (info),
					     g_app_info_get_icon (info));
  retval = g_strdup (g_app_info_get_executable (info));
  g_object_unref (info);

  return retval;
}

static void
cc_ua_panel_typing_assistant_prefs (GtkButton *button,
				    CcUaPanel *self)
{
  GtkWidget *chooser;
  GtkWidget *area;
  char *selected;
  GList *list;
  guint i;
  const char *items[] = {
    "caribou.desktop",
    "gnome-dasher.desktop",
    "dasher.desktop"
  };

  self->priv->typing_assistant_prefs = gtk_dialog_new_with_buttons (_("Select On-Screen Keyboard"),
								    GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (button))),
								    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
								    GTK_STOCK_OK,
								    GTK_RESPONSE_ACCEPT,
								    NULL);
  g_signal_connect_swapped (G_OBJECT (self->priv->typing_assistant_prefs), "response",
			    G_CALLBACK (gtk_widget_destroy), self->priv->typing_assistant_prefs);
  g_object_add_weak_pointer (G_OBJECT (self->priv->typing_assistant_prefs),
			     (void **) &self->priv->typing_assistant_prefs);

  /* Use a bogus mime-type so that no apps are added by default */
  chooser = gtk_app_chooser_button_new ("x-content/does-not-exist");
  area = gtk_dialog_get_content_area (GTK_DIALOG (self->priv->typing_assistant_prefs));
  gtk_box_pack_start (GTK_BOX (area), chooser, FALSE, FALSE, 12);
  g_object_set_data (G_OBJECT (self->priv->typing_assistant_prefs), "chooser", chooser);

  /* Add the items, and check whether we have any actual items */
  list = NULL;
  for (i = 0; i < G_N_ELEMENTS (items); i++)
    {
      char *added;

      added = cc_ua_panel_typing_assistant_add_app (GTK_APP_CHOOSER_BUTTON (chooser), items[i]);
      if (added != NULL)
        list = g_list_prepend (list, added);
    }
  list = g_list_reverse (list);

  if (list == NULL)
    {
      gtk_widget_destroy (self->priv->typing_assistant_prefs);
      g_debug ("Show an error here");
      return;
    }

  /* Select default item from GSettings, making sure the item exists */
  selected = g_settings_get_string (self->priv->mobility_settings, "exec");
  if (*selected == '\0' ||
      g_list_find_custom (list, selected, (GCompareFunc) g_strcmp0) == NULL)
    {
      g_free (selected);
      selected = NULL;
    }

  if (selected != NULL)
    gtk_app_chooser_button_set_active_custom_item (GTK_APP_CHOOSER_BUTTON (chooser), selected);

  g_signal_connect (G_OBJECT (chooser), "custom-item-activated",
		    G_CALLBACK (cc_ua_panel_typing_assistant_prefs_changed), self);

  /* Set the first item as the new default if the value was bogus */
  if (selected == NULL)
    {
      gtk_app_chooser_button_set_active_custom_item (GTK_APP_CHOOSER_BUTTON (chooser),
						     list->data);
    }
  g_free (selected);

  g_list_foreach (list, (GFunc) g_free, NULL);
  g_list_free (list);

  gtk_widget_show_all (self->priv->typing_assistant_prefs);
}

static void
cc_ua_panel_init_keyboard (CcUaPanel *self)
{
  CcUaPanelPrivate *priv = self->priv;
  GtkWidget *w;

  /* Typing assistant (on-screen keyboard) */
  w = WID (priv->builder, "typing_assistant_switch");
  settings_on_off_editor_new (priv, priv->application_settings, "screen-keyboard-enabled", w, typing_assistant_section);
  g_signal_connect (G_OBJECT (WID (priv->builder, "typing_assistant_preferences_button")), "clicked",
		    G_CALLBACK (cc_ua_panel_typing_assistant_prefs), self);

  /* enable shortcuts */
  w = WID (priv->builder, "typing_keyboard_toggle_checkbox");
  g_settings_bind (priv->kb_settings, "enable", w, "active", G_SETTINGS_BIND_DEFAULT);

  /* sticky keys */
  w = WID (priv->builder, "typing_sticky_keys_switch");
  settings_on_off_editor_new (priv, priv->kb_settings, "stickykeys-enable", w, sticky_keys_section);

  w = WID (priv->builder, "typing_sticky_keys_disable_two_keys_checkbutton");
  g_settings_bind (priv->kb_settings, "stickykeys-two-key-off", w, "active", G_SETTINGS_BIND_DEFAULT);

  w = WID (priv->builder, "typing_sticky_keys_beep_modifier_checkbutton");
  g_settings_bind (priv->kb_settings, "stickykeys-modifier-beep", w, "active", G_SETTINGS_BIND_DEFAULT);

  /* slow keys */
  w = WID (priv->builder, "typing_slow_keys_switch");
  settings_on_off_editor_new (priv, priv->kb_settings, "slowkeys-enable", w, slow_keys_section);

  w = WID (priv->builder, "typing_slowkeys_delay_scale");
  g_settings_bind (priv->kb_settings, "slowkeys-delay",
                   gtk_range_get_adjustment (GTK_RANGE (w)), "value",
                   G_SETTINGS_BIND_DEFAULT);

  w = WID (priv->builder, "typing_slow_keys_beep_pressed_checkbutton");
  g_settings_bind (priv->kb_settings, "slowkeys-beep-press", w, "active", G_SETTINGS_BIND_DEFAULT);

  w = WID (priv->builder, "typing_slow_keys_beep_accepted_checkbutton");
  g_settings_bind (priv->kb_settings, "slowkeys-beep-accept", w, "active", G_SETTINGS_BIND_DEFAULT);

  w = WID (priv->builder, "typing_slow_keys_beep_rejected_checkbutton");
  g_settings_bind (priv->kb_settings, "slowkeys-beep-reject", w, "active", G_SETTINGS_BIND_DEFAULT);

  /* bounce keys */
  w = WID (priv->builder, "typing_bounce_keys_switch");
  settings_on_off_editor_new (priv, priv->kb_settings, "bouncekeys-enable", w, bounce_keys_section);

  w = WID (priv->builder, "typing_bouncekeys_delay_scale");
  g_settings_bind (priv->kb_settings, "bouncekeys-delay",
                   gtk_range_get_adjustment (GTK_RANGE (w)), "value",
                   G_SETTINGS_BIND_DEFAULT);

  w = WID (priv->builder, "typing_bounce_keys_beep_rejected_checkbutton");
  g_settings_bind (priv->kb_settings, "bouncekeys-beep-reject", w, "active", G_SETTINGS_BIND_DEFAULT);

  g_signal_connect (WID (priv->builder, "typing_keyboard_preferences_button"),
                    "clicked",
                    G_CALLBACK (typing_keyboard_preferences_clicked), self);
}

/* mouse/pointing & clicking section */
static void
pointing_mouse_preferences_clicked_cb (GtkButton *button,
                                       CcUaPanel *panel)
{
  CcShell *shell;

  shell = cc_panel_get_shell (CC_PANEL (panel));
  cc_shell_set_active_panel_from_id (shell, "mouse", NULL);
}

static void
cc_ua_panel_init_mouse (CcUaPanel *self)
{
  CcUaPanelPrivate *priv = self->priv;
  GtkWidget *w;

  /* mouse keys */
  w = WID (priv->builder, "pointing_mouse_keys_switch");
  settings_on_off_editor_new (priv, priv->kb_settings, "mousekeys-enable", w, NULL);

  /* simulated secondary click */
  w = WID (priv->builder, "pointing_second_click_switch");
  settings_on_off_editor_new (priv, priv->mouse_settings, "secondary-click-enabled", w, secondary_click_section);

  w = WID (priv->builder, "pointing_secondary_click_delay_scale");
  g_settings_bind (priv->mouse_settings, "secondary-click-time",
                   gtk_range_get_adjustment (GTK_RANGE (w)), "value",
                   G_SETTINGS_BIND_DEFAULT);

  /* dwell click */
  w = WID (priv->builder, "pointing_hover_click_switch");
  settings_on_off_editor_new (priv, priv->mouse_settings, "dwell-click-enabled", w, dwell_click_section);

  w = WID (priv->builder, "pointing_dwell_delay_scale");
  g_settings_bind (priv->mouse_settings, "dwell-time",
                   gtk_range_get_adjustment (GTK_RANGE (w)), "value",
                   G_SETTINGS_BIND_DEFAULT);

  w = WID (priv->builder, "pointing_dwell_threshold_scale");
  g_settings_bind (priv->mouse_settings, "dwell-threshold",
                   gtk_range_get_adjustment (GTK_RANGE (w)), "value",
                   G_SETTINGS_BIND_DEFAULT);

  /* mouse preferences button */
  g_signal_connect (WID (priv->builder, "pointing_mouse_preferences_button"),
                    "clicked",
                    G_CALLBACK (pointing_mouse_preferences_clicked_cb), self);
}

static void
cc_ua_panel_init (CcUaPanel *self)
{
  CcUaPanelPrivate *priv;
  GtkWidget *widget;
  GError *err = NULL;
  gchar *objects[] = { "universal_access_box", "contrast_model",
                       "text_size_model", "slowkeys_delay_adjustment",
                       "bouncekeys_delay_adjustment", "click_delay_adjustment",
                       "dwell_time_adjustment", "dwell_threshold_adjustment",
                       "seeing_sizegroup", "typing_sizegroup",
                       "pointing_sizegroup", "pointing_sizegroup2",
                       "pointing_scale_sizegroup", "sizegroup1",
                       "hearing_sizegroup",
                       NULL };

  priv = self->priv = UA_PANEL_PRIVATE (self);

  priv->builder = gtk_builder_new ();

  gtk_builder_add_objects_from_file (priv->builder,
                                     GNOMECC_UI_DIR "/uap.ui",
                                     objects,
                                     &err);

  if (err)
    {
      g_warning ("Could not load interface file: %s", err->message);
      g_error_free (err);

      g_object_unref (priv->builder);
      priv->builder = NULL;

      return;
    }

  priv->client = gconf_client_get_default ();

  priv->interface_settings = g_settings_new ("org.gnome.desktop.interface");
  g_signal_connect (priv->interface_settings, "changed",
                    G_CALLBACK (interface_settings_changed_cb), self);

  priv->kb_settings = g_settings_new ("org.gnome.desktop.a11y.keyboard");
  priv->mouse_settings = g_settings_new ("org.gnome.desktop.a11y.mouse");
  priv->font_settings = g_settings_new ("org.gnome.settings-daemon.plugins.xsettings");
  priv->application_settings = g_settings_new ("org.gnome.desktop.a11y.applications");
  priv->mediakeys_settings = g_settings_new ("org.gnome.settings-daemon.plugins.media-keys");
  priv->mobility_settings = g_settings_new ("org.gnome.desktop.default-applications.at.mobility");

  cc_ua_panel_init_keyboard (self);
  cc_ua_panel_init_mouse (self);
  cc_ua_panel_init_hearing (self);
  cc_ua_panel_init_seeing (self);

  widget = (GtkWidget*) gtk_builder_get_object (priv->builder,
                                                "universal_access_box");

  gtk_container_add (GTK_CONTAINER (self), widget);
}

void
cc_ua_panel_register (GIOModule *module)
{
  cc_ua_panel_register_type (G_TYPE_MODULE (module));
  g_io_extension_point_implement (CC_SHELL_PANEL_EXTENSION_POINT,
                                  CC_TYPE_UA_PANEL,
                                  "universal-access", 0);
}

