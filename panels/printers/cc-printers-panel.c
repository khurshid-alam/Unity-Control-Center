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
#include <dbus/dbus-glib.h>

#include <cups/cups.h>

G_DEFINE_DYNAMIC_TYPE (CcPrintersPanel, cc_printers_panel, CC_TYPE_PANEL)

#define PRINTERS_PANEL_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CC_TYPE_PRINTERS_PANEL, CcPrintersPanelPrivate))

#define MECHANISM_BUS "org.opensuse.CupsPkHelper.Mechanism"

#define SUPPLY_BAR_WIDTH 140
#define SUPPLY_BAR_HEIGHT 10
#define SUPPLY_BAR_SPACE 5

struct _CcPrintersPanelPrivate
{
  GtkBuilder *builder;

  cups_dest_t *dests;
  int num_dests;
  int current_dest;

  cups_job_t *jobs;
  int num_jobs;
  int current_job;

  gchar **allowed_users;
  int num_allowed_users;
  int current_allowed_user;

  gpointer dummy;
};

static void actualize_jobs_list (CcPrintersPanel *self);
static void actualize_printers_list (CcPrintersPanel *self);
static void actualize_allowed_users_list (CcPrintersPanel *self);
static void printer_disable_cb (GtkToggleButton *togglebutton, gpointer user_data);

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
  PRINTER_NAME_COLUMN,
  PRINTER_ID_COLUMN,
  PRINTER_PAUSED_COLUMN,
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
  const gchar            *none = "---";
  GtkWidget              *widget;
  gchar                  *reason = NULL;
  gchar                 **printer_reasons = NULL;
  gchar                  *marker_levels = NULL;
  gchar                  *description = NULL;
  gchar                  *location = NULL;
  gchar                  *status = NULL;
  gint                    width, height;
  int                     printer_state = 3;
  int                     id, i, j;
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
      N_("Low on toner"),
      N_("Out of toner"),
      /* Translators: "Developer" like on photo development context */
      N_("Low on developer"),
      /* Translators: "Developer" like on photo development context */
      N_("Out of developer"),
      /* Translators: "marker" is one color bin of the printer */
      N_("Low on a marker supply"),
      /* Translators: "marker" is one color bin of the printer */
      N_("Out of a marker supply"),
      N_("Open cover"),
      N_("Open door"),
      N_("Low on paper"),
      N_("Out of paper"),
      N_("Offline"),
      N_("Paused"),
      N_("Waste receptacle almost full"),
      N_("Waste receptacle full"),
      N_("The optical photo conductor is near end of life"),
      N_("The optical photo conductor is no longer functioning")
    };

  priv = PRINTERS_PANEL_PRIVATE (self);

  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    gtk_tree_model_get (model, &iter,
			PRINTER_ID_COLUMN, &id,
			-1);
  else
    id = -1;

  priv->current_dest = id;

  actualize_jobs_list (self);
  actualize_allowed_users_list (self);

  if (priv->current_dest >= 0 &&
      priv->current_dest < priv->num_dests &&
      priv->dests != NULL)
    {
      for (i = 0; i < priv->dests[id].num_options; i++)
        {
          if (g_strcmp0 (priv->dests[id].options[i].name, "printer-location") == 0)
            location = g_strdup (priv->dests[id].options[i].value);
          else if (g_strcmp0 (priv->dests[id].options[i].name, "printer-state") == 0)
            printer_state = atoi (priv->dests[id].options[i].value);
          else if (g_strcmp0 (priv->dests[id].options[i].name, "printer-info") == 0)
            description = g_strdup (priv->dests[id].options[i].value);
          else if (g_strcmp0 (priv->dests[id].options[i].name, "printer-state-reasons") == 0)
            reason = priv->dests[id].options[i].value;
          else if (g_strcmp0 (priv->dests[priv->current_dest].options[i].name, "marker-levels") == 0)
            marker_levels = priv->dests[priv->current_dest].options[i].value;
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
            status = g_strdup (statuses[error_index]);
          else if (warning_index >= 0)
            status = g_strdup (statuses[warning_index]);
          else if (report_index >= 0)
            status = g_strdup (statuses[report_index]);
        }

      if (status == NULL)
        {
          switch (printer_state)
            {
              case 3:
                status = g_strdup ( N_ ("Idle"));
                break;
              case 4:
                status = g_strdup ( N_ ("Processing"));
                break;
              case 5:
                status = g_strdup ( N_ ("Paused"));
                break;
            }
        }
        
      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-status-label");

      if (status)
        {
          gtk_label_set_text (GTK_LABEL (widget), status);
          g_free (status);
        }
      else
        gtk_label_set_text (GTK_LABEL (widget), none);


      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-location-label");

      if (location)
        {
          gtk_label_set_text (GTK_LABEL (widget), location);
          g_free (location);
        }
      else
        gtk_label_set_text (GTK_LABEL (widget), none);


      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-description-label");

      if (description)
        {
          gtk_label_set_text (GTK_LABEL (widget), description);
          g_free (description);
        }
      else
        gtk_label_set_text (GTK_LABEL (widget), none);


      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-disable-button");

      gtk_widget_set_sensitive (widget, TRUE);
      g_signal_handlers_block_by_func (G_OBJECT (widget), printer_disable_cb, self);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), printer_state == 5);
      g_signal_handlers_unblock_by_func (G_OBJECT (widget), printer_disable_cb, self);


      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "supply-drawing-area");

      width = gtk_widget_get_allocated_width (widget);

      if (marker_levels)
        {
          gchar **marker_levelsv = NULL;

          widget = (GtkWidget*)
            gtk_builder_get_object (priv->builder, "supply-drawing-area");

          marker_levelsv = g_strsplit (marker_levels, ",", -1);
          gtk_widget_set_size_request (widget,
                                       width,
                                       ((g_strv_length (marker_levelsv) - 1) * SUPPLY_BAR_SPACE
                                       + g_strv_length (marker_levelsv) * SUPPLY_BAR_HEIGHT));
          g_strfreev (marker_levelsv);
        }
      else
        gtk_widget_set_size_request (widget, 0, 0);

      width = gtk_widget_get_allocated_width (widget);
      height = gtk_widget_get_allocated_height (widget);
      gtk_widget_queue_draw_area (widget, 0, 0, width, height);
    }
  else
    {
      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-description-label");
      gtk_label_set_text (GTK_LABEL (widget), "");

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-location-label");
      gtk_label_set_text (GTK_LABEL (widget), "");

      widget = (GtkWidget*)
        gtk_builder_get_object (priv->builder, "printer-disable-button");
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
  gboolean                paused = FALSE;
  gchar                  *current_printer_instance = NULL;
  gchar                  *current_printer_name = NULL;
  int                     current_dest = -1;
  int                     i, j;

  priv = PRINTERS_PANEL_PRIVATE (self);

  if (priv->current_dest >= 0 &&
      priv->current_dest < priv->num_dests &&
      priv->dests != NULL)
    {
      current_printer_name = g_strdup (priv->dests[priv->current_dest].name);
      if (priv->dests[priv->current_dest].instance)
        current_printer_instance = g_strdup (priv->dests[priv->current_dest].instance);
    }

  if (priv->num_jobs > 0)
    cupsFreeJobs (priv->num_jobs, priv->jobs);
  priv->num_dests = cupsGetDests (&priv->dests);
  priv->current_dest = -1;

  treeview = (GtkTreeView*)
    gtk_builder_get_object (priv->builder, "printer-treeview");

  store = gtk_list_store_new (PRINTER_N_COLUMNS,
                              G_TYPE_STRING,
                              G_TYPE_INT,
                              G_TYPE_BOOLEAN);

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

      gtk_list_store_set (store, &iter,
                          PRINTER_NAME_COLUMN, instance,
                          PRINTER_ID_COLUMN, i,
                          PRINTER_PAUSED_COLUMN, paused,
                          -1);
      g_free (instance);
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
      cups_job_t *jobs = NULL;
      int         num_jobs = 0;

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

      /* Select first printer */
      if (priv->current_dest < 0 && priv->num_dests > 0)
        {
          priv->current_dest = 0;
        }

      if (priv->current_dest >= 0)
        {
          GtkTreePath *path = gtk_tree_path_new_from_indices (priv->current_dest, -1);

          gtk_tree_model_get_iter ((GtkTreeModel *) store,
                                   &selected_iter,
                                   path);

          gtk_tree_selection_select_iter (
            gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview)),
            &selected_iter);
        }
    }
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
  GtkCellRenderer        *renderer;
  GtkWidget              *treeview;

  priv = PRINTERS_PANEL_PRIVATE (self);

  treeview = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "printer-treeview");

  g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview)),
                    "changed", G_CALLBACK (printer_selection_changed_cb), self);

  actualize_printers_list (self);


  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes ("Printer", renderer,
                                                     "text", PRINTER_NAME_COLUMN, NULL);
  gtk_tree_view_column_set_cell_data_func (column, renderer, set_cell_sensitivity_func,
                                           self, NULL);

  gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
}

enum
{
  JOB_ID_COLUMN,
  JOB_TITLE_COLUMN,
  JOB_STATE_COLUMN,
  JOB_USER_COLUMN,
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

  store = gtk_list_store_new (JOB_N_COLUMNS, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

  for (i = 0; i < priv->num_jobs; i++)
    {
      struct tm *ts;
      gchar     *time_string;
      gchar     *state = NULL;

      ts = localtime (&(priv->jobs[i].creation_time));
      time_string = g_strdup_printf ("%02d:%02d:%02d", ts->tm_hour, ts->tm_min, ts->tm_sec);

      switch (priv->jobs[i].state)
        {
          case IPP_JOB_PENDING:
            state = g_strdup (_("Pending"));
            break;
          case IPP_JOB_HELD:
            state = g_strdup (_("Held"));
            break;
          case IPP_JOB_PROCESSING:
            state = g_strdup (_("Processing"));
            break;
          case IPP_JOB_STOPPED:
            state = g_strdup (_("Stopped"));
            break;
          case IPP_JOB_CANCELED:
            state = g_strdup (_("Canceled"));
            break;
          case IPP_JOB_ABORTED:
            state = g_strdup (_("Aborted"));
            break;
          case IPP_JOB_COMPLETED:
            state = g_strdup (_("Completed"));
            break;
        }

      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter,
                          JOB_ID_COLUMN, i,
                          JOB_TITLE_COLUMN, priv->jobs[i].title,
                          JOB_STATE_COLUMN, state,
                          JOB_USER_COLUMN, priv->jobs[i].user,
                          JOB_CREATION_TIME_COLUMN, time_string,
                          -1);

      g_free (time_string);
      g_free (state);
    }

  gtk_tree_view_set_model (treeview, GTK_TREE_MODEL (store));
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
  int                     id;

  priv = PRINTERS_PANEL_PRIVATE (self);

  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    gtk_tree_model_get (model, &iter,
			JOB_ID_COLUMN, &id,
			-1);
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
  GtkTreeView            *treeview;
  gfloat                  x_align = 0.5;
  gfloat                  y_align = 0.5;

  priv = PRINTERS_PANEL_PRIVATE (self);

  actualize_jobs_list (self);

  treeview = (GtkTreeView*)
    gtk_builder_get_object (priv->builder, "job-treeview");

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_renderer_get_alignment (renderer, &x_align, &y_align);
  gtk_cell_renderer_set_alignment (renderer, 0.5, y_align);

  column = gtk_tree_view_column_new_with_attributes (_("Job Title"), renderer,
                                                     "text", JOB_TITLE_COLUMN, NULL);
  gtk_tree_view_column_set_fixed_width (column, 180);
  gtk_tree_view_column_set_min_width (column, 180);
  gtk_tree_view_column_set_max_width (column, 180);
  gtk_tree_view_append_column (treeview, column);

  column = gtk_tree_view_column_new_with_attributes (_("Job State"), renderer,
                                                     "text", JOB_STATE_COLUMN, NULL);
  gtk_tree_view_column_set_expand (column, TRUE);
  gtk_tree_view_append_column (treeview, column);

  column = gtk_tree_view_column_new_with_attributes (_("User"), renderer,
                                                     "text", JOB_USER_COLUMN, NULL);
  gtk_tree_view_column_set_expand (column, TRUE);
  gtk_tree_view_append_column (treeview, column);

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

static int
ccGetAllowedUsers (gchar ***allowed_users, char *printer_name)
{
  const char * const   attrs[1] = { "requesting-user-name-allowed" };
  http_t              *http;
  ipp_t               *request = NULL;
  gchar              **users = NULL;
  ipp_t               *response;
  char                 uri[HTTP_MAX_URI + 1];
  int                  num_allowed_users = 0;

  http = httpConnectEncrypt (cupsServer (),
                             ippPort (),
                             cupsEncryption ());

  if (http || !allowed_users)
    {
      request = ippNewRequest (IPP_GET_PRINTER_ATTRIBUTES);

      g_snprintf (uri, sizeof (uri), "ipp://localhost/printers/%s", printer_name);
      ippAddString (request,
                    IPP_TAG_OPERATION,
                    IPP_TAG_URI,
                    "printer-uri",
                    NULL,
                    uri);
       ippAddStrings (request,
                      IPP_TAG_OPERATION,
                      IPP_TAG_KEYWORD,
                      "requested-attributes",
                      1,
                      NULL,
                      attrs);

       response = cupsDoRequest (http, request, "/");
       if (response)
         {
           ipp_attribute_t *attr = NULL;
           ipp_attribute_t *allowed = NULL;

           for (attr = response->attrs; attr != NULL; attr = attr->next)
             {
               if (attr->group_tag == IPP_TAG_PRINTER &&
                   attr->value_tag == IPP_TAG_NAME &&
                   !g_strcmp0 (attr->name, "requesting-user-name-allowed"))
                 allowed = attr;
             }

            if (allowed && allowed->num_values > 0)
              {
                int i;

                num_allowed_users = allowed->num_values;
                users = g_new (gchar*, num_allowed_users);

                for (i = 0; i < num_allowed_users; i ++)
                  users[i] = g_strdup (allowed->values[i].string.text);
	      }
         }
     }

  *allowed_users = users;
  return num_allowed_users;
}

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
}

static void
allowed_users_selection_changed_cb (GtkTreeSelection *selection,
                                    gpointer          user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  GtkTreeModel           *model;
  GtkTreeIter             iter;
  int                     id;

  priv = PRINTERS_PANEL_PRIVATE (self);

  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    gtk_tree_model_get (model, &iter,
			ALLOWED_USERS_ID_COLUMN, &id,
			-1);
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

static DBusGProxy *
get_dbus_proxy ()
{
  DBusGConnection *system_bus;
  DBusGProxy      *proxy;
  GError          *error;

  error = NULL;
  system_bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
  if (system_bus == NULL)
    {
      g_warning (_("Could not connect to system bus: %s"),
                 error->message);
      g_error_free (error);
      return NULL;
    }

  error = NULL;

  proxy = dbus_g_proxy_new_for_name (system_bus,
                                     MECHANISM_BUS,
                                     "/",
                                     MECHANISM_BUS);
  return proxy;
}

static void
job_process_cb (GtkButton *button,
                gpointer   user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  DBusGProxy             *proxy;
  GtkWidget              *widget;
  gboolean                ret = FALSE;
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
      proxy = get_dbus_proxy ();

      if (!proxy)
        return;

      if ((GtkButton*) gtk_builder_get_object (priv->builder,
                                               "job-cancel-button") ==
          button)
        ret = dbus_g_proxy_call (proxy, "JobCancelPurge", &error,
                                 G_TYPE_INT, id,
                                 G_TYPE_BOOLEAN, FALSE,
                                 G_TYPE_INVALID,
                                 G_TYPE_STRING, &ret_error,
                                 G_TYPE_INVALID);
      else if ((GtkButton*) gtk_builder_get_object (priv->builder,
                                                        "job-hold-button") ==
               button)
        ret = dbus_g_proxy_call (proxy, "JobSetHoldUntil", &error,
                                 G_TYPE_INT, id,
                                 G_TYPE_STRING, "indefinite",
                                 G_TYPE_INVALID,
                                 G_TYPE_STRING, &ret_error,
                                 G_TYPE_INVALID);
      else if ((GtkButton*) gtk_builder_get_object (priv->builder,
                                                        "job-release-button") ==
               button)
        ret = dbus_g_proxy_call (proxy, "JobSetHoldUntil", &error,
                                 G_TYPE_INT, id,
                                 G_TYPE_STRING, "no-hold",
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
        actualize_jobs_list (self);
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
printer_disable_cb (GtkToggleButton *togglebutton,
                    gpointer         user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  DBusGProxy             *proxy;
  gboolean                ret = FALSE;
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
      proxy = get_dbus_proxy ();

      if (!proxy)
        return;

      ret = dbus_g_proxy_call (proxy, "PrinterSetEnabled", &error,
                               G_TYPE_STRING, name,
                               G_TYPE_BOOLEAN, paused,
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
        {
          gtk_toggle_button_set_active (togglebutton, paused);
          actualize_printers_list (self);
        }
  }
}

static void
printer_delete_cb (GtkToolButton *toolbutton,
                   gpointer       user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  DBusGProxy             *proxy;
  gboolean                ret = FALSE;
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
      proxy = get_dbus_proxy ();

      if (!proxy)
        return;

      ret = dbus_g_proxy_call (proxy, "PrinterDelete", &error,
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

static gboolean
supply_levels_draw_cb (GtkWidget *widget,
                       cairo_t *cr,
                       gpointer user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  gchar                  *marker_levels = NULL;
  gchar                  *marker_colors = NULL;
  gchar                  *marker_names = NULL;
  gchar                  *tooltip_text = NULL;
  int                     i;

  priv = PRINTERS_PANEL_PRIVATE (self);

  if (priv->current_dest >= 0 &&
      priv->current_dest < priv->num_dests &&
      priv->dests != NULL)
    {
      for (i = 0; i < priv->dests[priv->current_dest].num_options; i++)
        {
          if (g_strcmp0 (priv->dests[priv->current_dest].options[i].name, "marker-names") == 0)
            marker_names = priv->dests[priv->current_dest].options[i].value;
          else if (g_strcmp0 (priv->dests[priv->current_dest].options[i].name, "marker-levels") == 0)
            marker_levels = priv->dests[priv->current_dest].options[i].value;
          else if (g_strcmp0 (priv->dests[priv->current_dest].options[i].name, "marker-colors") == 0)
            marker_colors = priv->dests[priv->current_dest].options[i].value;
        }

      if (marker_levels && marker_colors && marker_names)
        {
          gchar **marker_levelsv = NULL;
          gchar **marker_colorsv = NULL;
          gchar **marker_namesv = NULL;
          gchar  *tmp = NULL;
          gint    width;
          gint    height;

          widget = (GtkWidget*)
            gtk_builder_get_object (priv->builder, "supply-drawing-area");

          marker_levelsv = g_strsplit (marker_levels, ",", -1);
          marker_colorsv = g_strsplit (marker_colors, ",", -1);
          marker_namesv = g_strsplit (marker_names, ",", -1);
  
          width = gtk_widget_get_allocated_width (widget);
          height = gtk_widget_get_allocated_height (widget);

          width = width < SUPPLY_BAR_WIDTH ? width : SUPPLY_BAR_WIDTH;
          for (i = 0; i < g_strv_length (marker_levelsv); i++)
            {
              GdkRGBA color = {0.0, 0.0, 0.0, 1.0};
              GdkRGBA light_color;
              double  display_value;
              int     value;

              value = atoi (marker_levelsv[i]);

              gdk_rgba_parse (&color, marker_colorsv[i]);
              light_color.red = color.red < 0.8 ? color.red + 0.8 : 1.0;
              light_color.green = color.green < 0.8 ? color.green + 0.8 : 1.0;
              light_color.blue = color.blue < 0.8 ? color.blue + 0.8 : 1.0;
              light_color.alpha = 1.0;

              if (value >= 0)
                {
                  display_value = value / 100.0 * width;

                  cairo_rectangle (cr,
                                   0.0,
                                   i * (SUPPLY_BAR_HEIGHT + SUPPLY_BAR_SPACE),
                                   display_value,
                                   SUPPLY_BAR_HEIGHT);
                  gdk_cairo_set_source_rgba (cr, &color);
                  cairo_fill (cr);

                  cairo_rectangle (cr,
                                   display_value,
                                   i * (SUPPLY_BAR_HEIGHT + SUPPLY_BAR_SPACE),
                                   width - display_value,
                                   SUPPLY_BAR_HEIGHT);
                  gdk_cairo_set_source_rgba (cr, &light_color);
                  cairo_fill (cr);
                }
              else
                {
                  cairo_rectangle (cr,
                                   0.0,
                                   i * (SUPPLY_BAR_HEIGHT + SUPPLY_BAR_SPACE),
                                   width,
                                   SUPPLY_BAR_HEIGHT);
                  gdk_cairo_set_source_rgba (cr, &light_color);
                  cairo_fill (cr);
                }

              if (tooltip_text)
                {
                  tmp = g_strdup_printf ("%s%s\n", tooltip_text, marker_namesv[i]);
                  g_free (tooltip_text);
                  tooltip_text = tmp;
                  tmp = NULL;
                }
              else
                tooltip_text = g_strdup_printf ("%s\n", marker_namesv[i]);
            }

          g_strfreev (marker_levelsv);
          g_strfreev (marker_colorsv);
          g_strfreev (marker_namesv);
        }

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
allowed_user_remove_cb (GtkButton *button,
                        gpointer   user_data)
{
  CcPrintersPanelPrivate *priv;
  CcPrintersPanel        *self = (CcPrintersPanel*) user_data;
  DBusGProxy             *proxy;
  gboolean                ret = FALSE;
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
      proxy = get_dbus_proxy ();

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

      ret = dbus_g_proxy_call (proxy, "PrinterSetUsersAllowed", &error,
                               G_TYPE_STRING, printer_name,
                               G_TYPE_STRV, names,
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
        actualize_allowed_users_list (self);
  }
}

static void
set_widget_style (GtkWidget *widget, gchar *style_data)
{
  GtkStyleProvider *provider;
  GtkStyleContext  *context;

  if (widget)
    {
      context = gtk_widget_get_style_context (widget);
      provider = g_object_get_data (G_OBJECT (widget), "provider");

      if (provider == NULL)
        {
          provider = (GtkStyleProvider *)gtk_css_provider_new ();
          g_object_set_data (G_OBJECT (widget), "provider", provider);
          gtk_style_context_add_provider (context,
                                          GTK_STYLE_PROVIDER (provider),
                                          GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }

      gtk_css_provider_load_from_data ((GtkCssProvider *)provider,
                                       style_data, -1, NULL);
      gtk_style_context_invalidate (context);
    }
}

static void
cc_printers_panel_init (CcPrintersPanel *self)
{
  CcPrintersPanelPrivate *priv;
  GtkWidget              *top_widget;
  GtkWidget              *widget;
  GError                 *error = NULL;
  gchar                  *objects[] = { "main-vbox", NULL };

  priv = self->priv = PRINTERS_PANEL_PRIVATE (self);

  /* initialize main data structure */
  priv->builder = gtk_builder_new ();
  priv->dests = NULL;
  priv->num_dests = 0;
  priv->current_dest = -1;

  priv->jobs = NULL;
  priv->num_jobs = 0;
  priv->current_job = -1;

  priv->allowed_users = NULL;
  priv->num_allowed_users = 0;
  priv->current_allowed_user = -1;

  gtk_builder_add_objects_from_file (priv->builder,
                                     DATADIR"/printers.ui",
                                     objects, &error);

  if (error)
    {
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
    gtk_builder_get_object (priv->builder, "printer-disable-button");
  g_signal_connect (widget, "toggled", G_CALLBACK (printer_disable_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "printer-delete-button");
  g_signal_connect (widget, "clicked", G_CALLBACK (printer_delete_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "allowed-user-remove-button");
  g_signal_connect (widget, "clicked", G_CALLBACK (allowed_user_remove_cb), self);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "supply-drawing-area");
  g_signal_connect (widget, "draw", G_CALLBACK (supply_levels_draw_cb), self);


  /* set plain style for borders of toolbars */
  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "printers-toolbar");
  set_widget_style (widget, "GtkToolbar { border-style: none }");


  /* make unused widgets insensitive for now */
  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "allowed-user-add-button");
  gtk_widget_set_sensitive (widget, FALSE);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "printer-add-button");
  gtk_widget_set_sensitive (widget, FALSE);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "print-test-page-button");
  gtk_widget_set_sensitive (widget, FALSE);

  widget = (GtkWidget*)
    gtk_builder_get_object (priv->builder, "clean-print-heads-button");
  gtk_widget_set_sensitive (widget, FALSE);


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

