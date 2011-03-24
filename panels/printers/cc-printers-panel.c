/*
 * Copyright (C) 2010 Red Hat, Inc
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

#include <config.h>

#include "cc-printers-panel.h"

#include <string.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <polkit/polkit.h>
#include <dbus/dbus-glib.h>
#include <gdesktop-enums.h>

#include <cups/cups.h>

#include <math.h>

#include "libgnome-control-center/cc-lockbutton.h"
#include "pp-new-printer-dialog.h"
#include "pp-utils.h"

G_DEFINE_DYNAMIC_TYPE (CcPrintersPanel, cc_printers_panel, CC_TYPE_PANEL)

#define PRINTERS_PANEL_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CC_TYPE_PRINTERS_PANEL, CcPrintersPanelPrivate))

#define MECHANISM_BUS "org.opensuse.CupsPkHelper.Mechanism"

#define SUPPLY_BAR_HEIGHT 20

#define EMPTY_TEXT "\xe2\x80\x94"

#define CLOCK_SCHEMA "org.gnome.desktop.interface"
#define CLOCK_FORMAT_KEY "clock-format"

struct _CcPrintersPanelPrivate
{
  GtkBuilder *builder;

  GtkWidget *lock_button;

  cups_dest_t *dests;
  gchar **dest_model_names;
  int num_dests;
  int current_dest;

  cups_job_t *jobs;
  int num_jobs;
  int current_job;

  gchar **allowed_users;
  int num_allowed_users;
  int current_allowed_user;

  GdkRGBA background_color;

  GPermission *permission;

  GSettings *lockdown_settings;

  PpNewPrinterDialog *pp_new_printer_dialog;

  gpointer dummy;
};

static void actualize_jobs_list (CcPrintersPanel *self);
static void actualize_printers_list (CcPrintersPanel *self);
static void actualize_allowed_users_list (CcPrintersPanel *self);
static void printer_disable_cb (GObject *gobject, GParamSpec *pspec, gpointer user_data);
static void printer_set_default_cb (GtkToggleButton *button, gpointer user_data);

static void
cc_printers_panel_get_property (GObject    *object,
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
cc_printers_panel_set_property (GObject      *object,
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
cc_printers_panel_dispose (GObject *object)
{
  CcPrintersPanelPrivate *priv = CC_PRINTERS_PANEL (object)->priv;
  int                     i;

  if (priv->num_dests > 0)
    {
      for (i = 0; i < priv->num_dests; i++)
        g_free (priv->dest_model_names[i]);
      g_free (priv->dest_model_names);
      cupsFreeDests (priv->num_dests, priv->dests);
    }
  priv->dests = NULL;
  priv->num_dests = 0;
  priv->current_dest = -1;

  if (priv->num_jobs > 0)
    cupsFreeJobs (priv->num_jobs, priv->jobs);
  priv->jobs = NULL;
  priv->num_jobs = 0;
  priv->current_job = -1;

  if (priv->num_allowed_users > 0)
    {
      for (i = 0; i < priv->num_allowed_users; i++)
        g_free (priv->allowed_users[i]);
      g_free (priv->allowed_users);
    }
  priv->allowed_users = NULL;
  priv->num_allowed_users = 0;
  priv->current_allowed_user = -1;

  if (priv->builder)
    {
      g_object_unref (priv->builder);
      priv->builder = NULL;
    }

  if (priv->lockdown_settings)
    {
      g_object_unref (priv->lockdown_settings);
      priv->lockdown_settings = NULL;
    }

  G_OBJECT_CLASS (cc_printers_panel_parent_class)->dispose (object);
}

static void
cc_printers_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_printers_panel_parent_class)->finalize (object);
}

static void
cc_printers_panel_class_init (CcPrintersPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (CcPrintersPanelPrivate));

  object_class->get_property = cc_printers_panel_get_property;
  object_class->set_property = cc_printers_panel_set_property;
  object_class->dispose = cc_printers_panel_dispose;
  object_class->finalize = cc_printers_panel_finalize;
}

static void
cc_printers_panel_class_finalize (CcPrintersPanelClass *klass)
{
}

enum
{
  NOTEBOOK_INFO_PAGE = 0,
  NOTEBOOK_JOBS_PAGE,
  NOTEBOOK_OPTIONS_PAGE,
  NOTEBOOK_NO_PRINTERS_PAGE,
  NOTEBOOK_NO_CUPS_PAGE,
  NOTEBOOK_N_PAGES
};

enum
{
  PRINTER_ID_COLUMN,
  PRINTER_NAME_COLUMN,
  PRINTER_PAUSED_COLUMN,
  PRINTER_DEFAULT_ICON_COLUMN,
  PRINTER_ICON_COLUMN,
  PRINTER_N_COLUMNS
};

static void
printer_selection_changed_cb (GtkTreeSelection *selection,
                              gpointer          user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  GtkTreeModel           *model;
  GtkTreeIter             iter;
  GtkWidget              *widget;
  gboolean                test_page_command_available = FALSE;
  gboolean                sensitive;
  gboolean                is_local = TRUE;
  gchar                  *printer_make_and_model = NULL;
  gchar                  *printer_model = NULL;
  gchar                  *reason = NULL;
  gchar                 **available_commands = NULL;
  gchar                  *printer_commands = NULL;
  gchar                 **printer_reasons = NULL;
  gchar                  *marker_types = NULL;
  gchar                  *printer_name = NULL;
  gchar                  *printer_type = NULL;
  gchar                  *active_jobs = NULL;
  gchar                  *supply_type = NULL;
  gchar                  *printer_uri = NULL;
  gchar                  *location = NULL;
  gchar                  *status = NULL;
  guint                   num_jobs;
  int                     printer_state = 3;
  int                     id = -1;
  int                     i, j;
  static const char * const reasons[] =
    {
      "toner-low",
      "toner-empty",
      "developer-low",
      "developer-empty",
      "marker-supply-low",
      "marker-supply-empty",
      "cover-open",
      "door-open",
      "media-low",
      "media-empty",
      "offline",
      "paused",
      "marker-waste-almost-full",
      "marker-waste-full",
      "opc-near-eol",
      "opc-life-over"
    };
  static const char * statuses[] =
    {
      /* Translators: The printer is low on toner */
      N_("Low on toner"),
      /* Translators: The printer has no toner left */
      N_("Out of toner"),
      /* Translators: "Developer" is a chemical for photo development,
       * http://en.wikipedia.org/wiki/Photographic_developer */
      N_("Low on developer"),
      /* Translators: "Developer" is a chemical for photo development,
       * http://en.wikipedia.org/wiki/Photographic_developer */
      N_("Out of developer"),
      /* Translators: "marker" is one color bin of the printer */
      N_("Low on a marker supply"),
      /* Translators: "marker" is one color bin of the printer */
      N_("Out of a marker supply"),
      /* Translators: One or more covers on the printer are open */
      N_("Open cover"),
      /* Translators: One or more doors on the printer are open */
      N_("Open door"),
      /* Translators: At least one input tray is low on media */
      N_("Low on paper"),
      /* Translators: At least one input tray is empty */
      N_("Out of paper"),
      /* Translators: The printer is offline */
      NC_("printer state", "Offline"),
      /* Translators: Someone has paused the Printer */
      NC_("printer state", "Paused"),
      /* Translators: The printer marker supply waste receptacle is almost full */
      N_("Waste receptacle almost full"),
      /* Translators: The printer marker supply waste receptacle is full */
      N_("Waste receptacle full"),
      /* Translators: Optical photo conductors are used in laser printers */
      N_("The optical photo conductor is near end of life"),
      /* Translators: Optical photo conductors are used in laser printers */
      N_("The optical photo conductor is no longer functioning")
    };

  priv = PRINTERS_PANEL_PRIVATE (self);

  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      gtk_tree_model_get (model, &iter,
			  PRINTER_ID_COLUMN, &id,
			  PRINTER_NAME_COLUMN, &printer_name,
			  -1);
    }
  else
    id = -1;

  priv->current_dest = id;

  actualize_jobs_list (self);
  actualize_allowed_users_list (self);

  if (priv->current_dest >= 0 &&
      priv->current_dest < priv->num_dests &&
      priv->dests != NULL)
    {
      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "notebook");
      if (gtk_notebook_get_current_page (GTK_NOTEBOOK (widget)) >= NOTEBOOK_NO_PRINTERS_PAGE)
        gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), NOTEBOOK_INFO_PAGE);

      for (i = 0; i < priv->dests[id].num_options; i++)
        {
          if (g_strcmp0 (priv->dests[id].options[i].name, "printer-location") == 0)
            location = g_strdup (priv->dests[id].options[i].value);
          else if (g_strcmp0 (priv->dests[id].options[i].name, "printer-state") == 0)
            printer_state = atoi (priv->dests[id].options[i].value);
          else if (g_strcmp0 (priv->dests[id].options[i].name, "printer-state-reasons") == 0)
            reason = priv->dests[id].options[i].value;
          else if (g_strcmp0 (priv->dests[priv->current_dest].options[i].name, "marker-types") == 0)
            marker_types = priv->dests[priv->current_dest].options[i].value;
          else if (g_strcmp0 (priv->dests[priv->current_dest].options[i].name, "printer-commands") == 0)
            printer_commands = priv->dests[priv->current_dest].options[i].value;
          else if (g_strcmp0 (priv->dests[priv->current_dest].options[i].name, "printer-make-and-model") == 0)
            printer_make_and_model = priv->dests[priv->current_dest].options[i].value;
          else if (g_strcmp0 (priv->dests[priv->current_dest].options[i].name, "printer-uri-supported") == 0)
            printer_uri = priv->dests[priv->current_dest].options[i].value;
          else if (g_strcmp0 (priv->dests[priv->current_dest].options[i].name, "printer-type") == 0)
            printer_type = priv->dests[priv->current_dest].options[i].value;
        }

      if (priv->dest_model_names[priv->current_dest] == NULL)
        priv->dest_model_names[priv->current_dest] =
          get_ppd_attribute (priv->dests[priv->current_dest].name, "ModelName");

      printer_model = g_strdup (priv->dest_model_names[priv->current_dest]);

      if (printer_model == NULL && printer_make_and_model)
        {
          gchar *breakpoint = NULL, *tmp = NULL, *tmp2 = NULL;
          gchar  backup;
          size_t length = 0;
          gchar *forbiden[] = {
              "foomatic",
              ",",
              "hpijs",
              "hpcups",
              "(recommended)",
              "postscript (recommended)",
              NULL };

          tmp = g_ascii_strdown (printer_make_and_model, -1);

          for (i = 0; i < g_strv_length (forbiden); i++)
            {
              tmp2 = g_strrstr (tmp, forbiden[i]);
              if (breakpoint == NULL || 
                  (tmp2 != NULL && tmp2 < breakpoint))
                breakpoint = tmp2;
            }

          if (breakpoint)
            {
              backup = *breakpoint;
              *breakpoint = '\0';
              length = strlen (tmp);
              *breakpoint = backup;
              g_free (tmp);

              if (length > 0)
                printer_model = g_strndup (printer_make_and_model, length);
            }
          else
            printer_model = g_strdup (printer_make_and_model);
        }

      /* Find the first of the most severe reasons
       * and show it in the status field
       */
      if (reason && g_strcmp0 (reason, "none") != 0)
        {
          int errors = 0, warnings = 0, reports = 0;
          int error_index = -1, warning_index = -1, report_index = -1;

          printer_reasons = g_strsplit (reason, ",", -1);
          for (i = 0; i < g_strv_length (printer_reasons); i++)
            {
              for (j = 0; j < G_N_ELEMENTS (reasons); j++)
                if (strncmp (printer_reasons[i],
                             reasons[j],
                             strlen (reasons[j])) == 0)
                    {
                      if (g_str_has_suffix (printer_reasons[i], "-report"))
                        {
                          if (reports == 0)
                            report_index = j;
                          reports++;
                        }
                      else if (g_str_has_suffix (printer_reasons[i], "-warning"))
                        {
                          if (warnings == 0)
                            warning_index = j;
                          warnings++;
                        }
                      else
                        {
                          if (errors == 0)
                            error_index = j;
                          errors++;
                        }
                    }
            }
          g_strfreev (printer_reasons);

          if (error_index >= 0)
            status = g_strdup (_(statuses[error_index]));
          else if (warning_index >= 0)
            status = g_strdup (_(statuses[warning_index]));
          else if (report_index >= 0)
            status = g_strdup (_(statuses[report_index]));
        }

      if (status == NULL)
        {
          switch (printer_state)
            {
              case 3:
                /* Translators: Printer's state (can start new job without waiting) */
                status = g_strdup ( C_("printer state", "Ready"));
                break;
              case 4:
                /* Translators: Printer's state (jobs are processing) */
                status = g_strdup ( C_("printer state", "Processing"));
                break;
              case 5:
                /* Translators: Printer's state (no jobs can be processed) */
                status = g_strdup ( C_("printer state", "Stopped"));
                break;
            }
        }

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-name-label");

      if (printer_name)
        {
          gtk_label_set_text (GTK_LABEL (widget), printer_name);
          g_free (printer_name);
        }
      else
        gtk_label_set_text (GTK_LABEL (widget), EMPTY_TEXT);


      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-status-label");

      if (status)
        {
          gtk_label_set_text (GTK_LABEL (widget), status);
          g_free (status);
        }
      else
        gtk_label_set_text (GTK_LABEL (widget), EMPTY_TEXT);


      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-location-label");

      if (location)
        {
          gtk_label_set_text (GTK_LABEL (widget), location);
          g_free (location);
        }
      else
        gtk_label_set_text (GTK_LABEL (widget), EMPTY_TEXT);


      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-model-label");

      if (printer_model)
        {
          gtk_label_set_text (GTK_LABEL (widget), printer_model);
          g_free (printer_model);
        }
      else
        gtk_label_set_text (GTK_LABEL (widget), EMPTY_TEXT);


      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-ip-address-label");

      if (printer_type)
        {
          cups_ptype_t type;
          type = atoi (printer_type);
          is_local = !(type & (CUPS_PRINTER_REMOTE | CUPS_PRINTER_IMPLICIT));
        }

      if (is_local)
        {
          gtk_label_set_text (GTK_LABEL (widget), "localhost");
        }
      else
        {
          if (printer_uri)
            {
              char scheme[HTTP_MAX_URI],
              userpass[HTTP_MAX_URI],
              server[HTTP_MAX_URI],
              resource[HTTP_MAX_URI];
              int port;

              httpSeparateURI(HTTP_URI_CODING_ALL, printer_uri, scheme, sizeof(scheme), userpass,
                              sizeof(userpass), server, sizeof(server), &port, resource,
                              sizeof(resource));

              if (server[0] != '\0')
                gtk_label_set_text (GTK_LABEL (widget), server);
              else
                gtk_label_set_text (GTK_LABEL (widget), EMPTY_TEXT);
            }
          else
            gtk_label_set_text (GTK_LABEL (widget), EMPTY_TEXT);
        }


      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-disable-switch");

      g_signal_handlers_block_by_func (G_OBJECT (widget), printer_disable_cb, self);
      gtk_switch_set_active (GTK_SWITCH (widget), printer_state != 5);
      g_signal_handlers_unblock_by_func (G_OBJECT (widget), printer_disable_cb, self);


      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-default-check-button");

      sensitive = gtk_widget_get_sensitive (widget);
      g_signal_handlers_block_by_func (G_OBJECT (widget), printer_set_default_cb, self);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), priv->dests[id].is_default);
      g_signal_handlers_unblock_by_func (G_OBJECT (widget), printer_set_default_cb, self);
      gtk_widget_set_sensitive (widget, sensitive);


      if (printer_commands)
        {
          available_commands = g_strsplit (printer_commands, ",", -1);
          for (i = 0; i < g_strv_length (available_commands); i++)
            {
              if (g_strcmp0 (available_commands[i], "PrintSelfTestPage") == 0)
                test_page_command_available = TRUE;
            }
          g_strfreev (available_commands);
        }

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "print-test-page-button");
      gtk_widget_set_sensitive (widget, test_page_command_available);


      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "supply-drawing-area");
      gtk_widget_set_size_request (widget, -1, SUPPLY_BAR_HEIGHT);
      gtk_widget_queue_draw (widget);


      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "supply-label");

      if (marker_types && g_strrstr (marker_types, "toner") != NULL)
        /* Translators: Toner supply */
        supply_type = g_strdup ( _("Toner Level"));
      else if (marker_types && g_strrstr (marker_types, "ink") != NULL)
        /* Translators: Ink supply */
        supply_type = g_strdup ( _("Ink Level"));
      else
        /* Translators: By supply we mean ink, toner, staples, water, ... */
        supply_type = g_strdup ( _("Supply Level"));

      if (supply_type)
        {
          gtk_label_set_text (GTK_LABEL (widget), supply_type);
          g_free (supply_type);
        }
      else
        gtk_label_set_text (GTK_LABEL (widget), EMPTY_TEXT);


      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-jobs-label");
      num_jobs = priv->num_jobs < 0 ? 0 : (guint) priv->num_jobs;
      /* Translators: there is n active print jobs on this printer */
      active_jobs = g_strdup_printf (ngettext ("%u active", "%u active", num_jobs), num_jobs);

      if (active_jobs)
        {
          gtk_label_set_text (GTK_LABEL (widget), active_jobs);
          g_free (active_jobs);
        }
      else
        gtk_label_set_text (GTK_LABEL (widget), EMPTY_TEXT);


      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-options-button");
      gtk_widget_set_sensitive (widget, TRUE);

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-jobs-button");
      gtk_widget_set_sensitive (widget, TRUE);

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-icon");
      gtk_widget_set_sensitive (widget, TRUE);
    }
  else
    {
      http_t *http = NULL;

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "notebook");

      http = httpConnectEncrypt (cupsServer (), ippPort (), cupsEncryption ());
      if (http)
        {
          httpClose (http);
          gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), NOTEBOOK_NO_PRINTERS_PAGE);
        }
      else
        gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), NOTEBOOK_NO_CUPS_PAGE);

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-name-label");
      gtk_label_set_text (GTK_LABEL (widget), "");

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-status-label");
      gtk_label_set_text (GTK_LABEL (widget), "");

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-location-label");
      gtk_label_set_text (GTK_LABEL (widget), "");

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-model-label");
      gtk_label_set_text (GTK_LABEL (widget), "");

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-ip-address-label");
      gtk_label_set_text (GTK_LABEL (widget), "");

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-jobs-label");
      gtk_label_set_text (GTK_LABEL (widget), "");

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "print-test-page-button");
      gtk_widget_set_sensitive (widget, FALSE);

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-options-button");
      gtk_widget_set_sensitive (widget, FALSE);

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-jobs-button");
      gtk_widget_set_sensitive (widget, FALSE);

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-icon");
      gtk_widget_set_sensitive (widget, FALSE);
    }

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "job-release-button");
  gtk_widget_set_sensitive (widget, FALSE);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "job-hold-button");
  gtk_widget_set_sensitive (widget, FALSE);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "job-cancel-button");
  gtk_widget_set_sensitive (widget, FALSE);
}

static void
actualize_printers_list (CcPrintersPanel *self)
{
  CcPrintersPanelPrivate *priv;
  GtkListStore           *store;
  GtkTreeIter             selected_iter;
  GtkTreeView            *treeview;
  GtkTreeIter             iter;
  cups_job_t             *jobs = NULL;
  gboolean                paused = FALSE;
  gboolean                valid = FALSE;
  gchar                  *current_printer_instance = NULL;
  gchar                  *current_printer_name = NULL;
  gchar                  *printer_icon_name = NULL;
  gchar                  *default_icon_name = NULL;
  int                     current_dest = -1;
  int                     i, j;
  int                     num_jobs = 0;

  priv = PRINTERS_PANEL_PRIVATE (self);

  if (priv->current_dest >= 0 &&
      priv->current_dest < priv->num_dests &&
      priv->dests != NULL)
    {
      current_printer_name = g_strdup (priv->dests[priv->current_dest].name);
      if (priv->dests[priv->current_dest].instance)
        current_printer_instance = g_strdup (priv->dests[priv->current_dest].instance);
    }

  if (priv->num_dests > 0)
    {
      for (i = 0; i < priv->num_dests; i++)
        g_free (priv->dest_model_names[i]);
      g_free (priv->dest_model_names);
      cupsFreeDests (priv->num_dests, priv->dests);
    }
  priv->num_dests = cupsGetDests (&priv->dests);
  priv->dest_model_names = g_new0 (gchar *, priv->num_dests);
  priv->current_dest = -1;

  treeview = (GtkTreeView*)
    gtk_builder_get_object (priv->builder, "printers-treeview");

  store = gtk_list_store_new (PRINTER_N_COLUMNS,
                              G_TYPE_INT,
                              G_TYPE_STRING,
                              G_TYPE_BOOLEAN,
                              G_TYPE_STRING,
                              G_TYPE_STRING);

  if (priv->num_dests == 0)
    {
      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter,
                          PRINTER_ID_COLUMN, 0,
      /* Translators: There are no printers available (none is configured or CUPS is not running) */
                          PRINTER_NAME_COLUMN, _("No printers available"),
                          PRINTER_PAUSED_COLUMN, TRUE,
                          PRINTER_DEFAULT_ICON_COLUMN, NULL,
                          PRINTER_ICON_COLUMN, NULL,
                          -1);
      gtk_widget_set_sensitive (GTK_WIDGET (treeview), FALSE);
    }
  else
    gtk_widget_set_sensitive (GTK_WIDGET (treeview), TRUE);

  for (i = 0; i < priv->num_dests; i++)
    {
      gchar *instance;

      gtk_list_store_append (store, &iter);

      if (priv->dests[i].instance)
        {
          instance = g_strdup_printf ("%s / %s", priv->dests[i].name, priv->dests[i].instance);

          if (current_printer_instance &&
              g_strcmp0 (current_printer_name, priv->dests[i].name) == 0 &&
              g_strcmp0 (current_printer_instance, priv->dests[i].instance) == 0)
            {
              current_dest = i;
              selected_iter = iter;
            }
        }
      else
        {
          instance = g_strdup (priv->dests[i].name);

          if (current_printer_instance == NULL &&
              g_strcmp0 (current_printer_name, priv->dests[i].name) == 0)
            {
              current_dest = i;
              selected_iter = iter;
            }
        }

      for (j = 0; j < priv->dests[i].num_options; j++)
        {
          if (g_strcmp0 (priv->dests[i].options[j].name, "printer-state") == 0)
            paused = (g_strcmp0 (priv->dests[i].options[j].value, "5") == 0);
        }

      if (priv->dests[i].is_default)
        default_icon_name = g_strdup ("emblem-default-symbolic");
      else
        default_icon_name = NULL;

      printer_icon_name = g_strdup ("printer");

      gtk_list_store_set (store, &iter,
                          PRINTER_ID_COLUMN, i,
                          PRINTER_NAME_COLUMN, instance,
                          PRINTER_PAUSED_COLUMN, paused,
                          PRINTER_DEFAULT_ICON_COLUMN, default_icon_name,
                          PRINTER_ICON_COLUMN, printer_icon_name,
                          -1);

      g_free (instance);
      g_free (printer_icon_name);
      g_free (default_icon_name);
    }

  gtk_tree_view_set_model (treeview, GTK_TREE_MODEL (store));

  if (current_dest >= 0)
    {
      priv->current_dest = current_dest;
      gtk_tree_selection_select_iter (
        gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview)),
        &selected_iter);
    }
  else
    {
      num_jobs = cupsGetJobs (&jobs, NULL, 1, CUPS_WHICHJOBS_ALL);

      /* Select last used printer */
      if (num_jobs > 0)
        {
          for (i = 0; i < priv->num_dests; i++)
            if (g_strcmp0 (priv->dests[i].name, jobs[num_jobs - 1].dest) == 0)
              {
                priv->current_dest = i;
                break;
              }
          cupsFreeJobs (num_jobs, jobs);
        }

      /* Select default printer */
      if (priv->current_dest < 0)
        {
          for (i = 0; i < priv->num_dests; i++)
            if (priv->dests[i].is_default)
              {
                priv->current_dest = i;
                break;
              }
        }

      if (priv->current_dest >= 0)
        {
          gint id;
          valid = gtk_tree_model_get_iter_first ((GtkTreeModel *) store,
                                                 &selected_iter);

          while (valid)
            {
              gtk_tree_model_get ((GtkTreeModel *) store, &selected_iter,
                                  PRINTER_ID_COLUMN, &id,
                                  -1);
              if (id == priv->current_dest)
                break;

              valid = gtk_tree_model_iter_next ((GtkTreeModel *) store,
                                                &selected_iter);
            }

          gtk_tree_selection_select_iter (
            gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview)),
            &selected_iter);
        }
      else if (priv->num_dests > 0)
        {
          /* Select first printer */
          gtk_tree_model_get_iter_first ((GtkTreeModel *) store,
                                         &selected_iter);

          gtk_tree_selection_select_iter (
            gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview)),
            &selected_iter);
        }
    }

  g_free (current_printer_name);
  g_free (current_printer_instance);
  g_object_unref (store);
}

static void
set_cell_sensitivity_func (GtkTreeViewColumn *tree_column,
                           GtkCellRenderer   *cell,
                           GtkTreeModel      *tree_model,
                           GtkTreeIter       *iter,
                           gpointer           func_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) func_data;
  gboolean                paused = FALSE;

  priv = PRINTERS_PANEL_PRIVATE (self);

  gtk_tree_model_get (tree_model, iter, PRINTER_PAUSED_COLUMN, &paused, -1);

  if (priv->num_dests == 0)
    g_object_set (G_OBJECT (cell),
                  "ellipsize", PANGO_ELLIPSIZE_NONE,
                  "width-chars", -1,
                  NULL);
  else
    g_object_set (G_OBJECT (cell),
                  "ellipsize", PANGO_ELLIPSIZE_END,
                  "width-chars", 18,
                  NULL);

  if (paused)
    g_object_set (cell,
                  "sensitive", FALSE,
                  NULL);
  else
    g_object_set (cell,
                  "sensitive", TRUE,
                  NULL);
}

static void
populate_printers_list (CcPrintersPanel *self)
{
  CcPrintersPanelPrivate *priv;
  GtkTreeViewColumn      *column;
  GtkCellRenderer        *icon_renderer;
  GtkCellRenderer        *icon_renderer2;
  GtkCellRenderer        *renderer;
  GtkWidget              *treeview;

  priv = PRINTERS_PANEL_PRIVATE (self);

  treeview = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "printers-treeview");

  g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview)),
                    "changed", G_CALLBACK (printer_selection_changed_cb), self);

  actualize_printers_list (self);


  icon_renderer = gtk_cell_renderer_pixbuf_new ();
  g_object_set (icon_renderer, "stock-size", GTK_ICON_SIZE_DND, NULL);
  column = gtk_tree_view_column_new_with_attributes ("Icon", icon_renderer,
                                                     "icon-name", PRINTER_ICON_COLUMN, NULL);
  gtk_tree_view_column_set_expand (column, FALSE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);


  renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (renderer), "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 18, NULL);
  column = gtk_tree_view_column_new_with_attributes ("Printer", renderer,
                                                     "text", PRINTER_NAME_COLUMN, NULL);
  gtk_tree_view_column_set_cell_data_func (column, renderer, set_cell_sensitivity_func,
                                           self, NULL);
  gtk_tree_view_column_set_expand (column, FALSE);
  gtk_tree_view_column_set_min_width (column, 120);
  gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);


  icon_renderer2 = gtk_cell_renderer_pixbuf_new ();
  column = gtk_tree_view_column_new_with_attributes ("Default", icon_renderer2,
                                                     "icon-name", PRINTER_DEFAULT_ICON_COLUMN, NULL);
  gtk_tree_view_column_set_expand (column, FALSE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
}

enum
{
  JOB_ID_COLUMN,
  JOB_TITLE_COLUMN,
  JOB_STATE_COLUMN,
  JOB_CREATION_TIME_COLUMN,
  JOB_N_COLUMNS
};

static void
actualize_jobs_list (CcPrintersPanel *self)
{
  CcPrintersPanelPrivate *priv;
  GtkListStore           *store;
  GtkTreeView            *treeview;
  GtkTreeIter             iter;
  GSettings              *settings;
  int                     i;

  priv = PRINTERS_PANEL_PRIVATE (self);

  treeview = (GtkTreeView*)
    gtk_builder_get_object (priv->builder, "job-treeview");

  if (priv->num_jobs > 0)
    cupsFreeJobs (priv->num_jobs, priv->jobs);
  priv->num_jobs = -1;
  priv->jobs = NULL;

  priv->current_job = -1;
  if (priv->current_dest >= 0 &&
      priv->current_dest < priv->num_dests &&
      priv->dests != NULL)
    priv->num_jobs = cupsGetJobs (&priv->jobs, priv->dests[priv->current_dest].name, 1, CUPS_WHICHJOBS_ACTIVE);

  store = gtk_list_store_new (JOB_N_COLUMNS, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

  for (i = 0; i < priv->num_jobs; i++)
    {
      GDesktopClockFormat  value;
      GDateTime           *time;
      struct tm *ts;
      gchar     *time_string;
      gchar     *state = NULL;

      ts = localtime (&(priv->jobs[i].creation_time));
      time = g_date_time_new_local (ts->tm_year,
                                    ts->tm_mon,
                                    ts->tm_mday,
                                    ts->tm_hour,
                                    ts->tm_min,
                                    ts->tm_sec);

      settings = g_settings_new (CLOCK_SCHEMA);
      value = g_settings_get_enum (settings, CLOCK_FORMAT_KEY);

      if (value == G_DESKTOP_CLOCK_FORMAT_24H)
        time_string = g_date_time_format (time, "%k:%M");
      else
        time_string = g_date_time_format (time, "%l:%M %p");

      g_date_time_unref (time);

      switch (priv->jobs[i].state)
        {
          case IPP_JOB_PENDING:
            /* Translators: Job's state (job is waiting to be printed) */
            state = g_strdup (C_("print job", "Pending"));
            break;
          case IPP_JOB_HELD:
            /* Translators: Job's state (job is held for printing) */
            state = g_strdup (C_("print job", "Held"));
            break;
          case IPP_JOB_PROCESSING:
            /* Translators: Job's state (job is currently printing) */
            state = g_strdup (C_("print job", "Processing"));
            break;
          case IPP_JOB_STOPPED:
            /* Translators: Job's state (job has been stopped) */
            state = g_strdup (C_("print job", "Stopped"));
            break;
          case IPP_JOB_CANCELED:
            /* Translators: Job's state (job has been canceled) */
            state = g_strdup (C_("print job", "Canceled"));
            break;
          case IPP_JOB_ABORTED:
            /* Translators: Job's state (job has aborted due to error) */
            state = g_strdup (C_("print job", "Aborted"));
            break;
          case IPP_JOB_COMPLETED:
            /* Translators: Job's state (job has completed successfully) */
            state = g_strdup (C_("print job", "Completed"));
            break;
        }

      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter,
                          JOB_ID_COLUMN, i,
                          JOB_TITLE_COLUMN, priv->jobs[i].title,
                          JOB_STATE_COLUMN, state,
                          JOB_CREATION_TIME_COLUMN, time_string,
                          -1);

      g_free (time_string);
      g_free (state);
    }

  gtk_tree_view_set_model (treeview, GTK_TREE_MODEL (store));
  g_object_unref (store);
}

static void
job_selection_changed_cb (GtkTreeSelection *selection,
                          gpointer          user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  GtkTreeModel           *model;
  GtkTreeIter             iter;
  GtkWidget              *widget;
  int                     id = -1;

  priv = PRINTERS_PANEL_PRIVATE (self);

  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    gtk_tree_model_get (model, &iter,
			JOB_ID_COLUMN, &id,
			-1);
  else
    id = -1;

  priv->current_job = id;

  if (priv->current_job >= 0 &&
      priv->current_job < priv->num_jobs &&
      priv->jobs != NULL)
    {
      ipp_jstate_t job_state = priv->jobs[priv->current_job].state;

      if (job_state == IPP_JOB_HELD)
        {
          widget = (GtkWidget*)
            gtk_builder_get_object (priv->builder, "job-release-button");
          gtk_widget_set_sensitive (widget, TRUE);
        }

      if (job_state == IPP_JOB_PENDING)
        {
          widget = (GtkWidget*)
            gtk_builder_get_object (priv->builder, "job-hold-button");
          gtk_widget_set_sensitive (widget, TRUE);
        }

      if (job_state < IPP_JOB_CANCELED)
        {
          widget = (GtkWidget*)
            gtk_builder_get_object (priv->builder, "job-cancel-button");
          gtk_widget_set_sensitive (widget, TRUE);
        }
    }
}

static void
populate_jobs_list (CcPrintersPanel *self)
{

  CcPrintersPanelPrivate *priv;
  GtkTreeViewColumn      *column;
  GtkCellRenderer        *renderer;
  GtkCellRenderer        *title_renderer;
  GtkTreeView            *treeview;

  priv = PRINTERS_PANEL_PRIVATE (self);

  actualize_jobs_list (self);

  treeview = (GtkTreeView*)
    gtk_builder_get_object (priv->builder, "job-treeview");

  renderer = gtk_cell_renderer_text_new ();
  title_renderer = gtk_cell_renderer_text_new ();

  /* Translators: Name of column showing titles of print jobs */
  column = gtk_tree_view_column_new_with_attributes (_("Job Title"), title_renderer,
                                                     "text", JOB_TITLE_COLUMN, NULL);
  g_object_set (G_OBJECT (title_renderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  gtk_tree_view_column_set_fixed_width (column, 180);
  gtk_tree_view_column_set_min_width (column, 180);
  gtk_tree_view_column_set_max_width (column, 180);
  gtk_tree_view_append_column (treeview, column);

  /* Translators: Name of column showing statuses of print jobs */
  column = gtk_tree_view_column_new_with_attributes (_("Job State"), renderer,
                                                     "text", JOB_STATE_COLUMN, NULL);
  gtk_tree_view_column_set_expand (column, TRUE);
  gtk_tree_view_append_column (treeview, column);

  /* Translators: Name of column showing times of creation of print jobs */
  column = gtk_tree_view_column_new_with_attributes (_("Time"), renderer,
                                                     "text", JOB_CREATION_TIME_COLUMN, NULL);
  gtk_tree_view_column_set_expand (column, TRUE);
  gtk_tree_view_append_column (treeview, column);

  g_signal_connect (gtk_tree_view_get_selection (treeview),
                    "changed", G_CALLBACK (job_selection_changed_cb), self);
}

enum
{
  ALLOWED_USERS_ID_COLUMN,
  ALLOWED_USERS_NAME_COLUMN,
  ALLOWED_USERS_N_COLUMNS
};

static void
actualize_allowed_users_list (CcPrintersPanel *self)
{
  CcPrintersPanelPrivate *priv;
  GtkListStore           *store;
  GtkTreeView            *treeview;
  GtkTreeIter             iter;
  int                     i;

  priv = PRINTERS_PANEL_PRIVATE (self);

  treeview = (GtkTreeView*)
    gtk_builder_get_object (priv->builder, "allowed-users-treeview");

  if (priv->allowed_users)
    {
      for (i = 0; i < priv->num_allowed_users; i++)
        g_free (priv->allowed_users[i]);
      g_free (priv->allowed_users);
      priv->allowed_users = NULL;
      priv->num_allowed_users = 0;
    }

  priv->current_allowed_user = -1;

  if (priv->current_dest >= 0 &&
      priv->current_dest < priv->num_dests &&
      priv->dests != NULL)
    priv->num_allowed_users = ccGetAllowedUsers (&priv->allowed_users, priv->dests[priv->current_dest].name);

  store = gtk_list_store_new (ALLOWED_USERS_N_COLUMNS, G_TYPE_INT, G_TYPE_STRING);

  for (i = 0; i < priv->num_allowed_users; i++)
    {
      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter,
                          ALLOWED_USERS_ID_COLUMN, i,
                          ALLOWED_USERS_NAME_COLUMN, priv->allowed_users[i],
                          -1);
    }

  gtk_tree_view_set_model (treeview, GTK_TREE_MODEL (store));
  g_object_unref (store);
}

static void
allowed_users_selection_changed_cb (GtkTreeSelection *selection,
                                    gpointer          user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  GtkTreeModel           *model;
  GtkTreeIter             iter;
  int                     id = -1;

  priv = PRINTERS_PANEL_PRIVATE (self);

  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    gtk_tree_model_get (model, &iter,
			ALLOWED_USERS_ID_COLUMN, &id,
			-1);
  else
    id = -1;

  priv->current_allowed_user = id;
}

static void
populate_allowed_users_list (CcPrintersPanel *self)
{

  CcPrintersPanelPrivate *priv;
  GtkTreeViewColumn      *column;
  GtkCellRenderer        *renderer;
  GtkTreeView            *treeview;

  priv = PRINTERS_PANEL_PRIVATE (self);

  actualize_allowed_users_list (self);

  treeview = (GtkTreeView*)
    gtk_builder_get_object (priv->builder, "allowed-users-treeview");

  gtk_tree_view_set_headers_visible (treeview, FALSE);

  renderer = gtk_cell_renderer_text_new ();

  column = gtk_tree_view_column_new_with_attributes (NULL, renderer,
                                                     "text", ALLOWED_USERS_NAME_COLUMN, NULL);
  gtk_tree_view_append_column (treeview, column);

  g_signal_connect (gtk_tree_view_get_selection (treeview),
                    "changed", G_CALLBACK (allowed_users_selection_changed_cb), self);
}

static void
job_process_cb (GtkButton *button,
                gpointer   user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  DBusGProxy             *proxy;
  GtkWidget              *widget;
  GError                 *error = NULL;
  char                   *ret_error = NULL;
  int                     id = -1;

  priv = PRINTERS_PANEL_PRIVATE (self);

  if (priv->current_job >= 0 &&
      priv->current_job < priv->num_jobs &&
      priv->jobs != NULL)
    id = priv->jobs[priv->current_job].id;

  if (id >= 0)
    {
      proxy = get_dbus_proxy (MECHANISM_BUS,
                              "/",
                              MECHANISM_BUS,
                              TRUE);

      if (!proxy)
        return;

      if ((GtkButton*) gtk_builder_get_object (priv->builder,
                                               "job-cancel-button") ==
          button)
        dbus_g_proxy_call (proxy, "JobCancelPurge", &error,
                           G_TYPE_INT, id,
                           G_TYPE_BOOLEAN, FALSE,
                           G_TYPE_INVALID,
                           G_TYPE_STRING, &ret_error,
                           G_TYPE_INVALID);
      else if ((GtkButton*) gtk_builder_get_object (priv->builder,
                                                        "job-hold-button") ==
               button)
        dbus_g_proxy_call (proxy, "JobSetHoldUntil", &error,
                           G_TYPE_INT, id,
                           G_TYPE_STRING, "indefinite",
                           G_TYPE_INVALID,
                           G_TYPE_STRING, &ret_error,
                           G_TYPE_INVALID);
      else if ((GtkButton*) gtk_builder_get_object (priv->builder,
                                                        "job-release-button") ==
               button)
        dbus_g_proxy_call (proxy, "JobSetHoldUntil", &error,
                           G_TYPE_INT, id,
                           G_TYPE_STRING, "no-hold",
                           G_TYPE_INVALID,
                           G_TYPE_STRING, &ret_error,
                           G_TYPE_INVALID);

      g_object_unref (proxy);

      if (error || (ret_error && ret_error[0] != '\0'))
        {
          if (error)
            g_warning ("%s", error->message);

          if (ret_error && ret_error[0] != '\0')
            g_warning ("%s", ret_error);
        }
      else
        actualize_jobs_list (self);

      g_clear_error (&error);
  }

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "job-release-button");
  gtk_widget_set_sensitive (widget, FALSE);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "job-hold-button");
  gtk_widget_set_sensitive (widget, FALSE);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "job-cancel-button");
  gtk_widget_set_sensitive (widget, FALSE);
}

static void
printer_disable_cb (GObject    *gobject,
                    GParamSpec *pspec,
                    gpointer    user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  DBusGProxy             *proxy;
  gboolean                paused = FALSE;
  GError                 *error = NULL;
  char                   *ret_error = NULL;
  char                   *name = NULL;
  int                     i;

  priv = PRINTERS_PANEL_PRIVATE (self);

  if (priv->current_dest >= 0 &&
      priv->current_dest < priv->num_dests &&
      priv->dests != NULL)
    {
      name = priv->dests[priv->current_dest].name;

      for (i = 0; i < priv->dests[priv->current_dest].num_options; i++)
        {
          if (g_strcmp0 (priv->dests[priv->current_dest].options[i].name, "printer-state") == 0)
            paused = (g_strcmp0 (priv->dests[priv->current_dest].options[i].value, "5") == 0);
        }
    }

  if (name)
    {
      proxy = get_dbus_proxy (MECHANISM_BUS,
                              "/",
                              MECHANISM_BUS,
                              TRUE);

      if (!proxy)
        return;

      dbus_g_proxy_call (proxy, "PrinterSetEnabled", &error,
                         G_TYPE_STRING, name,
                         G_TYPE_BOOLEAN, paused,
                         G_TYPE_INVALID,
                         G_TYPE_STRING, &ret_error,
                         G_TYPE_INVALID);

      g_object_unref (proxy);

      if (error || (ret_error && ret_error[0] != '\0'))
        {
          if (error)
            g_warning ("%s", error->message);

          if (ret_error && ret_error[0] != '\0')
            g_warning ("%s", ret_error);
        }
      else
        actualize_printers_list (self);

      g_clear_error (&error);
    }
}

typedef struct {
  gchar *color;
  gchar *type;
  gchar *name;
  gint   level;
} MarkerItem;

static gint
markers_cmp (gconstpointer a,
             gconstpointer b)
{
  MarkerItem *x = (MarkerItem*) a;
  MarkerItem *y = (MarkerItem*) b;

  if (x->level < y->level)
    return 1;
  else if (x->level == y->level)
    return 0;
  else
    return -1;
}

static void
rounded_rectangle (cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path (cr);
    cairo_arc (cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_arc (cr, x + w - r, y + r, r, 3 *M_PI / 2, 2 * M_PI);
    cairo_arc (cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc (cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_close_path (cr);
}

static gboolean
supply_levels_draw_cb (GtkWidget *widget,
                       cairo_t *cr,
                       gpointer user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  GtkStyleContext        *context;
  gchar                  *marker_levels = NULL;
  gchar                  *marker_colors = NULL;
  gchar                  *marker_names = NULL;
  gchar                  *marker_types = NULL;
  gchar                  *tooltip_text = NULL;
  gint                    width;
  gint                    height;
  int                     i;

  priv = PRINTERS_PANEL_PRIVATE (self);

  width = gtk_widget_get_allocated_width (widget);
  height = gtk_widget_get_allocated_height (widget);

  cairo_rectangle (cr, 0.0, 0.0, width, height);
  gdk_cairo_set_source_rgba (cr, &priv->background_color);
  cairo_fill (cr);

  if (priv->current_dest >= 0 &&
      priv->current_dest < priv->num_dests &&
      priv->dests != NULL)
    {
      for (i = 0; i < priv->dests[priv->current_dest].num_options; i++)
        {
          if (g_strcmp0 (priv->dests[priv->current_dest].options[i].name, "marker-names") == 0)
            marker_names = g_strcompress (priv->dests[priv->current_dest].options[i].value);
          else if (g_strcmp0 (priv->dests[priv->current_dest].options[i].name, "marker-levels") == 0)
            marker_levels = priv->dests[priv->current_dest].options[i].value;
          else if (g_strcmp0 (priv->dests[priv->current_dest].options[i].name, "marker-colors") == 0)
            marker_colors = priv->dests[priv->current_dest].options[i].value;
          else if (g_strcmp0 (priv->dests[priv->current_dest].options[i].name, "marker-types") == 0)
            marker_types = priv->dests[priv->current_dest].options[i].value;
        }

      if (marker_levels && marker_colors && marker_names && marker_types)
        {
          GdkRGBA   border_color = {0.0, 0.0, 0.0, 1.0};
          GSList   *markers = NULL;
          GSList   *tmp_list = NULL;
          GValue    int_val = {0};
          gchar   **marker_levelsv = NULL;
          gchar   **marker_colorsv = NULL;
          gchar   **marker_namesv = NULL;
          gchar   **marker_typesv = NULL;
          gchar    *tmp = NULL;
          gint      border_radius = 3;

          context = gtk_widget_get_style_context ((GtkWidget *)
            gtk_builder_get_object (priv->builder, "printer-options-button"));
          gtk_style_context_get_border_color (context, 0, &border_color);
          gtk_style_context_get_property (
            context, GTK_STYLE_PROPERTY_BORDER_RADIUS, 0, &int_val);
          border_radius = g_value_get_int (&int_val);

          widget = (GtkWidget*)
            gtk_builder_get_object (priv->builder, "supply-drawing-area");

          marker_levelsv = g_strsplit (marker_levels, ",", -1);
          marker_colorsv = g_strsplit (marker_colors, ",", -1);
          marker_namesv = g_strsplit (marker_names, ",", -1);
          marker_typesv = g_strsplit (marker_types, ",", -1);
  
          for (i = 0; i < g_strv_length (marker_levelsv); i++)
            {
              MarkerItem *marker;

              if (g_strcmp0 (marker_typesv[i], "ink") == 0 ||
                  g_strcmp0 (marker_typesv[i], "toner") == 0)
                {
                  marker = g_new0 (MarkerItem, 1);
                  marker->type = g_strdup (marker_typesv[i]);
                  marker->name = g_strdup (marker_namesv[i]);
                  marker->color = g_strdup (marker_colorsv[i]);
                  marker->level = atoi (marker_levelsv[i]);

                  markers = g_slist_prepend (markers, marker);
                }
            }

          markers = g_slist_sort (markers, markers_cmp);

          for (tmp_list = markers; tmp_list; tmp_list = tmp_list->next)
            {
              GdkRGBA color = {0.0, 0.0, 0.0, 1.0};
              double  display_value;
              int     value;

              value = ((MarkerItem*) tmp_list->data)->level;

              gdk_rgba_parse (&color, ((MarkerItem*) tmp_list->data)->color);

              if (value > 0)
                {
                  display_value = value / 100.0 * (width - 3.0);
                  gdk_cairo_set_source_rgba (cr, &color);
                  rounded_rectangle (cr, 1.5, 1.5, display_value, SUPPLY_BAR_HEIGHT - 3.0, border_radius);
                  cairo_fill (cr);
                }

              if (tooltip_text)
                {
                  tmp = g_strdup_printf ("%s\n%s",
                                         tooltip_text,
                                         ((MarkerItem*) tmp_list->data)->name);
                  g_free (tooltip_text);
                  tooltip_text = tmp;
                  tmp = NULL;
                }
              else
                tooltip_text = g_strdup_printf ("%s",
                                                ((MarkerItem*) tmp_list->data)->name);
            }

          cairo_set_line_width (cr, 1.0);
          gdk_cairo_set_source_rgba (cr, &border_color);
          rounded_rectangle (cr, 1.5, 1.5, width - 3.0, SUPPLY_BAR_HEIGHT - 3.0, border_radius);
          cairo_stroke (cr);

          for (tmp_list = markers; tmp_list; tmp_list = tmp_list->next)
            {
              g_free (((MarkerItem*) tmp_list->data)->name);
              g_free (((MarkerItem*) tmp_list->data)->type);
              g_free (((MarkerItem*) tmp_list->data)->color);
            }
          g_slist_free_full (markers, g_free);

          g_strfreev (marker_levelsv);
          g_strfreev (marker_colorsv);
          g_strfreev (marker_namesv);
          g_strfreev (marker_typesv);
        }

      g_free (marker_names);

      if (tooltip_text)
        {
          gtk_widget_set_tooltip_text (widget, tooltip_text);
          g_free (tooltip_text);
        }
      else
        {
          gtk_widget_set_tooltip_text (widget, NULL);
          gtk_widget_set_has_tooltip (widget, FALSE);
        }
    }
    
  return TRUE;
}

static void
allowed_user_remove_cb (GtkToolButton *button,
                        gpointer       user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  DBusGProxy             *proxy;
  GError                 *error = NULL;
  char                   *ret_error = NULL;
  char                   *printer_name = NULL;
  char                   **names = NULL;
  char                   *name = NULL;
  int                     i, j;

  priv = PRINTERS_PANEL_PRIVATE (self);

  if (priv->current_allowed_user >= 0 &&
      priv->current_allowed_user < priv->num_allowed_users &&
      priv->allowed_users != NULL)
    name = priv->allowed_users[priv->current_allowed_user];

  if (priv->current_dest >= 0 &&
      priv->current_dest < priv->num_dests &&
      priv->dests != NULL)
    printer_name = priv->dests[priv->current_dest].name;

  if (name && printer_name)
    {
      proxy = get_dbus_proxy (MECHANISM_BUS,
                              "/",
                              MECHANISM_BUS,
                              TRUE);

      if (!proxy)
        return;

      names = g_new0 (gchar*, priv->num_allowed_users);
      j = 0;
      for (i = 0; i < (priv->num_allowed_users); i++)
        {
          if (i != priv->current_allowed_user)
            {
              names[j] = priv->allowed_users[i];
              j++;
            }
        }

      dbus_g_proxy_call (proxy, "PrinterSetUsersAllowed", &error,
                         G_TYPE_STRING, printer_name,
                         G_TYPE_STRV, names,
                         G_TYPE_INVALID,
                         G_TYPE_STRING, &ret_error,
                         G_TYPE_INVALID);

      g_object_unref (proxy);

      if (error || (ret_error && ret_error[0] != '\0'))
        {
          if (error)
            g_warning ("%s", error->message);

          if (ret_error && ret_error[0] != '\0')
            g_warning ("%s", ret_error);
        }
      else
        actualize_allowed_users_list (self);

      g_clear_error (&error);
      g_free (names);
  }
}

static void
allowed_user_add_cb (GtkCellRendererText *renderer,
                     gchar               *path,
                     gchar               *new_text,
                     gpointer             user_data)
{
  CcPrintersPanelPrivate  *priv;
  CcPrintersPanel         *self = (CcPrintersPanel*) user_data;
  DBusGProxy              *proxy;
  GError                  *error = NULL;
  char                    *ret_error = NULL;
  char                    *printer_name = NULL;
  char                   **names = NULL;
  int                      i;

  priv = PRINTERS_PANEL_PRIVATE (self);

  g_signal_handlers_disconnect_by_func (G_OBJECT (renderer),
                                        allowed_user_add_cb,
                                        self);
  g_object_set (G_OBJECT (renderer), "editable", FALSE, NULL);

  if (priv->current_dest >= 0 &&
      priv->current_dest < priv->num_dests &&
      priv->dests != NULL)
    printer_name = priv->dests[priv->current_dest].name;

  if (new_text && new_text[0] != '\0' && printer_name)
    {
      proxy = get_dbus_proxy (MECHANISM_BUS,
                              "/",
                              MECHANISM_BUS,
                              TRUE);

      if (!proxy)
        return;

      names = g_new0 (char *, priv->num_allowed_users + 2);
      for (i = 0; i < (priv->num_allowed_users); i++)
        names[i] = priv->allowed_users[i];
      names[priv->num_allowed_users] = new_text;

      dbus_g_proxy_call (proxy, "PrinterSetUsersAllowed", &error,
                         G_TYPE_STRING, printer_name,
                         G_TYPE_STRV, names,
                         G_TYPE_INVALID,
                         G_TYPE_STRING, &ret_error,
                         G_TYPE_INVALID);

      g_object_unref (proxy);

      if (error || (ret_error && ret_error[0] != '\0'))
        {
          if (error)
            g_warning ("%s", error->message);

          if (ret_error && ret_error[0] != '\0')
            g_warning ("%s", ret_error);
        }

      g_clear_error (&error);
      g_free (names);
    }

  actualize_allowed_users_list (self);
}

static void
allowed_user_add_button_cb (GtkToolButton *button,
                            gpointer       user_data)
{
  CcPrintersPanelPrivate *priv;
  GtkTreeViewColumn      *column;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  GtkListStore           *liststore;
  GtkTreeView            *treeview;
  GtkTreeIter             iter;
  GtkTreePath            *path;
  GList                  *renderers;

  priv = PRINTERS_PANEL_PRIVATE (self);

  treeview = (GtkTreeView*)
    gtk_builder_get_object (priv->builder, "allowed-users-treeview");

  liststore = (GtkListStore*)
    gtk_tree_view_get_model (treeview);

  gtk_list_store_prepend (liststore, &iter);
  column = gtk_tree_view_get_column (treeview, 0);
  path = gtk_tree_model_get_path (GTK_TREE_MODEL (liststore), &iter);
  renderers = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));

  if (column && renderers)
    {
      g_signal_connect (G_OBJECT (renderers->data),
                        "edited",
                        G_CALLBACK (allowed_user_add_cb),
                        self);

      g_object_set (renderers->data, "editable", TRUE, NULL);
      gtk_widget_grab_focus (GTK_WIDGET (treeview));
      gtk_tree_view_set_cursor_on_cell (treeview,
                                        path,
                                        column,
                                        GTK_CELL_RENDERER (renderers->data),
                                        TRUE);
    }

  g_list_free (renderers);
  gtk_tree_path_free (path);
}

static void
printer_set_default_cb (GtkToggleButton *button,
                        gpointer         user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  DBusGProxy             *proxy;
  GError                 *error = NULL;
  char                   *ret_error = NULL;
  char                   *name = NULL;

  priv = PRINTERS_PANEL_PRIVATE (self);

  if (priv->current_dest >= 0 &&
      priv->current_dest < priv->num_dests &&
      priv->dests != NULL)
    name = priv->dests[priv->current_dest].name;

  if (name)
    {
      proxy = get_dbus_proxy (MECHANISM_BUS,
                              "/",
                              MECHANISM_BUS,
                              TRUE);

      if (!proxy)
        return;

      dbus_g_proxy_call (proxy, "PrinterSetDefault", &error,
                         G_TYPE_STRING, name,
                         G_TYPE_INVALID,
                         G_TYPE_STRING, &ret_error,
                         G_TYPE_INVALID);

      g_object_unref (proxy);

      if (error || (ret_error && ret_error[0] != '\0'))
        {
          if (error)
            g_warning ("%s", error->message);

          if (ret_error && ret_error[0] != '\0')
            g_warning ("%s", ret_error);

        }
      else
        actualize_printers_list (self);

      g_clear_error (&error);

      g_signal_handlers_block_by_func (G_OBJECT (button), printer_set_default_cb, self);
      gtk_toggle_button_set_active (button, priv->dests[priv->current_dest].is_default);
      g_signal_handlers_unblock_by_func (G_OBJECT (button), printer_set_default_cb, self);
  }
}

static void
new_printer_dialog_response_cb (GtkDialog *dialog,
                                gint       response_id,
                                gpointer   user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;

  priv = PRINTERS_PANEL_PRIVATE (self);

  pp_new_printer_dialog_free (priv->pp_new_printer_dialog);
  priv->pp_new_printer_dialog = NULL;

  if (response_id == GTK_RESPONSE_OK)
    actualize_printers_list (self);
  else if (response_id == GTK_RESPONSE_REJECT)
    {
      GtkWidget *message_dialog;

      message_dialog = gtk_message_dialog_new (NULL,
                                               0,
                                               GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_CLOSE,
      /* Translators: Addition of the new printer failed. */
                                               _("Failed to add new printer."));
      g_signal_connect (message_dialog,
                        "response",
                        G_CALLBACK (gtk_widget_destroy),
                        NULL);
      gtk_widget_show (message_dialog);
    }
}

static void
printer_add_cb (GtkToolButton *toolbutton,
                gpointer       user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  GtkWidget              *widget;

  priv = PRINTERS_PANEL_PRIVATE (self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "main-vbox");

  priv->pp_new_printer_dialog = pp_new_printer_dialog_new (
    GTK_WINDOW (gtk_widget_get_toplevel (widget)),
    new_printer_dialog_response_cb,
    self);
}

static void
printer_remove_cb (GtkToolButton *toolbutton,
                   gpointer       user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  DBusGProxy             *proxy;
  GError                 *error = NULL;
  char                   *ret_error = NULL;
  char                   *name = NULL;

  priv = PRINTERS_PANEL_PRIVATE (self);

  if (priv->current_dest >= 0 &&
      priv->current_dest < priv->num_dests &&
      priv->dests != NULL)
    name = priv->dests[priv->current_dest].name;

  if (name)
    {
      proxy = get_dbus_proxy (MECHANISM_BUS,
                              "/",
                              MECHANISM_BUS,
                              TRUE);

      if (!proxy)
        return;

      dbus_g_proxy_call (proxy, "PrinterDelete", &error,
                         G_TYPE_STRING, name,
                         G_TYPE_INVALID,
                         G_TYPE_STRING, &ret_error,
                         G_TYPE_INVALID);

      if (error || (ret_error && ret_error[0] != '\0'))
        {
          if (error)
            g_warning ("%s", error->message);

          if (ret_error && ret_error[0] != '\0')
            g_warning ("%s", ret_error);
        }
      else
        actualize_printers_list (self);
  }
}

static void
printer_maintenance_cb (GtkButton *button,
                        gpointer   user_data)
{
  CcPrintersPanelPrivate  *priv;
  CcPrintersPanel         *self = (CcPrintersPanel*) user_data;
  ipp_t                   *response = NULL;
  gchar                   *printer_name = NULL;

  priv = PRINTERS_PANEL_PRIVATE (self);

  if (priv->current_dest >= 0 &&
      priv->current_dest < priv->num_dests &&
      priv->dests != NULL)
    printer_name = priv->dests[priv->current_dest].name;

  if (printer_name)
    {
      if ((GtkButton*) gtk_builder_get_object (priv->builder,
                                               "print-test-page-button")
                       == button)
        {
          response = execute_maintenance_command (printer_name,
                                                  "PrintSelfTestPage",
          /* Translators: Name of job which makes printer to print test page */
                                                  _("Test page"));
        }
      else if ((GtkButton*) gtk_builder_get_object (priv->builder,
                                                    "clean-print-heads-button")
                            == button)
        response = execute_maintenance_command (printer_name,
                                                "Clean all",
          /* Translators: Name of job which makes printer to clean its heads */
                                                _("Clean print heads"));
      if (response)
        {
          if (response->state == IPP_ERROR)
            /* Translators: An error has occured during execution of a CUPS maintenance command */
            g_warning (_("An error has occured during a maintenance command."));
          ippDelete(response);
        }
    }
}

static void
test_page_cb (GtkButton *button,
              gpointer   user_data)
{
  CcPrintersPanelPrivate  *priv;
  CcPrintersPanel         *self = (CcPrintersPanel*) user_data;
  cups_ptype_t             type = 0;
  const gchar             *printer_type = NULL;
  gchar                   *printer_name = NULL;
  gint                     i;

  priv = PRINTERS_PANEL_PRIVATE (self);

  if (priv->current_dest >= 0 &&
      priv->current_dest < priv->num_dests &&
      priv->dests != NULL)
    {
      printer_name = priv->dests[priv->current_dest].name;
      printer_type = cupsGetOption ("printer-type",
                                    priv->dests[priv->current_dest].num_options,
                                    priv->dests[priv->current_dest].options);
      if (printer_type)
        type = atoi (printer_type);
    }

  if (printer_name)
    {
      const gchar  *const dirs[] = { "/usr/share/cups",
                                     "/usr/local/share/cups",
                                     NULL };
      const gchar  *testprint[] = { "%s/data/testprint",
                                    "%s/data/testprint.ps",
                                    NULL };
      const gchar **pattern;
      const gchar  *datadir = NULL;
      http_t       *http = NULL;
      gchar        *printer_uri = NULL;
      gchar        *filename = NULL;
      gchar        *resource = NULL;
      ipp_t        *response;
      ipp_t        *request;

      if ((datadir = getenv ("CUPS_DATADIR")) != NULL)
        {
          for (pattern = testprint; *pattern != NULL; pattern++)
            {
              filename = g_strdup_printf (*pattern, datadir);
              if (g_access (filename, R_OK) == 0)
                break;
              else
                {
                  g_free (filename);
                  filename = NULL;
                }
            }
        }
      else
        {
          for (i = 0; (datadir = dirs[i]) != NULL && filename == NULL; i++)
            {
              for (pattern = testprint; *pattern != NULL; pattern++)
                {
                  filename = g_strdup_printf (*pattern, datadir);
                  if (g_access (filename, R_OK) == 0)
                    break;
                  else
                    {
                      g_free (filename);
                      filename = NULL;
                    }
                }
            }
        }

      if (filename)
        {
          if (type & CUPS_PRINTER_CLASS)
            {
              printer_uri = g_strdup_printf ("ipp://localhost/classes/%s", printer_name);
              resource = g_strdup_printf ("/classes/%s", printer_name);
            }
          else
            {
              printer_uri = g_strdup_printf ("ipp://localhost/printers/%s", printer_name);
              resource = g_strdup_printf ("/printers/%s", printer_name);
            }

          http = httpConnectEncrypt (cupsServer (), ippPort (), cupsEncryption ());
          if (http)
            {
              request = ippNewRequest (IPP_PRINT_JOB);
              ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
                            "printer-uri", NULL, printer_uri);
              ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
                            "requesting-user-name", NULL, cupsUser ());
              ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
              /* Translators: Name of job which makes printer to print test page */
                            "job-name", NULL, _("Test page"));
              response = cupsDoFileRequest (http, request, resource, filename);
              httpClose (http);
            }

          g_free (filename);
          g_free (printer_uri);
          g_free (resource);
        }
      else
        {
          response = execute_maintenance_command (printer_name,
                                                  "PrintSelfTestPage",
          /* Translators: Name of job which makes printer to print test page */
                                                  _("Test page"));
        }

      if (response)
        {
          if (response->state == IPP_ERROR)
            g_warning ("An error has occured during printing of test page.");
          ippDelete (response);
        }
    }
}

static void
on_permission_changed (GPermission *permission,
                       GParamSpec  *pspec,
                       gpointer     data)
{
  CcPrintersPanelPrivate  *priv;
  CcPrintersPanel         *self = (CcPrintersPanel*) data;
  GtkWidget               *widget;
  gboolean                 is_authorized;

  priv = PRINTERS_PANEL_PRIVATE (self);

  is_authorized = g_permission_get_allowed (G_PERMISSION (priv->permission)) &&
    !g_settings_get_boolean (priv->lockdown_settings, "disable-print-setup");

  widget = (GtkWidget*) gtk_builder_get_object (priv->builder, "printer-add-button");
  gtk_widget_set_sensitive (widget, is_authorized);

  widget = (GtkWidget*) gtk_builder_get_object (priv->builder, "printer-add-button2");
  gtk_widget_set_sensitive (widget, is_authorized);

  widget = (GtkWidget*) gtk_builder_get_object (priv->builder, "printer-remove-button");
  gtk_widget_set_sensitive (widget, is_authorized);

  widget = (GtkWidget*) gtk_builder_get_object (priv->builder, "printer-disable-switch");
  gtk_widget_set_sensitive (widget, is_authorized);

  widget = (GtkWidget*) gtk_builder_get_object (priv->builder, "printer-default-check-button");
  gtk_widget_set_sensitive (widget, is_authorized);

  widget = (GtkWidget*) gtk_builder_get_object (priv->builder, "allowed-user-add-button");
  gtk_widget_set_sensitive (widget, is_authorized);

  widget = (GtkWidget*) gtk_builder_get_object (priv->builder, "allowed-user-remove-button");
  gtk_widget_set_sensitive (widget, is_authorized);
}

static void
on_lockdown_settings_changed (GSettings  *settings,
                              const char *key,
                              gpointer    user_data)
{
  CcPrintersPanelPrivate  *priv;
  CcPrintersPanel         *self = (CcPrintersPanel*) user_data;

  if (g_str_equal (key, "disable-print-setup") == FALSE)
    return;

  priv = PRINTERS_PANEL_PRIVATE (self);

  gtk_widget_set_sensitive (priv->lock_button,
    !g_settings_get_boolean (priv->lockdown_settings, "disable-print-setup"));

  on_permission_changed (priv->permission, NULL, user_data);
}

static void
go_back_cb (GtkButton *button,
            gpointer   user_data)
{
  CcPrintersPanelPrivate  *priv;
  CcPrintersPanel         *self = (CcPrintersPanel*) user_data;
  GtkWidget               *widget;

  priv = PRINTERS_PANEL_PRIVATE (self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "notebook");
  gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), NOTEBOOK_INFO_PAGE);
}

static void
switch_to_jobs_cb (GtkButton *button,
                   gpointer   user_data)
{
  CcPrintersPanelPrivate  *priv;
  CcPrintersPanel         *self = (CcPrintersPanel*) user_data;
  GtkWidget               *widget;

  priv = PRINTERS_PANEL_PRIVATE (self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "notebook");
  gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), NOTEBOOK_JOBS_PAGE);
}

static void
switch_to_options_cb (GtkButton *button,
                      gpointer   user_data)
{
  CcPrintersPanelPrivate  *priv;
  CcPrintersPanel         *self = (CcPrintersPanel*) user_data;
  GtkWidget               *widget;

  priv = PRINTERS_PANEL_PRIVATE (self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "notebook");
  gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), NOTEBOOK_OPTIONS_PAGE);
}

static void
cc_printers_panel_init (CcPrintersPanel *self)
{
  CcPrintersPanelPrivate *priv;
  GtkWidget              *top_widget;
  GtkWidget              *widget;
  GtkWidget              *box;
  GError                 *error = NULL;
  gchar                  *objects[] = { "main-vbox", NULL };
  GtkStyleContext        *context;

  priv = self->priv = PRINTERS_PANEL_PRIVATE (self);

  /* initialize main data structure */
  priv->builder = gtk_builder_new ();
  priv->dests = NULL;
  priv->dest_model_names = NULL;
  priv->num_dests = 0;
  priv->current_dest = -1;

  priv->jobs = NULL;
  priv->num_jobs = 0;
  priv->current_job = -1;

  priv->allowed_users = NULL;
  priv->num_allowed_users = 0;
  priv->current_allowed_user = -1;

  priv->pp_new_printer_dialog = NULL;

  gtk_builder_add_objects_from_file (priv->builder,
                                     DATADIR"/printers.ui",
                                     objects, &error);

  if (error)
    {
      /* Translators: The XML file containing user interface can not be loaded */
      g_warning (_("Could not load ui: %s"), error->message);
      g_error_free (error);
      return;
    }

  /* add the top level widget */
  top_widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "main-vbox");

  /* connect signals */
  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "job-cancel-button");
  g_signal_connect (widget, "clicked", G_CALLBACK (job_process_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "job-hold-button");
  g_signal_connect (widget, "clicked", G_CALLBACK (job_process_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "job-release-button");
  g_signal_connect (widget, "clicked", G_CALLBACK (job_process_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "printer-add-button");
  g_signal_connect (widget, "clicked", G_CALLBACK (printer_add_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "printer-add-button2");
  g_signal_connect (widget, "clicked", G_CALLBACK (printer_add_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "printer-remove-button");
  g_signal_connect (widget, "clicked", G_CALLBACK (printer_remove_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "printer-disable-switch");
  g_signal_connect (widget, "notify::active", G_CALLBACK (printer_disable_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "allowed-user-remove-button");
  g_signal_connect (widget, "clicked", G_CALLBACK (allowed_user_remove_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "allowed-user-add-button");
  g_signal_connect (widget, "clicked", G_CALLBACK (allowed_user_add_button_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "supply-drawing-area");
  g_signal_connect (widget, "draw", G_CALLBACK (supply_levels_draw_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "printer-default-check-button");
  g_signal_connect (widget, "toggled", G_CALLBACK (printer_set_default_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "print-test-page-button");
  g_signal_connect (widget, "clicked", G_CALLBACK (test_page_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "back-button-1");
  g_signal_connect (widget, "clicked", G_CALLBACK (go_back_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "back-button-2");
  g_signal_connect (widget, "clicked", G_CALLBACK (go_back_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "printer-jobs-button");
  g_signal_connect (widget, "clicked", G_CALLBACK (switch_to_jobs_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "printer-options-button");
  g_signal_connect (widget, "clicked", G_CALLBACK (switch_to_options_cb), self);

  priv->lockdown_settings = g_settings_new ("org.gnome.desktop.lockdown");
  g_signal_connect (priv->lockdown_settings,
                    "changed",
                    G_CALLBACK (on_lockdown_settings_changed),
                    self);


  /* Set junctions */
  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "printers-scrolledwindow");
  context = gtk_widget_get_style_context (widget);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "printers-toolbar");
  context = gtk_widget_get_style_context (widget);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "allowed-users-scrolledwindow");
  context = gtk_widget_get_style_context (widget);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "allowed-users-toolbar");
  context = gtk_widget_get_style_context (widget);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "queue-scrolledwindow");
  context = gtk_widget_get_style_context (widget);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "queue-toolbar");
  context = gtk_widget_get_style_context (widget);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);


  /* Add unlock button */
  priv->permission = (GPermission *)polkit_permission_new_sync (
    "org.opensuse.cupspkhelper.mechanism.all-edit", NULL, NULL, NULL);
  if (priv->permission != NULL)
    {
      widget = cc_lock_button_new (priv->permission);
      gtk_widget_set_margin_top (widget, 12);
      gtk_widget_show (widget);
      box = (GtkWidget*) gtk_builder_get_object (priv->builder, "main-vbox");
      gtk_box_pack_end (GTK_BOX (box), widget, FALSE, FALSE, 0);
      g_signal_connect (priv->permission, "notify",
                        G_CALLBACK (on_permission_changed), self);
      on_permission_changed (priv->permission, NULL, self);
      priv->lock_button = widget;
    }


  gtk_style_context_get_background_color (gtk_widget_get_style_context (top_widget),
                                          GTK_STATE_FLAG_NORMAL,
                                          &priv->background_color);

  populate_printers_list (self);
  populate_jobs_list (self);
  populate_allowed_users_list (self);

  gtk_container_add (GTK_CONTAINER (self), top_widget);
  gtk_widget_show_all (GTK_WIDGET (self));
}

void
cc_printers_panel_register (GIOModule *module)
{
  cc_printers_panel_register_type (G_TYPE_MODULE (module));
  g_io_extension_point_implement (CC_SHELL_PANEL_EXTENSION_POINT,
                                  CC_TYPE_PRINTERS_PANEL,
                                  "printers", 0);
}

