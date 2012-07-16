/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright 2009-2010  Red Hat, Inc,
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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

#ifndef __PP_UTILS_H__
#define __PP_UTILS_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Match level of PPD driver.
 */
enum
{
  PPD_NO_MATCH = 0,
  PPD_GENERIC_MATCH,
  PPD_CLOSE_MATCH,
  PPD_EXACT_MATCH,
  PPD_EXACT_CMD_MATCH
};

typedef struct
{
  gchar *ppd_name;
  gchar *ppd_display_name;
  gint   ppd_match_level;
} PPDName;

typedef struct
{
  gchar    *manufacturer_name;
  gchar    *manufacturer_display_name;
  PPDName **ppds;
  gsize     num_of_ppds;
} PPDManufacturerItem;

typedef struct
{
  PPDManufacturerItem **manufacturers;
  gsize                 num_of_manufacturers;
} PPDList;

gchar      *get_tag_value (const gchar *tag_string,
                           const gchar *tag_name);

PPDName    *get_ppd_name (gchar *device_id,
                          gchar *device_make_and_model,
                          gchar *device_uri);

char       *get_dest_attr (const char *dest_name,
                           const char *attr);

ipp_t      *execute_maintenance_command (const char *printer_name,
                                         const char *command,
                                         const char *title);

int         ccGetAllowedUsers (gchar      ***allowed_users,
                               const char   *printer_name);

gchar      *get_ppd_attribute (const gchar *ppd_file_name,
                               const gchar *attribute_name);

void        cancel_cups_subscription (gint id);

gint        renew_cups_subscription (gint id,
                                     const char * const *events,
                                     gint num_events,
                                     gint lease_duration);

void        set_local_default_printer (const gchar *printer_name);

gboolean    printer_set_location (const gchar *printer_name,
                                  const gchar *location);

gboolean    printer_set_accepting_jobs (const gchar *printer_name,
                                        gboolean     accepting_jobs,
                                        const gchar *reason);

gboolean    printer_set_enabled (const gchar *printer_name,
                                 gboolean     enabled);

gboolean    printer_rename (const gchar *old_name,
                            const gchar *new_name);

gboolean    printer_delete (const gchar *printer_name);

gboolean    printer_set_default (const gchar *printer_name);

gboolean    printer_set_shared (const gchar *printer_name,
                                gboolean     shared);

gboolean    printer_set_job_sheets (const gchar *printer_name,
                                    const gchar *start_sheet,
                                    const gchar *end_sheet);

gboolean    printer_set_policy (const gchar *printer_name,
                                const gchar *policy,
                                gboolean     error_policy);

gboolean    printer_set_users (const gchar  *printer_name,
                               gchar       **users,
                               gboolean      allowed);

gboolean    class_add_printer (const gchar *class_name,
                               const gchar *printer_name);

gboolean    printer_is_local (cups_ptype_t  printer_type,
                              const gchar  *device_uri);

gchar      *printer_get_hostname (cups_ptype_t  printer_type,
                                  const gchar  *device_uri,
                                  const gchar  *printer_uri);

void        printer_set_default_media_size (const gchar *printer_name);

typedef void (*PSPCallback) (gchar    *printer_name,
                             gboolean  success,
                             gpointer  user_data);

void        printer_set_ppd_async (const gchar  *printer_name,
                                   const gchar  *ppd_name,
                                   GCancellable *cancellable,
                                   PSPCallback   callback,
                                   gpointer      user_data);

void        printer_set_ppd_file_async (const gchar *printer_name,
                                        const gchar *ppd_filename,
                                        GCancellable *cancellable,
                                        PSPCallback   callback,
                                        gpointer      user_data);

typedef void (*GPNCallback) (PPDName     **names,
                             const gchar  *printer_name,
                             gboolean      cancelled,
                             gpointer      user_data);

void        get_ppd_names_async (gchar        *printer_name,
                                 gint          count,
                                 GCancellable *cancellable,
                                 GPNCallback   callback,
                                 gpointer      user_data);

typedef void (*GAPCallback) (PPDList  *ppds,
                             gpointer  user_data);

void        get_all_ppds_async (GAPCallback callback,
                                gpointer    user_data);

PPDList    *ppd_list_copy (PPDList *list);
void        ppd_list_free (PPDList *list);

enum
{
  IPP_ATTRIBUTE_TYPE_INTEGER = 0,
  IPP_ATTRIBUTE_TYPE_STRING,
  IPP_ATTRIBUTE_TYPE_RANGE,
  IPP_ATTRIBUTE_TYPE_BOOLEAN
};

typedef struct
{
  gboolean  boolean_value;
  gchar    *string_value;
  gint      integer_value;
  gint      lower_range;
  gint      upper_range;
} IPPAttributeValue;

typedef struct
{
  gchar             *attribute_name;
  IPPAttributeValue *attribute_values;
  gint               num_of_values;
  gint               attribute_type;
} IPPAttribute;

typedef void (*GIACallback) (GHashTable *table,
                             gpointer    user_data);

void        get_ipp_attributes_async (const gchar  *printer_name,
                                      gchar       **attributes_names,
                                      GIACallback   callback,
                                      gpointer      user_data);

IPPAttribute *ipp_attribute_copy (IPPAttribute *attr);

void        ipp_attribute_free (IPPAttribute *attr);

gchar      *get_standard_manufacturers_name (gchar *name);

G_END_DECLS

#endif /* __PP_UTILS_H */
