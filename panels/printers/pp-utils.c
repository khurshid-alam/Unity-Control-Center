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

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <cups/cups.h>
#include <cups/ppd.h>

#include "pp-utils.h"

#define MECHANISM_BUS "org.opensuse.CupsPkHelper.Mechanism"

#define SCP_BUS   "org.fedoraproject.Config.Printing"
#define SCP_PATH  "/org/fedoraproject/Config/Printing"
#define SCP_IFACE "org.fedoraproject.Config.Printing"

#define DBUS_TIMEOUT 120000

gchar *
get_tag_value (const gchar *tag_string, const gchar *tag_name)
{
  gchar **tag_string_splitted = NULL;
  gchar  *tag_value = NULL;
  gint    tag_name_length;
  gint    i;

  if (tag_string && tag_name)
    {
      tag_name_length = strlen (tag_name);
      tag_string_splitted = g_strsplit (tag_string, ";", 0);
      if (tag_string_splitted)
        {
          for (i = 0; i < g_strv_length (tag_string_splitted); i++)
            if (g_ascii_strncasecmp (tag_string_splitted[i], tag_name, tag_name_length) == 0)
              if (strlen (tag_string_splitted[i]) > tag_name_length + 1)
                tag_value = g_strdup (tag_string_splitted[i] + tag_name_length + 1);

          g_strfreev (tag_string_splitted);
        }
    }

  return tag_value;
}


typedef struct
{
  gchar *ppd_name;
  gchar *ppd_device_id;
  gchar *ppd_product;
  gchar *ppd_make_and_model;

  gchar *driver_type;

  gchar *mfg;
  gchar *mdl;
  gint   match_level;
  gint   preference_value;
} PPDItem;


static void
ppd_item_free (PPDItem *item)
{
  if (item)
    {
      g_free (item->ppd_name);
      g_free (item->ppd_device_id);
      g_free (item->ppd_product);
      g_free (item->ppd_make_and_model);
      g_free (item->driver_type);
      g_free (item->mfg);
      g_free (item->mdl);
    }
}

static PPDItem *
ppd_item_copy (PPDItem *item)
{
  PPDItem *result = NULL;

  result = g_new0 (PPDItem, 1);
  if (item && result)
    {
      result->ppd_name = g_strdup (item->ppd_name);
      result->ppd_device_id = g_strdup (item->ppd_device_id);
      result->ppd_product = g_strdup (item->ppd_product);
      result->ppd_make_and_model = g_strdup (item->ppd_make_and_model);
      result->driver_type = g_strdup (item->driver_type);
      result->mfg = g_strdup (item->mfg);
      result->mdl = g_strdup (item->mdl);
      result->match_level = item->match_level;
      result->preference_value = item->preference_value;
    }

  return result;
}


/*
 * Make deep copy of given const string array.
 */
static gchar **
strvdup (const gchar * const x[])
{
  gint i, length = 0;
  gchar **result;

  for (length = 0; x && x[length]; length++);

  length++;

  result = g_new0 (gchar *, length);
  for (i = 0; i < length; i++)
    result[i] = g_strdup (x[i]);

  return result;
}

/*
 * Normalize given string so that it is lowercase, doesn't
 * have trailing or leading whitespaces and digits doesn't
 * neighbour with alphabetic.
 * (see cupshelpers/ppds.py from system-config-printer)
 */
static gchar *
normalize (const gchar *input_string)
{
  gchar *tmp = NULL;
  gchar *res = NULL;
  gchar *result = NULL;
  gint   i, j = 0, k = -1;

  if (input_string)
    {
      tmp = g_strstrip (g_ascii_strdown (input_string, -1));
      if (tmp)
        {
          res = g_new (gchar, 2 * strlen (tmp));

          for (i = 0; i < strlen (tmp); i++)
            {
              if ((g_ascii_isalpha (tmp[i]) && k >= 0 && g_ascii_isdigit (res[k])) ||
                  (g_ascii_isdigit (tmp[i]) && k >= 0 && g_ascii_isalpha (res[k])))
                {
                  res[j] = ' ';
                  k = j++;
                  res[j] = tmp[i];
                  k = j++;
                }
              else
                {
                  if (g_ascii_isspace (tmp[i]) || !g_ascii_isalnum (tmp[i]))
                    {
                      if (!(k >= 0 && res[k] == ' '))
                        {
                          res[j] = ' ';
                          k = j++;
                        }
                    }
                  else
                    {
                      res[j] = tmp[i];
                      k = j++;
                    }
                }
            }

          res[j] = '\0';

          result = g_strdup (res);
          g_free (tmp);
          g_free (res);
        }
    }

  return result;
}


/*
 * Find out type of the given printer driver.
 * (see xml/preferreddrivers.xml from system-config-printer)
 */
static gchar *
get_driver_type (gchar *ppd_name,
                 gchar *ppd_device_id,
                 gchar *ppd_make_and_model,
                 gchar *ppd_product,
                 gint   match_level)
{
  gchar *tmp = NULL;

  if (match_level == PPD_GENERIC_MATCH)
    {
      if (ppd_name && g_regex_match_simple ("(foomatic(-db-compressed-ppds)?|ijsgutenprint.*):", ppd_name, 0, 0))
        {
          tmp = get_tag_value ("DRV", ppd_device_id);
          if (tmp && g_regex_match_simple (".*,?R1", tmp, 0, 0))
            return g_strdup ("generic-foomatic-recommended");
        }
    }

  if (match_level == PPD_GENERIC_MATCH || match_level == PPD_NO_MATCH)
    {
      if (ppd_name && g_regex_match_simple ("(foomatic(-db-compressed-ppds)?|ijsgutenprint.*):Generic-ESC_P", ppd_name, 0, 0))
        return g_strdup ("generic-escp");

      if (ppd_name && g_regex_match_simple ("drv:///sample.drv/epson(9|24).ppd", ppd_name, 0, 0))
        return g_strdup ("generic-escp");

      if (g_strcmp0 (ppd_make_and_model, "Generic PostScript Printer") == 0)
        return g_strdup ("generic-postscript");

      if (g_strcmp0 (ppd_make_and_model, "Generic PCL 6 Printer") == 0)
        return g_strdup ("generic-pcl6");

      if (g_strcmp0 (ppd_make_and_model, "Generic PCL 5e Printer") == 0)
        return g_strdup ("generic-pcl5e");

      if (g_strcmp0 (ppd_make_and_model, "Generic PCL 5 Printer") == 0)
        return g_strdup ("generic-pcl5");

      if (g_strcmp0 (ppd_make_and_model, "Generic PCL Laser Printer") == 0)
        return g_strdup ("generic-pcl");

      return g_strdup ("generic");
    }


  if (ppd_name && g_regex_match_simple ("drv:///sample.drv/", ppd_name, 0, 0))
    return g_strdup ("cups");

  if (ppd_product && g_regex_match_simple (".*Ghostscript", ppd_product, 0, 0))
    return g_strdup ("ghostscript");

  if (ppd_name && g_regex_match_simple ("gutenprint.*:.*/simple|.*-gutenprint.*\\.sim", ppd_name, 0, 0))
    return g_strdup ("gutenprint-simplified");

  if (ppd_name && g_regex_match_simple ("gutenprint.*:|.*-gutenprint", ppd_name, 0, 0))
    return g_strdup ("gutenprint-expert");

  if (ppd_make_and_model && g_regex_match_simple (".* Foomatic/hpijs", ppd_make_and_model, 0, 0))
    {
      tmp = get_tag_value ("DRV", ppd_device_id);
      if (tmp && g_regex_match_simple (",?R1", tmp, 0, 0))
        return g_strdup ("foomatic-recommended-hpijs");
    }

  if (ppd_make_and_model && g_regex_match_simple (".* Foomatic/hpijs", ppd_make_and_model, 0, 0))
    return g_strdup ("foomatic-hpijs");

  if (ppd_name && g_regex_match_simple ("foomatic(-db-compressed-ppds)?:", ppd_name, 0, 0) &&
      ppd_make_and_model && g_regex_match_simple (".* Postscript", ppd_make_and_model, 0, 0))
    {
      tmp = get_tag_value ("DRV", ppd_device_id);
      if (tmp && g_regex_match_simple (".*,?R1", tmp, 0, 0))
        return g_strdup ("foomatic-recommended-postscript");
    }

  if (ppd_name && g_regex_match_simple ("foomatic(-db-compressed-ppds)?:.*-Postscript", ppd_name, 0, 0))
    return g_strdup ("foomatic-postscript");

  if (ppd_make_and_model && g_regex_match_simple (".* Foomatic/pxlmono", ppd_make_and_model, 0, 0))
    return g_strdup ("foomatic-pxlmono");

  if (ppd_name && g_regex_match_simple ("(foomatic(-db-compressed-ppds)?|ijsgutenprint.*):", ppd_name, 0, 0))
    {
      tmp = get_tag_value ("DRV", ppd_device_id);
      if (tmp && g_regex_match_simple (".*,?R1", tmp, 0, 0))
        return g_strdup ("foomatic-recommended-non-postscript");
    }

  if (ppd_name && g_regex_match_simple ("(foomatic(-db-compressed-ppds)?|ijsgutenprint.*):.*-gutenprint", ppd_name, 0, 0))
    return g_strdup ("foomatic-gutenprint");

  if (ppd_name && g_regex_match_simple ("(foomatic(-db-compressed-ppds)?|ijsgutenprint.*):", ppd_name, 0, 0))
    return g_strdup ("foomatic");

  if (ppd_name && g_regex_match_simple ("drv:///(hp/)?hpcups.drv/|.*-hpcups", ppd_name, 0, 0) &&
      ppd_make_and_model && g_regex_match_simple (".* plugin", ppd_make_and_model, 0, 0))
    return g_strdup ("hpcups-plugin");

  if (ppd_name && g_regex_match_simple ("drv:///(hp/)?hpcups.drv/|.*-hpcups", ppd_name, 0, 0))
    return g_strdup ("hpcups");

  if (ppd_name && g_regex_match_simple ("drv:///(hp/)?hpijs.drv/|.*-hpijs", ppd_name, 0, 0) &&
      ppd_make_and_model && g_regex_match_simple (".* plugin", ppd_make_and_model, 0, 0))
    return g_strdup ("hpijs-plugin");

  if (ppd_name && g_regex_match_simple ("drv:///(hp/)?hpijs.drv/|.*-hpijs", ppd_name, 0, 0))
    return g_strdup ("hpijs");

  if (ppd_name && g_regex_match_simple (".*splix", ppd_name, 0, 0))
    return g_strdup ("splix");

  if (ppd_name && g_regex_match_simple (".*turboprint", ppd_name, 0, 0))
    return g_strdup ("turboprint");

  if (ppd_name && g_regex_match_simple (".*/(Ricoh|Lanier|Gestetner|InfoPrint|Infotech|Savin|NRG)/PS/", ppd_name, 0, 0))
    return g_strdup ("manufacturer-ricoh-postscript");

  if (ppd_name && g_regex_match_simple (".*/(Ricoh|Lanier|Gestetner|InfoPrint|Infotech|Savin|NRG)/PXL/", ppd_name, 0, 0))
    return g_strdup ("manufacturer-ricoh-pxl");

  if (match_level == PPD_EXACT_CMD_MATCH)
    return g_strdup ("manufacturer-cmd");

  return g_strdup ("manufacturer");
}


/*
 * Return preference value. The most preferred driver has the lowest value.
 * If the value is higher or equal to 1000 then try to avoid installation
 * of this driver. If it is higher or equal to 2000 then don't install
 * this driver.
 * (see xml/preferreddrivers.xml from system-config-printer)
 */
static gint
get_driver_preference (PPDItem *item)
{
  gchar   *tmp1 = NULL;
  gchar   *tmp2 = NULL;
  gint     result = 0;

  if (item && item->ppd_make_and_model &&
      g_regex_match_simple ("Brother HL-2030", item->ppd_make_and_model, 0, 0))
    {
      tmp1 = get_tag_value ("MFG", item->ppd_device_id);
      tmp2 = get_tag_value ("MDL", item->ppd_device_id);

      if (tmp1 && g_regex_match_simple ("Brother", tmp1, 0, 0) &&
          tmp2 && g_regex_match_simple ("HL-2030", tmp2, 0, 0))
        {
          if (item->driver_type && g_regex_match_simple ("gutenprint*", item->driver_type, 0, 0))
            return result + 2000;
          else
            return result;
        }
    }
  result++;

  if (item && item->ppd_make_and_model &&
      g_regex_match_simple ("(Ricoh|Lanier|Gestetner|InfoPrint|Infotech|Savin|NRG) ", item->ppd_make_and_model, 0, 0))
    {
      tmp1 = get_tag_value ("MFG", item->ppd_device_id);

      if (tmp1 && g_regex_match_simple ("(Ricoh|Lanier|Gestetner|InfoPrint|Infotech|Savin|NRG)", tmp1, 0, 0) &&
          ((g_strcmp0 (item->driver_type, "manufacturer-ricoh-postscript") == 0) ||
           (g_strcmp0 (item->driver_type, "manufacturer-ricoh-pxl") == 0)))
        return result;
    }
  result++;

  if (item && item->ppd_make_and_model &&
      g_regex_match_simple ("Xerox 6250DP", item->ppd_make_and_model, 0, 0))
    {
      tmp1 = get_tag_value ("MFG", item->ppd_device_id);
      tmp2 = get_tag_value ("MDL", item->ppd_device_id);

      if (tmp1 && g_regex_match_simple ("Xerox", tmp1, 0, 0) &&
          tmp2 && g_regex_match_simple ("6250DP", tmp2, 0, 0))
        {
          if (item->driver_type && g_regex_match_simple ("gutenprint*", item->driver_type, 0, 0))
            return result + 1000;
          else
            return result;
        }
    }
  result++;

  if (item && g_strcmp0 (item->driver_type, "manufacturer-cmd") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "foomatic-recommended-hpijs") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "foomatic-recommended-non-postscript") == 0)
    return result;
  result++;

  if (item && item->driver_type &&
      g_regex_match_simple ("manufacturer*", item->driver_type, 0, 0))
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "foomatic-recommended-postscript") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "foomatic-postscript") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "hpcups") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "hpijs") == 0)
    return result;
  result++;

  if (item && item->ppd_make_and_model &&
      g_regex_match_simple ("(HP|Hewlett-Packard) ", item->ppd_make_and_model, 0, 0))
    {
      tmp1 = get_tag_value ("MFG", item->ppd_device_id);

      if (tmp1 && g_regex_match_simple ("HP|Hewlett-Packard", tmp1, 0, 0) &&
          g_strcmp0 (item->driver_type, "foomatic-hpijs") == 0)
        return result;
    }
  result++;

  if (item && g_strcmp0 (item->driver_type, "gutenprint-simplified") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "gutenprint-expert") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "foomatic-hpijs") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "foomatic-gutenprint") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "foomatic") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "cups") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "generic-postscript") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "generic-foomatic-recommended") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "generic-pcl6") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "generic-pcl5c") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "generic-pcl5e") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "generic-pcl5") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "generic-pcl") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "foomatic-pxlmono") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "generic-escp") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "ghostscript") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "generic") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "hpcups-plugin") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "hpijs-plugin") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "splix") == 0)
    return result;
  result++;

  if (item && g_strcmp0 (item->driver_type, "turboprint") == 0)
    return result;
  result++;

  return result;
}


/*
 * Compare driver types according to preference order.
 * The most preferred driver is the lowest one.
 * (see xml/preferreddrivers.xml from system-config-printer)
 */
static gint
preference_value_cmp (gconstpointer a,
                      gconstpointer b)
{
  PPDItem *c = (PPDItem *) a;
  PPDItem *d = (PPDItem *) b;

  if (c == NULL && d == NULL)
    return 0;
  else if (c == NULL)
    return -1;
  else if (d == NULL)
    return 1;

  if (c->preference_value < d->preference_value)
    return -1;
  else if (c->preference_value > d->preference_value)
    return 1;
  else
    return 0;
}


/* Compare PPDItems a and b according to normalized model name */
static gint
item_cmp (gconstpointer a,
          gconstpointer b)
{
  PPDItem *c = (PPDItem *) a;
  PPDItem *d = (PPDItem *) b;
  glong   a_number;
  glong   b_number;
  gchar  *a_normalized = NULL;
  gchar  *b_normalized = NULL;
  gchar **av = NULL;
  gchar **bv = NULL;
  gint    a_length = 0;
  gint    b_length = 0;
  gint    min_length;
  gint    result = 0;
  gint    i;

  if (c && d)
    {
      a_normalized = normalize (c->mdl);
      b_normalized = normalize (d->mdl);

      if (a_normalized)
        av = g_strsplit (a_normalized, " ", 0);

      if (b_normalized)
        bv = g_strsplit (b_normalized, " ", 0);

      if (av)
        a_length = g_strv_length (av);

      if (bv)
        b_length = g_strv_length (bv);

      min_length = a_length < b_length ? a_length : b_length;

      for (i = 0; i < min_length; i++)
        {
          if (g_ascii_isdigit (av[i][0]) && g_ascii_isdigit (bv[i][0]))
            {
              a_number = atol (av[i]);
              b_number = atol (bv[i]);
              if (a_number < b_number)
                {
                  result = -1;
                  goto out;
                }
              else if (a_number > b_number)
                {
                  result = 1;
                  goto out;
                }
            }
          else if (g_ascii_isdigit (av[i][0]) && !g_ascii_isdigit (bv[i][0]))
            {
              result = -1;
              goto out;
            }
          else if (!g_ascii_isdigit (av[i][0]) && g_ascii_isdigit (bv[i][0]))
            {
              result = 1;
              goto out;
            }
          else
            {
              if (g_strcmp0 (av[i], bv[i]) != 0)
                {
                  result = g_strcmp0 (av[i], bv[i]);
                  goto out;
                }
            }
        }

      if (a_length < b_length)
        result = -1;
      else if (a_length > b_length)
        result = 1;
    }

out:
  if (av)
    g_strfreev (av);

  if (bv)
    g_strfreev (bv);

  g_free (a_normalized);
  g_free (b_normalized);

  return result;
}


static gint
get_prefix_length (gchar *a, gchar *b)
{
  gint a_length;
  gint b_length;
  gint min_length;
  gint i;

  if (a && b)
    {
      a_length = strlen (a);
      b_length = strlen (b);
      min_length = a_length < b_length ? a_length : b_length;

      for (i = 0; i < min_length; i++)
        {
          if (a[i] != b[i])
            return i;
        }
      return min_length;
    }

  return 0;
}


/*
 * Append best matching ppds from list "ppds" to list "list"
 * according to model name "model". Return the resulting list.
 */
static GList *
append_best_ppds (GList *list,
                  GList *ppds,
                  gchar *model)
{
  PPDItem *item;
  PPDItem *tmp_item;
  PPDItem *best_item = NULL;
  gchar   *mdl_normalized;
  gchar   *mdl;
  gchar   *tmp;
  GList   *local_ppds;
  GList   *actual_item;
  GList   *candidates = NULL;
  GList   *tmp_list = NULL;
  GList   *result = NULL;
  gint     best_prefix_length = -1;
  gint     prefix_length;

  result = list;

  if (model)
    {
      mdl = g_ascii_strdown (model, -1);
      if (g_str_has_suffix (mdl, " series"))
        {
          tmp = g_strndup (mdl, strlen (mdl) - 7);
          g_free (mdl);
          mdl = tmp;
        }

      mdl_normalized = normalize (mdl);

      item = g_new0 (PPDItem, 1);
      item->ppd_device_id = g_strdup_printf ("mdl:%s;", mdl);
      item->mdl = mdl_normalized;

      local_ppds = g_list_copy (ppds);
      local_ppds = g_list_append (local_ppds, item);
      local_ppds = g_list_sort (local_ppds, item_cmp);

      actual_item = g_list_find (local_ppds, item);
      if (actual_item)
        {
          if (actual_item->prev)
            candidates = g_list_append (candidates, actual_item->prev->data);
          if (actual_item->next)
            candidates = g_list_append (candidates, actual_item->next->data);
        }

      for (tmp_list = candidates; tmp_list; tmp_list = tmp_list->next)
        {
          tmp_item = (PPDItem *) tmp_list->data;

          prefix_length = get_prefix_length (tmp_item->mdl, mdl_normalized);
          if (prefix_length > best_prefix_length)
            {
              best_prefix_length = prefix_length;
              best_item = tmp_item;
            }
        }

      if (best_item && best_prefix_length > strlen (mdl_normalized) / 2)
        {
          if (best_prefix_length == strlen (mdl_normalized))
            best_item->match_level = PPD_EXACT_MATCH;
          else
            best_item->match_level = PPD_CLOSE_MATCH;

          result = g_list_append (result, ppd_item_copy (best_item));
        }
      else
        {
          /* TODO the last resort (see _findBestMatchPPDs() in ppds.py) */
        }

      g_list_free (candidates);
      g_list_free (local_ppds);

      g_free (item->ppd_device_id);
      g_free (item);
      g_free (mdl);
      g_free (mdl_normalized);
    }

  return result;
}

/*
 * Return the best matching driver name
 * for device described by given parameters.
 */
PPDName *
get_ppd_name (gchar *device_id,
              gchar *device_make_and_model,
              gchar *device_uri)
{
  GDBusConnection *bus;
  GVariant   *output;
  GVariant   *array;
  GVariant   *tuple;
  PPDName    *result = NULL;
  GError     *error = NULL;
  gchar      *name, *match;
  gint        i, j;
  static const char * const match_levels[] = {
             "exact-cmd",
             "exact",
             "close",
             "generic",
             "none"};

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (bus)
    {
      output = g_dbus_connection_call_sync (bus,
                                            SCP_BUS,
                                            SCP_PATH,
                                            SCP_IFACE,
                                            "GetBestDrivers",
                                            g_variant_new ("(sss)",
                                                           device_id ? device_id : "",
                                                           device_make_and_model ? device_make_and_model : "",
                                                           device_uri ? device_uri : ""),
                                            NULL,
                                            G_DBUS_CALL_FLAGS_NONE,
                                            60000,
                                            NULL,
                                            &error);

      if (output && g_variant_n_children (output) >= 1)
        {
          array = g_variant_get_child_value (output, 0);
          if (array)
            for (j = 0; j < G_N_ELEMENTS (match_levels) && result == NULL; j++)
              for (i = 0; i < g_variant_n_children (array) && result == NULL; i++)
                {
                  tuple = g_variant_get_child_value (array, i);
                  if (tuple && g_variant_n_children (tuple) == 2)
                    {
                      name = g_strdup (g_variant_get_string (
                                         g_variant_get_child_value (tuple, 0),
                                         NULL));
                      match = g_strdup (g_variant_get_string (
                                          g_variant_get_child_value (tuple, 1),
                                          NULL));

                      if (g_strcmp0 (match, match_levels[j]) == 0)
                        {
                          result = g_new0 (PPDName, 1);
                          result->ppd_name = g_strdup (name);

                          if (g_strcmp0 (match, "exact-cmd") == 0)
                            result->ppd_match_level = PPD_EXACT_CMD_MATCH;
                          else if (g_strcmp0 (match, "exact") == 0)
                            result->ppd_match_level = PPD_EXACT_MATCH;
                          else if (g_strcmp0 (match, "close") == 0)
                            result->ppd_match_level = PPD_CLOSE_MATCH;
                          else if (g_strcmp0 (match, "generic") == 0)
                            result->ppd_match_level = PPD_GENERIC_MATCH;
                          else if (g_strcmp0 (match, "none") == 0)
                            result->ppd_match_level = PPD_NO_MATCH;
                        }

                      g_free (match);
                      g_free (name);
                    }
                }
        }

      if (output)
        g_variant_unref (output);
      g_object_unref (bus);
    }

  if (bus == NULL ||
      (error &&
       error->domain == G_DBUS_ERROR &&
       (error->code == G_DBUS_ERROR_SERVICE_UNKNOWN ||
        error->code == G_DBUS_ERROR_UNKNOWN_METHOD)))
    {
      ipp_attribute_t *attr = NULL;
      const gchar     *hp_equivalents[] = {"hp", "hewlett packard", NULL};
      const gchar     *kyocera_equivalents[] = {"kyocera", "kyocera mita", NULL};
      const gchar     *toshiba_equivalents[] = {"toshiba", "toshiba tec corp.", NULL};
      const gchar     *lexmark_equivalents[] = {"lexmark", "lexmark international", NULL};
      gboolean         ppd_exact_match_found = FALSE;
      PPDItem         *item;
      http_t          *http = NULL;
      ipp_t           *request = NULL;
      ipp_t           *response = NULL;
      GList           *tmp_list;
      GList           *tmp_list2;
      GList           *mdls = NULL;
      GList           *list = NULL;
      gchar           *mfg_normalized = NULL;
      gchar           *mdl_normalized = NULL;
      gchar           *eq_normalized = NULL;
      gchar           *mfg = NULL;
      gchar           *mdl = NULL;
      gchar           *tmp = NULL;
      gchar           *ppd_device_id;
      gchar           *ppd_make_and_model;
      gchar           *ppd_name;
      gchar           *ppd_product;
      gchar          **equivalents = NULL;
      gint             i;

      g_warning ("You should install system-config-printer which provides \
DBus method \"GetBestDrivers\". Using fallback solution for now.");
      g_error_free (error);

      mfg = get_tag_value (device_id, "mfg");
      if (!mfg)
        mfg = get_tag_value (device_id, "manufacturer");

      mdl = get_tag_value (device_id, "mdl");
      if (!mdl)
        mdl = get_tag_value (device_id, "model");

      mfg_normalized = normalize (mfg);
      mdl_normalized = normalize (mdl);

      if (mfg_normalized && mfg)
        {
          if (g_str_has_prefix (mfg_normalized, "hewlett") ||
              g_str_has_prefix (mfg_normalized, "hp"))
            equivalents = strvdup (hp_equivalents);

          if (g_str_has_prefix (mfg_normalized, "kyocera"))
            equivalents = strvdup (kyocera_equivalents);

          if (g_str_has_prefix (mfg_normalized, "toshiba"))
            equivalents = strvdup (toshiba_equivalents);

          if (g_str_has_prefix (mfg_normalized, "lexmark"))
            equivalents = strvdup (lexmark_equivalents);

          if (equivalents == NULL)
            {
              equivalents = g_new0 (gchar *, 2);
              equivalents[0] = g_strdup (mfg);
            }
        }

      http = httpConnectEncrypt (cupsServer (),
                                 ippPort (),
                                 cupsEncryption ());

      /* Find usable drivers for given device */
      if (http)
        {
          /* Try exact match according to device-id */
          if (device_id)
            {
              request = ippNewRequest (CUPS_GET_PPDS);
              ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_TEXT,
                            "ppd-device-id", NULL, device_id);
              response = cupsDoRequest (http, request, "/");

              if (response &&
                  response->request.status.status_code <= IPP_OK_CONFLICT)
                {
                  for (attr = response->attrs; attr != NULL; attr = attr->next)
                    {
                      while (attr != NULL && attr->group_tag != IPP_TAG_PRINTER)
                        attr = attr->next;

                      if (attr == NULL)
                        break;

                      ppd_device_id = NULL;
                      ppd_make_and_model = NULL;
                      ppd_name = NULL;
                      ppd_product = NULL;

                      while (attr != NULL && attr->group_tag == IPP_TAG_PRINTER)
                        {
                          if (g_strcmp0 (attr->name, "ppd-device-id") == 0 &&
                              attr->value_tag == IPP_TAG_TEXT)
                            ppd_device_id = attr->values[0].string.text;
                          else if (g_strcmp0 (attr->name, "ppd-make-and-model") == 0 &&
                                   attr->value_tag == IPP_TAG_TEXT)
                            ppd_make_and_model = attr->values[0].string.text;
                          else if (g_strcmp0 (attr->name, "ppd-name") == 0 &&
                                   attr->value_tag == IPP_TAG_NAME)
                            ppd_name = attr->values[0].string.text;
                          else if (g_strcmp0 (attr->name, "ppd-product") == 0 &&
                                   attr->value_tag == IPP_TAG_TEXT)
                            ppd_product = attr->values[0].string.text;

                          attr = attr->next;
                        }

                      if (ppd_device_id && ppd_name)
                        {
                          item = g_new0 (PPDItem, 1);
                          item->ppd_name = g_strdup (ppd_name);
                          item->ppd_device_id = g_strdup (ppd_device_id);
                          item->ppd_make_and_model = g_strdup (ppd_make_and_model);
                          item->ppd_product = g_strdup (ppd_product);

                          tmp = get_tag_value (ppd_device_id, "mfg");
                          if (!tmp)
                            tmp = get_tag_value (ppd_device_id, "manufacturer");
                          item->mfg = normalize (tmp);
                          g_free (tmp);

                          tmp = get_tag_value (ppd_device_id, "mdl");
                          if (!tmp)
                            tmp = get_tag_value (ppd_device_id, "model");
                          item->mdl = normalize (tmp);
                          g_free (tmp);

                          item->match_level = PPD_EXACT_CMD_MATCH;
                          ppd_exact_match_found = TRUE;
                          list = g_list_append (list, item);
                        }

                      if (attr == NULL)
                        break;
                    }
                }

              if (response)
                ippDelete(response);
            }

          /* Try match according to manufacturer and model fields */
          if (!ppd_exact_match_found && mfg_normalized && mdl_normalized)
            {
              request = ippNewRequest (CUPS_GET_PPDS);
              response = cupsDoRequest (http, request, "/");

              if (response &&
                  response->request.status.status_code <= IPP_OK_CONFLICT)
                {
                  for (i = 0; equivalents && equivalents[i]; i++)
                    {
                      eq_normalized = normalize (equivalents[i]);
                      for (attr = response->attrs; attr != NULL; attr = attr->next)
                        {
                          while (attr != NULL && attr->group_tag != IPP_TAG_PRINTER)
                            attr = attr->next;

                          if (attr == NULL)
                            break;

                          ppd_device_id = NULL;
                          ppd_make_and_model = NULL;
                          ppd_name = NULL;
                          ppd_product = NULL;

                          while (attr != NULL && attr->group_tag == IPP_TAG_PRINTER)
                            {
                              if (g_strcmp0 (attr->name, "ppd-device-id") == 0 &&
                                  attr->value_tag == IPP_TAG_TEXT)
                                ppd_device_id = attr->values[0].string.text;
                              else if (g_strcmp0 (attr->name, "ppd-make-and-model") == 0 &&
                                       attr->value_tag == IPP_TAG_TEXT)
                                ppd_make_and_model = attr->values[0].string.text;
                              else if (g_strcmp0 (attr->name, "ppd-name") == 0 &&
                                       attr->value_tag == IPP_TAG_NAME)
                                ppd_name = attr->values[0].string.text;
                              else if (g_strcmp0 (attr->name, "ppd-product") == 0 &&
                                       attr->value_tag == IPP_TAG_TEXT)
                                ppd_product = attr->values[0].string.text;

                              attr = attr->next;
                            }

                          if (ppd_device_id && ppd_name)
                            {
                              item = g_new0 (PPDItem, 1);
                              item->ppd_name = g_strdup (ppd_name);
                              item->ppd_device_id = g_strdup (ppd_device_id);
                              item->ppd_make_and_model = g_strdup (ppd_make_and_model);
                              item->ppd_product = g_strdup (ppd_product);

                              tmp = get_tag_value (ppd_device_id, "mfg");
                              if (!tmp)
                                tmp = get_tag_value (ppd_device_id, "manufacturer");
                              item->mfg = normalize (tmp);
                              g_free (tmp);

                              tmp = get_tag_value (ppd_device_id, "mdl");
                              if (!tmp)
                                tmp = get_tag_value (ppd_device_id, "model");
                              item->mdl = normalize (tmp);
                              g_free (tmp);

                              if (item->mdl && item->mfg &&
                                  g_ascii_strcasecmp (item->mdl, mdl_normalized) == 0 &&
                                  g_ascii_strcasecmp (item->mfg, eq_normalized) == 0)
                                {
                                  item->match_level = PPD_EXACT_MATCH;
                                  ppd_exact_match_found = TRUE;
                                }

                              if (item->match_level == PPD_EXACT_MATCH)
                                list = g_list_append (list, item);
                              else if (item->mfg &&
                                       g_ascii_strcasecmp (item->mfg, eq_normalized) == 0)
                                mdls = g_list_append (mdls, item);
                              else
                                {
                                  ppd_item_free (item);
                                  g_free (item);
                                }
                            }

                          if (attr == NULL)
                            break;
                        }

                      g_free (eq_normalized);
                    }
                }

              if (response)
                ippDelete(response);
            }

          httpClose (http);
        }

      if (list == NULL)
        list = append_best_ppds (list, mdls, mdl);

      /* Find out driver types for all listed drivers and set their preference values */
      for (tmp_list = list; tmp_list; tmp_list = tmp_list->next)
        {
          item = (PPDItem *) tmp_list->data;
          if (item)
            {
              item->driver_type = get_driver_type (item->ppd_name,
                                                   item->ppd_device_id,
                                                   item->ppd_make_and_model,
                                                   item->ppd_product,
                                                   item->match_level);
              item->preference_value = get_driver_preference (item);
            }
        }

      /* Sort driver list according to preference value */
      list = g_list_sort (list, preference_value_cmp);

      /* Split blacklisted drivers to tmp_list */
      for (tmp_list = list; tmp_list; tmp_list = tmp_list->next)
        {
          item = (PPDItem *) tmp_list->data;
          if (item && item->preference_value >= 2000)
            break;
        }

      /* Free tmp_list */
      if (tmp_list)
        {
          if (tmp_list->prev)
            tmp_list->prev->next = NULL;
          else
            list = NULL;

          tmp_list->prev = NULL;
          for (tmp_list2 = tmp_list; tmp_list2; tmp_list2 = tmp_list2->next)
            {
              item = (PPDItem *) tmp_list2->data;
              ppd_item_free (item);
              g_free (item);
            }

          g_list_free (tmp_list);
        }

      /* Free driver list and set the best one */
      if (list)
        {
          item = (PPDItem *) list->data;
          if (item)
            {
              result = g_new0 (PPDName, 1);
              result->ppd_name = g_strdup (item->ppd_name);
              result->ppd_match_level = item->match_level;
              switch (item->match_level)
                {
                  case PPD_GENERIC_MATCH:
                  case PPD_CLOSE_MATCH:
                    g_warning ("Found PPD does not match given device exactly!");
                    break;
                  default:
                    break;
                }
            }

          for (tmp_list = list; tmp_list; tmp_list = tmp_list->next)
            {
              item = (PPDItem *) tmp_list->data;
              ppd_item_free (item);
              g_free (item);
            }

          g_list_free (list);
        }

      if (mdls)
        {
          for (tmp_list = mdls; tmp_list; tmp_list = tmp_list->next)
            {
              item = (PPDItem *) tmp_list->data;
              ppd_item_free (item);
              g_free (item);
            }
          g_list_free (mdls);
        }

      g_free (mfg);
      g_free (mdl);
      g_free (mfg_normalized);
      g_free (mdl_normalized);
      if (equivalents)
        g_strfreev (equivalents);
    }

  return result;
}

char *
get_dest_attr (const char *dest_name,
               const char *attr)
{
  cups_dest_t *dests;
  int          num_dests;
  cups_dest_t *dest;
  const char  *value;
  char        *ret;

  if (dest_name == NULL)
          return NULL;

  ret = NULL;

  num_dests = cupsGetDests (&dests);
  if (num_dests < 1) {
          g_debug ("Unable to get printer destinations");
          return NULL;
  }

  dest = cupsGetDest (dest_name, NULL, num_dests, dests);
  if (dest == NULL) {
          g_debug ("Unable to find a printer named '%s'", dest_name);
          goto out;
  }

  value = cupsGetOption (attr, dest->num_options, dest->options);
  if (value == NULL) {
          g_debug ("Unable to get %s for '%s'", attr, dest_name);
          goto out;
  }
  ret = g_strdup (value);
out:
  cupsFreeDests (num_dests, dests);

  return ret;
}

ipp_t *
execute_maintenance_command (const char *printer_name,
                             const char *command,
                             const char *title)
{
  http_t *http;
  GError *error = NULL;
  ipp_t  *request = NULL;
  ipp_t  *response = NULL;
  gchar  *file_name = NULL;
  char   *uri;
  int     fd = -1;

  http = httpConnectEncrypt (cupsServer (),
                             ippPort (),
                             cupsEncryption ());

  if (!http)
    return NULL;

  request = ippNewRequest (IPP_PRINT_JOB);

  uri = g_strdup_printf ("ipp://localhost/printers/%s", printer_name);

  ippAddString (request,
                IPP_TAG_OPERATION,
                IPP_TAG_URI,
                "printer-uri",
                NULL,
                uri);

  g_free (uri);

  ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME, "job-name",
                NULL, title);

  ippAddString (request, IPP_TAG_JOB, IPP_TAG_MIMETYPE, "document-format",
                NULL, "application/vnd.cups-command");

  fd = g_file_open_tmp ("ccXXXXXX", &file_name, &error);

  if (fd != -1)
    {
      FILE *file;

      file = fdopen (fd, "w");
      fprintf (file, "#CUPS-COMMAND\n");
      fprintf (file, "%s\n", command);
      fclose (file);

      response = cupsDoFileRequest (http, request, "/", file_name);
      g_unlink (file_name);
    }
  else
    {
      g_warning ("%s", error->message);
      g_error_free (error);
    }

  g_free (file_name);
  httpClose (http);

  return response;
}

int
ccGetAllowedUsers (gchar ***allowed_users, const char *printer_name)
{
  const char * const   attrs[1] = { "requesting-user-name-allowed" };
  http_t              *http;
  ipp_t               *request = NULL;
  gchar              **users = NULL;
  ipp_t               *response;
  char                *uri;
  int                  num_allowed_users = 0;

  http = httpConnectEncrypt (cupsServer (),
                             ippPort (),
                             cupsEncryption ());

  if (!http)
    {
      *allowed_users = NULL;
      return 0;
    }

  request = ippNewRequest (IPP_GET_PRINTER_ATTRIBUTES);

  uri = g_strdup_printf ("ipp://localhost/printers/%s", printer_name);

  ippAddString (request,
                IPP_TAG_OPERATION,
                IPP_TAG_URI,
                "printer-uri",
                NULL,
                uri);

  g_free (uri);

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
      ippDelete(response);
    }
  httpClose (http);

  *allowed_users = users;
  return num_allowed_users;
}

gchar *
get_ppd_attribute (const gchar *ppd_file_name,
                   const gchar *attribute_name)
{
  ppd_file_t *ppd_file = NULL;
  ppd_attr_t *ppd_attr = NULL;
  gchar *result = NULL;

  if (ppd_file_name)
    {
      ppd_file = ppdOpenFile (ppd_file_name);

      if (ppd_file)
        {
          ppd_attr = ppdFindAttr (ppd_file, attribute_name, NULL);
          if (ppd_attr != NULL)
            result = g_strdup (ppd_attr->value);
          ppdClose (ppd_file);
        }
    }

  return result;
}

/* Cancels subscription of given id */
void
cancel_cups_subscription (gint id)
{
  http_t *http;
  ipp_t  *request;

  if (id >= 0 &&
      ((http = httpConnectEncrypt (cupsServer (), ippPort (),
                                  cupsEncryption ())) != NULL)) {
    request = ippNewRequest (IPP_CANCEL_SUBSCRIPTION);
    ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
                 "printer-uri", NULL, "/");
    ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
                 "requesting-user-name", NULL, cupsUser ());
    ippAddInteger (request, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
                  "notify-subscription-id", id);
    ippDelete (cupsDoRequest (http, request, "/"));
    httpClose (http);
  }
}

/* Returns id of renewed subscription or new id */
gint
renew_cups_subscription (gint id,
                         const char * const *events,
                         gint num_events,
                         gint lease_duration)
{
  ipp_attribute_t              *attr = NULL;
  http_t                       *http;
  ipp_t                        *request;
  ipp_t                        *response = NULL;
  gint                          result = -1;

  if ((http = httpConnectEncrypt (cupsServer (), ippPort (),
                                  cupsEncryption ())) == NULL) {
    g_debug ("Connection to CUPS server \'%s\' failed.", cupsServer ());
  }
  else {
    if (id >= 0) {
      request = ippNewRequest (IPP_RENEW_SUBSCRIPTION);
      ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
                   "printer-uri", NULL, "/");
      ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
                   "requesting-user-name", NULL, cupsUser ());
      ippAddInteger (request, IPP_TAG_OPERATION, IPP_TAG_INTEGER,
                    "notify-subscription-id", id);
      ippAddInteger (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
                    "notify-lease-duration", lease_duration);
      response = cupsDoRequest (http, request, "/");
      if (response != NULL &&
          response->request.status.status_code <= IPP_OK_CONFLICT) {
        if ((attr = ippFindAttribute (response, "notify-lease-duration",
                                      IPP_TAG_INTEGER)) == NULL)
          g_debug ("No notify-lease-duration in response!\n");
        else
          if (attr->values[0].integer == lease_duration)
            result = id;
      }
    }

    if (result < 0) {
      request = ippNewRequest (IPP_CREATE_PRINTER_SUBSCRIPTION);
      ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
                    "printer-uri", NULL, "/");
      ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_NAME,
                    "requesting-user-name", NULL, cupsUser ());
      ippAddStrings (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_KEYWORD,
                     "notify-events", num_events, NULL, events);
      ippAddString (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_KEYWORD,
                    "notify-pull-method", NULL, "ippget");
      ippAddString (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_URI,
                    "notify-recipient-uri", NULL, "dbus://");
      ippAddInteger (request, IPP_TAG_SUBSCRIPTION, IPP_TAG_INTEGER,
                     "notify-lease-duration", lease_duration);
      response = cupsDoRequest (http, request, "/");

      if (response != NULL &&
          response->request.status.status_code <= IPP_OK_CONFLICT) {
        if ((attr = ippFindAttribute (response, "notify-subscription-id",
                                      IPP_TAG_INTEGER)) == NULL)
          g_debug ("No notify-subscription-id in response!\n");
        else
          result = attr->values[0].integer;
      }
    }

    if (response)
      ippDelete (response);

    httpClose (http);
  }

  return result;
}

/*  Set default destination in ~/.cups/lpoptions.
 *  Unset default destination if "dest" is NULL.
 */
void
set_local_default_printer (const gchar *printer_name)
{
  cups_dest_t *dests = NULL;
  int          num_dests = 0;
  int          i;

  num_dests = cupsGetDests (&dests);

  for (i = 0; i < num_dests; i ++)
    {
      if (printer_name && g_strcmp0 (dests[i].name, printer_name) == 0)
        dests[i].is_default = 1;
      else
        dests[i].is_default = 0;
    }

  cupsSetDests (num_dests, dests);
}

/*
 * This function does something which should be provided by CUPS...
 * It returns FALSE if the renaming fails.
 */
gboolean
printer_rename (const gchar *old_name,
                const gchar *new_name)
{
  ipp_attribute_t  *attr = NULL;
  cups_ptype_t      printer_type = 0;
  cups_dest_t      *dests = NULL;
  cups_dest_t      *dest = NULL;
  cups_job_t       *jobs = NULL;
  GDBusConnection  *bus;
  const char       *printer_location = NULL;
  const char       *ppd_filename = NULL;
  const char       *printer_info = NULL;
  const char       *printer_uri = NULL;
  const char       *device_uri = NULL;
  const char       *job_sheets = NULL;
  gboolean          result = FALSE;
  gboolean          accepting = TRUE;
  gboolean          printer_paused = FALSE;
  gboolean          default_printer = FALSE;
  gboolean          printer_shared = FALSE;
  GError           *error = NULL;
  http_t           *http;
  gchar           **sheets = NULL;
  gchar           **users_allowed = NULL;
  gchar           **users_denied = NULL;
  gchar           **member_names = NULL;
  gchar            *start_sheet = NULL;
  gchar            *end_sheet = NULL;
  gchar            *error_policy = NULL;
  gchar            *op_policy = NULL;
  ipp_t            *request;
  ipp_t            *response;
  gint              i;
  int               num_dests = 0;
  int               num_jobs = 0;
  static const char * const requested_attrs[] = {
    "printer-error-policy",
    "printer-op-policy",
    "requesting-user-name-allowed",
    "requesting-user-name-denied",
    "member-names"};

  if (old_name == NULL ||
      old_name[0] == '\0' ||
      new_name == NULL ||
      new_name[0] == '\0' ||
      g_strcmp0 (old_name, new_name) == 0)
    return FALSE;

  num_dests = cupsGetDests (&dests);

  dest = cupsGetDest (new_name, NULL, num_dests, dests);
  if (dest)
    {
      cupsFreeDests (num_dests, dests);
      return FALSE;
    }

  num_jobs = cupsGetJobs (&jobs, old_name, 0, CUPS_WHICHJOBS_ACTIVE);
  cupsFreeJobs (num_jobs, jobs);
  if (num_jobs > 1)
    {
      g_warning ("There are queued jobs on printer %s!", old_name);
      cupsFreeDests (num_dests, dests);
      return FALSE;
    }

  /*
   * Gather some informations about the original printer
   */
  dest = cupsGetDest (old_name, NULL, num_dests, dests);
  if (dest)
    {
      for (i = 0; i < dest->num_options; i++)
        {
          if (g_strcmp0 (dest->options[i].name, "printer-is-accepting-jobs") == 0)
            accepting = g_strcmp0 (dest->options[i].value, "true") == 0;
          else if (g_strcmp0 (dest->options[i].name, "printer-is-shared") == 0)
            printer_shared = g_strcmp0 (dest->options[i].value, "true") == 0;
          else if (g_strcmp0 (dest->options[i].name, "device-uri") == 0)
            device_uri = dest->options[i].value;
          else if (g_strcmp0 (dest->options[i].name, "printer-uri-supported") == 0)
            printer_uri = dest->options[i].value;
          else if (g_strcmp0 (dest->options[i].name, "printer-info") == 0)
            printer_info = dest->options[i].value;
          else if (g_strcmp0 (dest->options[i].name, "printer-location") == 0)
            printer_location = dest->options[i].value;
          else if (g_strcmp0 (dest->options[i].name, "printer-state") == 0)
            printer_paused = g_strcmp0 (dest->options[i].value, "5") == 0;
          else if (g_strcmp0 (dest->options[i].name, "job-sheets") == 0)
            job_sheets = dest->options[i].value;
          else if (g_strcmp0 (dest->options[i].name, "printer-type") == 0)
            printer_type = atoi (dest->options[i].value);
        }
      default_printer = dest->is_default;
    }
  cupsFreeDests (num_dests, dests);

  if (accepting)
    {
      printer_set_accepting_jobs (old_name, FALSE, NULL);

      num_jobs = cupsGetJobs (&jobs, old_name, 0, CUPS_WHICHJOBS_ACTIVE);
      cupsFreeJobs (num_jobs, jobs);
      if (num_jobs > 1)
        {
          printer_set_accepting_jobs (old_name, accepting, NULL);
          g_warning ("There are queued jobs on printer %s!", old_name);
          return FALSE;
        }
    }


  /*
   * Gather additional informations about the original printer
   */
  if ((http = httpConnectEncrypt (cupsServer (), ippPort (),
                                  cupsEncryption ())) != NULL)
    {
      request = ippNewRequest (IPP_GET_PRINTER_ATTRIBUTES);
      ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
                    "printer-uri", NULL, printer_uri);
      ippAddStrings (request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                     "requested-attributes", G_N_ELEMENTS (requested_attrs), NULL, requested_attrs);
      response = cupsDoRequest (http, request, "/");

      if (response)
        {
          if (response->request.status.status_code <= IPP_OK_CONFLICT)
            {
              attr = ippFindAttribute (response, "printer-error-policy", IPP_TAG_NAME);
              if (attr)
                error_policy = g_strdup (attr->values[0].string.text);

              attr = ippFindAttribute (response, "printer-op-policy", IPP_TAG_NAME);
              if (attr)
                op_policy = g_strdup (attr->values[0].string.text);

              attr = ippFindAttribute (response, "requesting-user-name-allowed", IPP_TAG_NAME);
              if (attr && attr->num_values > 0)
                {
                  users_allowed = g_new0 (gchar *, attr->num_values + 1);
                  for (i = 0; i < attr->num_values; i++)
                    users_allowed[i] = g_strdup (attr->values[i].string.text);
                }

              attr = ippFindAttribute (response, "requesting-user-name-denied", IPP_TAG_NAME);
              if (attr && attr->num_values > 0)
                {
                  users_denied = g_new0 (gchar *, attr->num_values + 1);
                  for (i = 0; i < attr->num_values; i++)
                    users_denied[i] = g_strdup (attr->values[i].string.text);
                }

              attr = ippFindAttribute (response, "member-names", IPP_TAG_NAME);
              if (attr && attr->num_values > 0)
                {
                  member_names = g_new0 (gchar *, attr->num_values + 1);
                  for (i = 0; i < attr->num_values; i++)
                    member_names[i] = g_strdup (attr->values[i].string.text);
                }
            }
          ippDelete (response);
        }
      httpClose (http);
    }

  if (job_sheets)
    {
      sheets = g_strsplit (job_sheets, ",", 0);
      if (g_strv_length (sheets) > 1)
        {
          start_sheet = sheets[0];
          end_sheet = sheets[1];
        }
    }

  ppd_filename = cupsGetPPD (old_name);

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
   {
     g_warning ("Failed to get system bus: %s", error->message);
     g_error_free (error);
   }
  else
    {
      if (printer_type & CUPS_PRINTER_CLASS)
        {
          if (member_names)
            for (i = 0; i < g_strv_length (member_names); i++)
              class_add_printer (new_name, member_names[i]);
        }
      else
        {
          GVariant *output;

          output = g_dbus_connection_call_sync (bus,
                                                MECHANISM_BUS,
                                                "/",
                                                MECHANISM_BUS,
                                                "PrinterAddWithPpdFile",
                                                g_variant_new ("(sssss)",
                                                               new_name,
                                                               device_uri ? device_uri : "",
                                                               ppd_filename ? ppd_filename : "",
                                                               printer_info ? printer_info : "",
                                                               printer_location ? printer_location : ""),
                                                G_VARIANT_TYPE ("(s)"),
                                                G_DBUS_CALL_FLAGS_NONE,
                                                -1,
                                                NULL,
                                                &error);
          g_object_unref (bus);

          if (output)
            {
              const gchar *ret_error;

              g_variant_get (output, "(&s)", &ret_error);
              if (ret_error[0] != '\0')
                g_warning ("%s", ret_error);

              g_variant_unref (output);
            }
          else
            {
              g_warning ("%s", error->message);
              g_error_free (error);
            }
        }
    }

  if (ppd_filename)
    g_unlink (ppd_filename);

  num_dests = cupsGetDests (&dests);
  dest = cupsGetDest (new_name, NULL, num_dests, dests);
  if (dest)
    {
      printer_set_accepting_jobs (new_name, accepting, NULL);
      printer_set_enabled (new_name, !printer_paused);
      printer_set_shared (new_name, printer_shared);
      printer_set_job_sheets (new_name, start_sheet, end_sheet);
      printer_set_policy (new_name, op_policy, FALSE);
      printer_set_policy (new_name, error_policy, TRUE);
      printer_set_users (new_name, users_allowed, TRUE);
      printer_set_users (new_name, users_denied, FALSE);
      if (default_printer)
        printer_set_default (new_name);

      printer_delete (old_name);

      result = TRUE;
    }
  else
    printer_set_accepting_jobs (old_name, accepting, NULL);

  cupsFreeDests (num_dests, dests);
  g_free (op_policy);
  g_free (error_policy);
  if (sheets)
    g_strfreev (sheets);
  if (users_allowed)
    g_strfreev (users_allowed);
  if (users_denied)
    g_strfreev (users_denied);

  return result;
}

gboolean
printer_set_location (const gchar *printer_name,
                      const gchar *location)
{
  GDBusConnection *bus;
  GVariant   *output;
  gboolean    result = FALSE;
  GError     *error = NULL;

  if (!printer_name || !location)
    return TRUE;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
   {
     g_warning ("Failed to get system bus: %s", error->message);
     g_error_free (error);
     return TRUE;
   }

  output = g_dbus_connection_call_sync (bus,
                                        MECHANISM_BUS,
                                        "/",
                                        MECHANISM_BUS,
                                        "PrinterSetLocation",
                                        g_variant_new ("(ss)", printer_name, location),
                                        G_VARIANT_TYPE ("(s)"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &error);
  g_object_unref (bus);

  if (output)
    {
      const gchar *ret_error;

      g_variant_get (output, "(&s)", &ret_error);
      if (ret_error[0] != '\0')
        g_warning ("%s", ret_error);
      else
        result = TRUE;

      g_variant_unref (output);
    }
  else
    {
      g_warning ("%s", error->message);
      g_error_free (error);
    }

  return result;
}

gboolean
printer_set_accepting_jobs (const gchar *printer_name,
                            gboolean     accepting_jobs,
                            const gchar *reason)
{
  GDBusConnection *bus;
  GVariant   *output;
  gboolean    result = FALSE;
  GError     *error = NULL;

  if (!printer_name)
    return TRUE;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
   {
     g_warning ("Failed to get system bus: %s", error->message);
     g_error_free (error);
     return TRUE;
   }

  output = g_dbus_connection_call_sync (bus,
                                        MECHANISM_BUS,
                                        "/",
                                        MECHANISM_BUS,
                                        "PrinterSetAcceptJobs",
                                        g_variant_new ("(sbs)",
                                                       printer_name,
                                                       accepting_jobs,
                                                       reason ? reason : ""),
                                        G_VARIANT_TYPE ("(s)"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &error);
  g_object_unref (bus);

  if (output)
    {
      const gchar *ret_error;

      g_variant_get (output, "(&s)", &ret_error);
      if (ret_error[0] != '\0')
        g_warning ("%s", ret_error);
      else
        result = TRUE;
      g_variant_unref (output);
    }
  else
    {
      g_warning ("%s", error->message);
      g_error_free (error);
    }

  return result;
}

gboolean
printer_set_enabled (const gchar *printer_name,
                     gboolean     enabled)
{
  GDBusConnection *bus;
  GVariant   *output;
  gboolean    result = FALSE;
  GError     *error = NULL;

  if (!printer_name)
    return TRUE;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
   {
     g_warning ("Failed to get system bus: %s", error->message);
     g_error_free (error);
     return TRUE;
   }

  output = g_dbus_connection_call_sync (bus,
                                        MECHANISM_BUS,
                                        "/",
                                        MECHANISM_BUS,
                                        "PrinterSetEnabled",
                                        g_variant_new ("(sb)", printer_name, enabled),
                                        G_VARIANT_TYPE ("(s)"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &error);
  g_object_unref (bus);

  if (output)
    {
      const gchar *ret_error;

      g_variant_get (output, "(&s)", &ret_error);
      if (ret_error[0] != '\0')
        g_warning ("%s", ret_error);
      else
        result = TRUE;

      g_variant_unref (output);
    }
  else
    {
      g_warning ("%s", error->message);
      g_error_free (error);
    }

  return result;
}

gboolean
printer_delete (const gchar *printer_name)
{
  GDBusConnection *bus;
  GVariant   *output;
  gboolean    result = FALSE;
  GError     *error = NULL;

  if (!printer_name)
    return TRUE;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
   {
     g_warning ("Failed to get system bus: %s", error->message);
     g_error_free (error);
     return TRUE;
   }

  output = g_dbus_connection_call_sync (bus,
                                        MECHANISM_BUS,
                                        "/",
                                        MECHANISM_BUS,
                                        "PrinterDelete",
                                        g_variant_new ("(s)", printer_name),
                                        G_VARIANT_TYPE ("(s)"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &error);
  g_object_unref (bus);

  if (output)
    {
      const gchar *ret_error;

      g_variant_get (output, "(&s)", &ret_error);
      if (ret_error[0] != '\0')
        g_warning ("%s", ret_error);
      else
        result = TRUE;

      g_variant_unref (output);
    }
  else
    {
      g_warning ("%s", error->message);
      g_error_free (error);
    }

  return result;
}

gboolean
printer_set_default (const gchar *printer_name)
{
  GDBusConnection *bus;
  const char *cups_server;
  GVariant   *output;
  gboolean    result = FALSE;
  GError     *error = NULL;

  if (!printer_name)
    return TRUE;

  cups_server = cupsServer ();
  if (g_ascii_strncasecmp (cups_server, "localhost", 9) == 0 ||
      g_ascii_strncasecmp (cups_server, "127.0.0.1", 9) == 0 ||
      g_ascii_strncasecmp (cups_server, "::1", 3) == 0 ||
      cups_server[0] == '/')
    {
      /* Clean .cups/lpoptions before setting
       * default printer on local CUPS server.
       */
      set_local_default_printer (NULL);

      bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
      if (!bus)
        {
          g_warning ("Failed to get system bus: %s", error->message);
          g_error_free (error);
        }
      else
        {
          output = g_dbus_connection_call_sync (bus,
                                                MECHANISM_BUS,
                                                "/",
                                                MECHANISM_BUS,
                                                "PrinterSetDefault",
                                                g_variant_new ("(s)", printer_name),
                                                G_VARIANT_TYPE ("(s)"),
                                                G_DBUS_CALL_FLAGS_NONE,
                                                -1,
                                                NULL,
                                                &error);
          g_object_unref (bus);

          if (output)
            {
              const gchar *ret_error;

              g_variant_get (output, "(&s)", &ret_error);
              if (ret_error[0] != '\0')
                g_warning ("%s", ret_error);
              else
                result = TRUE;

              g_variant_unref (output);
            }
          else
            {
              g_warning ("%s", error->message);
              g_error_free (error);
            }
        }
    }
  else
    /* Store default printer to .cups/lpoptions
     * if we are connected to a remote CUPS server.
     */
    {
      set_local_default_printer (printer_name);
    }

  return result;
}

gboolean
printer_set_shared (const gchar *printer_name,
                    gboolean     shared)
{
  GDBusConnection *bus;
  GVariant   *output;
  gboolean    result = FALSE;
  GError     *error = NULL;

  if (!printer_name)
    return TRUE;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
   {
     g_warning ("Failed to get system bus: %s", error->message);
     g_error_free (error);
     return TRUE;
   }

  output = g_dbus_connection_call_sync (bus,
                                        MECHANISM_BUS,
                                        "/",
                                        MECHANISM_BUS,
                                        "PrinterSetShared",
                                        g_variant_new ("(sb)", printer_name, shared),
                                        G_VARIANT_TYPE ("(s)"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &error);
  g_object_unref (bus);

  if (output)
    {
      const gchar *ret_error;

      g_variant_get (output, "(&s)", &ret_error);
      if (ret_error[0] != '\0')
        g_warning ("%s", ret_error);
      else
        result = TRUE;

      g_variant_unref (output);
    }
  else
    {
      g_warning ("%s", error->message);
      g_error_free (error);
    }

  return result;
}

gboolean
printer_set_job_sheets (const gchar *printer_name,
                        const gchar *start_sheet,
                        const gchar *end_sheet)
{
  GDBusConnection *bus;
  GVariant   *output;
  GError     *error = NULL;
  gboolean    result = FALSE;

  if (!printer_name || !start_sheet || !end_sheet)
    return TRUE;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
   {
     g_warning ("Failed to get system bus: %s", error->message);
     g_error_free (error);
     return TRUE;
   }

  output = g_dbus_connection_call_sync (bus,
                                        MECHANISM_BUS,
                                        "/",
                                        MECHANISM_BUS,
                                        "PrinterSetJobSheets",
                                        g_variant_new ("(sss)", printer_name, start_sheet, end_sheet),
                                        G_VARIANT_TYPE ("(s)"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &error);
  g_object_unref (bus);

  if (output)
    {
      const gchar *ret_error;

      g_variant_get (output, "(&s)", &ret_error);
      if (ret_error[0] != '\0')
        g_warning ("%s", ret_error);
      else
        result = TRUE;

      g_variant_unref (output);
    }
  else
    {
      g_warning ("%s", error->message);
      g_error_free (error);
    }

  return result;
}

gboolean
printer_set_policy (const gchar *printer_name,
                    const gchar *policy,
                    gboolean     error_policy)
{
  GDBusConnection *bus;
  GVariant   *output;
  gboolean   result = FALSE;
  GError     *error = NULL;

  if (!printer_name || !policy)
    return TRUE;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
   {
     g_warning ("Failed to get system bus: %s", error->message);
     g_error_free (error);
     return TRUE;
   }

  if (error_policy)
    output = g_dbus_connection_call_sync (bus,
                                          MECHANISM_BUS,
                                          "/",
                                          MECHANISM_BUS,
                                          "PrinterSetErrorPolicy",
                                          g_variant_new ("(ss)", printer_name, policy),
                                          G_VARIANT_TYPE ("(s)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);
  else
    output = g_dbus_connection_call_sync (bus,
                                          MECHANISM_BUS,
                                          "/",
                                          MECHANISM_BUS,
                                          "PrinterSetOpPolicy",
                                          g_variant_new ("(ss)", printer_name, policy),
                                          G_VARIANT_TYPE ("(s)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);
  g_object_unref (bus);

  if (output)
    {
      const gchar *ret_error;

      g_variant_get (output, "(&s)", &ret_error);
      if (ret_error[0] != '\0')
        g_warning ("%s", ret_error);
      else
        result = TRUE;

      g_variant_unref (output);
    }
  else
    {
      g_warning ("%s", error->message);
      g_error_free (error);
    }

  return result;
}

gboolean
printer_set_users (const gchar  *printer_name,
                   gchar       **users,
                   gboolean      allowed)
{
  GDBusConnection *bus;
  GVariantBuilder array_builder;
  gint        i;
  GVariant   *output;
  gboolean    result = FALSE;
  GError     *error = NULL;

  if (!printer_name || !users)
    return TRUE;
  
  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
   {
     g_warning ("Failed to get system bus: %s", error->message);
     g_error_free (error);
     return TRUE;
   }

  g_variant_builder_init (&array_builder, G_VARIANT_TYPE ("as"));
  for (i = 0; users[i]; i++)
    g_variant_builder_add (&array_builder, "s", users[i]);

  if (allowed)
    output = g_dbus_connection_call_sync (bus,
                                          MECHANISM_BUS,
                                          "/",
                                          MECHANISM_BUS,
                                          "PrinterSetUsersAllowed",
                                          g_variant_new ("(sas)", printer_name, &array_builder),
                                          G_VARIANT_TYPE ("(s)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);
  else
    output = g_dbus_connection_call_sync (bus,
                                          MECHANISM_BUS,
                                          "/",
                                          MECHANISM_BUS,
                                          "PrinterSetUsersDenied",
                                          g_variant_new ("(sas)", printer_name, &array_builder),
                                          G_VARIANT_TYPE ("(s)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);
  g_object_unref (bus);

  if (output)
    {
      const gchar *ret_error;

      g_variant_get (output, "(&s)", &ret_error);
      if (ret_error[0] != '\0')
        g_warning ("%s", ret_error);
      else
        result = TRUE;

      g_variant_unref (output);
    }
  else
    {
      g_warning ("%s", error->message);
      g_error_free (error);
    }

  return result;
}

gboolean
class_add_printer (const gchar *class_name,
                   const gchar *printer_name)
{
  GDBusConnection *bus;
  GVariant   *output;
  gboolean    result = FALSE;
  GError     *error = NULL;

  if (!class_name || !printer_name)
    return TRUE;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
   {
     g_warning ("Failed to get system bus: %s", error->message);
     g_error_free (error);
     return TRUE;
   }

  output = g_dbus_connection_call_sync (bus,
                                        MECHANISM_BUS,
                                        "/",
                                        MECHANISM_BUS,
                                        "ClassAddPrinter",
                                        g_variant_new ("(ss)", class_name, printer_name),
                                        G_VARIANT_TYPE ("(s)"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &error);
  g_object_unref (bus);

  if (output)
    {
      const gchar *ret_error;

      g_variant_get (output, "(&s)", &ret_error);
      if (ret_error[0] != '\0')
        g_warning ("%s", ret_error);
      else
        result = TRUE;

      g_variant_unref (output);
    }
  else
    {
      g_warning ("%s", error->message);
      g_error_free (error);
    }

  return result;
}

gboolean
printer_is_local (cups_ptype_t  printer_type,
                  const gchar  *device_uri)
{
  gboolean result = TRUE;
  char     scheme[HTTP_MAX_URI];
  char     username[HTTP_MAX_URI];
  char     hostname[HTTP_MAX_URI];
  char     resource[HTTP_MAX_URI];
  int      port;

  if (printer_type &
      (CUPS_PRINTER_DISCOVERED |
       CUPS_PRINTER_REMOTE |
       CUPS_PRINTER_IMPLICIT))
    result = FALSE;

  if (device_uri == NULL || !result)
    return result;

  httpSeparateURI (HTTP_URI_CODING_ALL, device_uri,
		   scheme, sizeof (scheme), 
		   username, sizeof (username),
		   hostname, sizeof (hostname),
		   &port,
		   resource, sizeof (resource));

  if (g_str_equal (scheme, "ipp") ||
      g_str_equal (scheme, "smb") ||
      g_str_equal (scheme, "socket") ||
      g_str_equal (scheme, "lpd"))
    result = FALSE;

  return result;
}

gchar*
printer_get_hostname (cups_ptype_t  printer_type,
                      const gchar  *device_uri,
                      const gchar  *printer_uri)
{
  gboolean  local = TRUE;
  gchar    *result = NULL;
  char      scheme[HTTP_MAX_URI];
  char      username[HTTP_MAX_URI];
  char      hostname[HTTP_MAX_URI];
  char      resource[HTTP_MAX_URI];
  int       port;

  if (device_uri == NULL)
    return result;

  if (printer_type & (CUPS_PRINTER_DISCOVERED |
                      CUPS_PRINTER_REMOTE |
                      CUPS_PRINTER_IMPLICIT))
    {
      if (printer_uri)
        {
          httpSeparateURI (HTTP_URI_CODING_ALL, printer_uri,
                           scheme, sizeof (scheme),
                           username, sizeof (username),
                           hostname, sizeof (hostname),
                           &port,
                           resource, sizeof (resource));

          if (hostname[0] != '\0')
            result = g_strdup (hostname);
        }

      local = FALSE;
    }

  if (result == NULL && device_uri)
    {
      httpSeparateURI (HTTP_URI_CODING_ALL, device_uri,
                       scheme, sizeof (scheme),
                       username, sizeof (username),
                       hostname, sizeof (hostname),
                       &port,
                       resource, sizeof (resource));

      if (g_str_equal (scheme, "ipp") ||
          g_str_equal (scheme, "smb") ||
          g_str_equal (scheme, "socket") ||
          g_str_equal (scheme, "lpd"))
        {
          if (hostname[0] != '\0')
            result = g_strdup (hostname);

          local = FALSE;
        }
    }

  if (local)
    result = g_strdup ("localhost");

  return result;
}

/* Returns default media size for current locale */
static const gchar *
get_paper_size_from_locale ()
{
  if (g_str_equal (gtk_paper_size_get_default (), GTK_PAPER_NAME_LETTER))
    return "na-letter";
  else
    return "iso-a4";
}

/* Set default media size according to the locale */
void
printer_set_default_media_size (const gchar *printer_name)
{
  GVariantBuilder  array_builder;
  GDBusConnection *bus;
  GVariant        *output;
  GError          *error = NULL;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
   {
     g_warning ("Failed to get system bus: %s", error->message);
     g_error_free (error);
     return;
   }

  g_variant_builder_init (&array_builder, G_VARIANT_TYPE ("as"));
  g_variant_builder_add (&array_builder, "s", get_paper_size_from_locale ());

  output = g_dbus_connection_call_sync (bus,
                                        MECHANISM_BUS,
                                        "/",
                                        MECHANISM_BUS,
                                        "PrinterAddOption",
                                        g_variant_new ("(ssas)",
                                                       printer_name,
                                                       "media",
                                                       &array_builder),
                                        G_VARIANT_TYPE ("(s)"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &error);

  g_object_unref (bus);

  if (output)
    {
      const gchar *ret_error;

      g_variant_get (output, "(&s)", &ret_error);
      if (ret_error[0] != '\0')
        g_warning ("%s", ret_error);

      g_variant_unref (output);
    }
  else
    {
      if (!(error->domain == G_DBUS_ERROR &&
            (error->code == G_DBUS_ERROR_SERVICE_UNKNOWN ||
             error->code == G_DBUS_ERROR_UNKNOWN_METHOD)))
        g_warning ("%s", error->message);
      g_error_free (error);
    }
}


typedef struct
{
  gchar        *printer_name;
  gchar       **attributes_names;
  GHashTable   *result;
  GIACallback   callback;
  gpointer      user_data;
  GMainContext *context;
} GIAData;

static gboolean
get_ipp_attributes_idle_cb (gpointer user_data)
{
  GIAData *data = (GIAData *) user_data;

  data->callback (data->result, data->user_data);

  return FALSE;
}

static void
get_ipp_attributes_data_free (gpointer user_data)
{
  GIAData *data = (GIAData *) user_data;

  if (data->context)
    g_main_context_unref (data->context);
  g_free (data->printer_name);
  if (data->attributes_names)
    g_strfreev (data->attributes_names);
  g_free (data);
}

static void
get_ipp_attributes_cb (gpointer user_data)
{
  GIAData *data = (GIAData *) user_data;
  GSource *idle_source;

  idle_source = g_idle_source_new ();
  g_source_set_callback (idle_source,
                         get_ipp_attributes_idle_cb,
                         data,
                         get_ipp_attributes_data_free);
  g_source_attach (idle_source, data->context);
  g_source_unref (idle_source);
}

static void
ipp_attribute_free2 (gpointer attr)
{
  IPPAttribute *attribute = (IPPAttribute *) attr;
  ipp_attribute_free (attribute);
}

static gpointer
get_ipp_attributes_func (gpointer user_data)
{
  ipp_attribute_t  *attr = NULL;
  GIAData          *data = (GIAData *) user_data;
  ipp_t            *request;
  ipp_t            *response = NULL;
  gchar            *printer_uri;
  char            **requested_attrs = NULL;
  gint              i, j, length = 0;

  printer_uri = g_strdup_printf ("ipp://localhost/printers/%s", data->printer_name);

  if (data->attributes_names)
    {
      length = g_strv_length (data->attributes_names);

      requested_attrs = g_new0 (char *, length);
      for (i = 0; data->attributes_names[i]; i++)
        requested_attrs[i] = g_strdup (data->attributes_names[i]);

      request = ippNewRequest (IPP_GET_PRINTER_ATTRIBUTES);
      ippAddString (request, IPP_TAG_OPERATION, IPP_TAG_URI,
                    "printer-uri", NULL, printer_uri);
      ippAddStrings (request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                     "requested-attributes", length, NULL, (const char **) requested_attrs);
      response = cupsDoRequest (CUPS_HTTP_DEFAULT, request, "/");
    }

  if (response)
    {
      if (response->request.status.status_code <= IPP_OK_CONFLICT)
        {
          for (j = 0; j < length; j++)
            {
              attr = ippFindAttribute (response, requested_attrs[j], IPP_TAG_ZERO);
              if (attr && attr->num_values > 0 && attr->value_tag != IPP_TAG_NOVALUE)
                {
                  IPPAttribute *attribute;

                  attribute = g_new0 (IPPAttribute, 1);
                  attribute->attribute_name = g_strdup (requested_attrs[j]);
                  attribute->attribute_values = g_new0 (IPPAttributeValue, attr->num_values);
                  attribute->num_of_values = attr->num_values;

                  if (attr->value_tag == IPP_TAG_INTEGER ||
                      attr->value_tag == IPP_TAG_ENUM)
                    {
                      attribute->attribute_type = IPP_ATTRIBUTE_TYPE_INTEGER;

                      for (i = 0; i < attr->num_values; i++)
                        attribute->attribute_values[i].integer_value = attr->values[i].integer;
                    }
                  else if (attr->value_tag == IPP_TAG_NAME ||
                           attr->value_tag == IPP_TAG_STRING ||
                           attr->value_tag == IPP_TAG_TEXT ||
                           attr->value_tag == IPP_TAG_URI ||
                           attr->value_tag == IPP_TAG_KEYWORD ||
                           attr->value_tag == IPP_TAG_URISCHEME)
                    {
                      attribute->attribute_type = IPP_ATTRIBUTE_TYPE_STRING;

                      for (i = 0; i < attr->num_values; i++)
                        attribute->attribute_values[i].string_value = g_strdup (attr->values[i].string.text);
                    }
                  else if (attr->value_tag == IPP_TAG_RANGE)
                    {
                      attribute->attribute_type = IPP_ATTRIBUTE_TYPE_RANGE;

                      for (i = 0; i < attr->num_values; i++)
                        {
                          attribute->attribute_values[i].lower_range = attr->values[i].range.lower;
                          attribute->attribute_values[i].upper_range = attr->values[i].range.upper;
                        }
                    }
                  else if (attr->value_tag == IPP_TAG_BOOLEAN)
                    {
                      attribute->attribute_type = IPP_ATTRIBUTE_TYPE_BOOLEAN;

                      for (i = 0; i < attr->num_values; i++)
                        attribute->attribute_values[i].boolean_value = attr->values[i].boolean;
                    }

                  if (!data->result)
                    data->result = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, ipp_attribute_free2);

                  g_hash_table_insert (data->result, g_strdup (requested_attrs[j]), attribute);
                }
            }
        }

      ippDelete (response);
    }


  for (i = 0; i < length; i++)
    g_free (requested_attrs[i]);
  g_free (requested_attrs);

  g_free (printer_uri);

  get_ipp_attributes_cb (data);

  return NULL;
}

void
get_ipp_attributes_async (const gchar  *printer_name,
                          gchar       **attributes_names,
                          GIACallback   callback,
                          gpointer      user_data)
{
  GIAData *data;
  GThread *thread;
  GError  *error = NULL;

  data = g_new0 (GIAData, 1);
  data->printer_name = g_strdup (printer_name);
  data->attributes_names = g_strdupv (attributes_names);
  data->callback = callback;
  data->user_data = user_data;
  data->context = g_main_context_ref_thread_default ();

  thread = g_thread_try_new ("get-ipp-attributes",
                             get_ipp_attributes_func,
                             data,
                             &error);

  if (!thread)
    {
      g_warning ("%s", error->message);
      callback (NULL, user_data);

      g_error_free (error);
      get_ipp_attributes_data_free (data);
    }
  else
    {
      g_thread_unref (thread);
    }
}

IPPAttribute *
ipp_attribute_copy (IPPAttribute *attr)
{
  IPPAttribute *result = NULL;
  gint          i;

  if (attr)
    {
      result = g_new0 (IPPAttribute, 1);

      *result = *attr;
      result->attribute_name = g_strdup (attr->attribute_name);
      result->attribute_values = g_new0 (IPPAttributeValue, attr->num_of_values);
      for (i = 0; i < attr->num_of_values; i++)
        {
          result->attribute_values[i] = attr->attribute_values[i];
          if (attr->attribute_values[i].string_value)
            result->attribute_values[i].string_value = g_strdup (attr->attribute_values[i].string_value);
        }
    }

  return result;
}

void
ipp_attribute_free (IPPAttribute *attr)
{
  gint i;

  if (attr)
    {
      for (i = 0; i < attr->num_of_values; i++)
        g_free (attr->attribute_values[i].string_value);

      g_free (attr->attribute_values);
      g_free (attr->attribute_name);
      g_free (attr);
    }
}



typedef struct
{
  gchar        *printer_name;
  gchar        *ppd_copy;
  GCancellable *cancellable;
  PSPCallback   callback;
  gpointer      user_data;
} PSPData;

static void
printer_set_ppd_async_dbus_cb (GObject      *source_object,
                               GAsyncResult *res,
                               gpointer      user_data)
{
  GVariant *output;
  gboolean  result = FALSE;
  PSPData  *data = (PSPData *) user_data;
  GError   *error = NULL;

  output = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object),
                                          res,
                                          &error);
  g_object_unref (source_object);

  if (output)
    {
      const gchar *ret_error;

      g_variant_get (output, "(&s)", &ret_error);
      if (ret_error[0] != '\0')
        g_warning ("%s", ret_error);
      else
        result = TRUE;

      g_variant_unref (output);
    }
  else
    {
      if (error->code != G_IO_ERROR_CANCELLED)
        g_warning ("%s", error->message);
      g_error_free (error);
    }

  /* Don't call callback if cancelled */
  if (!data->cancellable ||
      !g_cancellable_is_cancelled (data->cancellable))
    data->callback (g_strdup (data->printer_name),
                    result,
                    data->user_data);

  if (data->cancellable)
    g_object_unref (data->cancellable);

  if (data->ppd_copy)
    {
      g_unlink (data->ppd_copy);
      g_free (data->ppd_copy);
    }

  g_free (data->printer_name);
  g_free (data);
}

/*
 * Set ppd for given printer.
 * Don't use this for classes, just for printers.
 */
void
printer_set_ppd_async (const gchar  *printer_name,
                       const gchar  *ppd_name,
                       GCancellable *cancellable,
                       PSPCallback   callback,
                       gpointer      user_data)
{
  GDBusConnection *bus;
  PSPData         *data;
  GError          *error = NULL;

  data = g_new0 (PSPData, 1);
  if (cancellable)
    data->cancellable = g_object_ref (cancellable);
  data->callback = callback;
  data->user_data = user_data;
  data->printer_name = g_strdup (printer_name);

  if (printer_name == NULL ||
      printer_name[0] == '\0')
    {
      goto out;
    }

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
    {
      g_warning ("Failed to get system bus: %s", error->message);
      g_error_free (error);
      goto out;
    }

  g_dbus_connection_call (bus,
                          MECHANISM_BUS,
                          "/",
                          MECHANISM_BUS,
                          "PrinterAdd",
                          g_variant_new ("(sssss)",
                                         printer_name,
                                         "",
                                         ppd_name,
                                         "",
                                         ""),
                          G_VARIANT_TYPE ("(s)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          data->cancellable,
                          printer_set_ppd_async_dbus_cb,
                          data);

  return;

out:
  callback (g_strdup (printer_name), FALSE, user_data);

  if (data->cancellable)
    g_object_unref (data->cancellable);
  g_free (data->printer_name);
  g_free (data);
}

static void
printer_set_ppd_file_async_scb (GObject      *source_object,
                                GAsyncResult *res,
                                gpointer      user_data)
{
  GDBusConnection *bus;
  gboolean         success;
  PSPData         *data = (PSPData *) user_data;
  GError          *error = NULL;

  success = g_file_copy_finish (G_FILE (source_object),
                                res,
                                &error);
  g_object_unref (source_object);

  if (!success)
    {
      g_warning ("%s", error->message);
      g_error_free (error);
      goto out;
    }

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
    {
      g_warning ("Failed to get system bus: %s", error->message);
      g_error_free (error);
      goto out;
    }

  g_dbus_connection_call (bus,
                          MECHANISM_BUS,
                          "/",
                          MECHANISM_BUS,
                          "PrinterAddWithPpdFile",
                          g_variant_new ("(sssss)",
                                         data->printer_name,
                                         "",
                                         data->ppd_copy,
                                         "",
                                         ""),
                          G_VARIANT_TYPE ("(s)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          data->cancellable,
                          printer_set_ppd_async_dbus_cb,
                          data);

  return;

out:
  data->callback (g_strdup (data->printer_name), FALSE, data->user_data);

  if (data->cancellable)
    g_object_unref (data->cancellable);
  g_free (data->printer_name);
  g_free (data->ppd_copy);
  g_free (data);
}

/*
 * Set ppd for given printer.
 * Don't use this for classes, just for printers.
 */
void
printer_set_ppd_file_async (const gchar  *printer_name,
                            const gchar  *ppd_filename,
                            GCancellable *cancellable,
                            PSPCallback   callback,
                            gpointer      user_data)
{
  GFileIOStream *stream;
  PSPData       *data;
  GFile         *source_ppd_file;
  GFile         *destination_ppd_file;

  data = g_new0 (PSPData, 1);
  if (cancellable)
    data->cancellable = g_object_ref (cancellable);
  data->callback = callback;
  data->user_data = user_data;
  data->printer_name = g_strdup (printer_name);

  if (printer_name == NULL ||
      printer_name[0] == '\0')
    {
      goto out;
    }

  /*
   * We need to copy the PPD to temp directory at first.
   * This is needed because of SELinux.
   */
  source_ppd_file = g_file_new_for_path (ppd_filename);
  destination_ppd_file = g_file_new_tmp ("g-c-c-XXXXXX.ppd", &stream, NULL);
  g_object_unref (stream);
  data->ppd_copy = g_strdup (g_file_get_path (destination_ppd_file));

  g_file_copy_async (source_ppd_file,
                     destination_ppd_file,
                     G_FILE_COPY_OVERWRITE,
                     G_PRIORITY_DEFAULT,
                     cancellable,
                     NULL,
                     NULL,
                     printer_set_ppd_file_async_scb,
                     data);

  g_object_unref (destination_ppd_file);

  return;

out:
  callback (g_strdup (printer_name), FALSE, user_data);

  if (data->cancellable)
    g_object_unref (data->cancellable);
  g_free (data->printer_name);
  g_free (data);
}



typedef void (*GPACallback) (gchar    **attribute_values,
                             gpointer   user_data);

typedef struct
{
  gchar         *attribute_name;
  gchar        **ppds_names;
  gchar        **result;
  GPACallback    callback;
  gpointer       user_data;
  GMainContext  *context;
} GPAData;

static gboolean
get_ppds_attribute_idle_cb (gpointer user_data)
{
  GPAData *data = (GPAData *) user_data;

  data->callback (data->result, data->user_data);

  return FALSE;
}

static void
get_ppds_attribute_data_free (gpointer user_data)
{
  GPAData *data = (GPAData *) user_data;

  if (data->context)
    g_main_context_unref (data->context);
  g_free (data->attribute_name);
  g_strfreev (data->ppds_names);
  g_free (data);
}

static void
get_ppds_attribute_cb (gpointer user_data)
{
  GPAData *data = (GPAData *) user_data;
  GSource *idle_source;

  idle_source = g_idle_source_new ();
  g_source_set_callback (idle_source,
                         get_ppds_attribute_idle_cb,
                         data,
                         get_ppds_attribute_data_free);
  g_source_attach (idle_source, data->context);
  g_source_unref (idle_source);
}

static gpointer
get_ppds_attribute_func (gpointer user_data)
{
  ppd_file_t  *ppd_file;
  ppd_attr_t  *ppd_attr;
  GPAData     *data = (GPAData *) user_data;
  gchar       *ppd_filename;
  gint         i;

  data->result = g_new0 (gchar *, g_strv_length (data->ppds_names) + 1);
  for (i = 0; data->ppds_names[i]; i++)
    {
      ppd_filename = g_strdup (cupsGetServerPPD (CUPS_HTTP_DEFAULT, data->ppds_names[i]));
      if (ppd_filename)
        {
          ppd_file = ppdOpenFile (ppd_filename);
          if (ppd_file)
            {
              ppd_attr = ppdFindAttr (ppd_file, data->attribute_name, NULL);
              if (ppd_attr != NULL)
                data->result[i] = g_strdup (ppd_attr->value);

              ppdClose (ppd_file);
            }

          g_unlink (ppd_filename);
          g_free (ppd_filename);
        }
    }

  get_ppds_attribute_cb (data);

  return NULL;
}

/*
 * Get values of requested PPD attribute for given PPDs.
 */
static void
get_ppds_attribute_async (gchar       **ppds_names,
                          gchar        *attribute_name,
                          GPACallback   callback,
                          gpointer      user_data)
{
  GPAData *data;
  GThread *thread;
  GError  *error = NULL;

  if (!ppds_names || !attribute_name)
    callback (NULL, user_data);

  data = g_new0 (GPAData, 1);
  data->ppds_names = g_strdupv (ppds_names);
  data->attribute_name = g_strdup (attribute_name);
  data->callback = callback;
  data->user_data = user_data;
  data->context = g_main_context_ref_thread_default ();

  thread = g_thread_try_new ("get-ppds-attribute",
                             get_ppds_attribute_func,
                             data,
                             &error);

  if (!thread)
    {
      g_warning ("%s", error->message);
      callback (NULL, user_data);

      g_error_free (error);
      get_ppds_attribute_data_free (data);
    }
  else
    {
      g_thread_unref (thread);
    }
}



typedef void (*GDACallback) (gchar    *device_id,
                             gchar    *device_make_and_model,
                             gchar    *device_uri,
                             gpointer  user_data);

typedef struct
{
  gchar        *printer_name;
  gchar        *device_uri;
  GCancellable *cancellable;
  GList        *backend_list;
  GDACallback   callback;
  gpointer      user_data;
} GDAData;

typedef struct
{
  gchar         *printer_name;
  gint           count;
  PPDName      **result;
  GCancellable  *cancellable;
  GPNCallback    callback;
  gpointer       user_data;
} GPNData;

static void
get_ppd_names_async_cb (gchar    **attribute_values,
                        gpointer   user_data)
{
  GPNData *data = (GPNData *) user_data;
  gint     i;

  if (g_cancellable_is_cancelled (data->cancellable))
    {
      g_strfreev (attribute_values);

      for (i = 0; data->result[i]; i++)
        {
          g_free (data->result[i]->ppd_name);
          g_free (data->result[i]);
        }

      g_free (data->result);
      data->result = NULL;

      goto out;
    }

  if (attribute_values)
    {
      for (i = 0; attribute_values[i]; i++)
        data->result[i]->ppd_display_name = attribute_values[i];

      g_free (attribute_values);
    }

out:
  data->callback (data->result,
                  data->printer_name,
                  g_cancellable_is_cancelled (data->cancellable),
                  data->user_data);

  if (data->cancellable)
    g_object_unref (data->cancellable);
  g_free (data->printer_name);
  g_free (data);
}

static void
get_ppd_names_async_dbus_scb (GObject      *source_object,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  GVariant  *output;
  PPDName   *ppd_item;
  PPDName  **result = NULL;
  GPNData   *data = (GPNData *) user_data;
  GError    *error = NULL;
  GList     *driver_list = NULL;
  GList     *iter;
  gint       i, j, n = 0;
  static const char * const match_levels[] = {
             "exact-cmd",
             "exact",
             "close",
             "generic",
             "none"};

  output = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object),
                                          res,
                                          &error);
  g_object_unref (source_object);

  if (output)
    {
      GVariant *array;

      g_variant_get (output, "(@a(ss))",
                     &array);

      if (array)
        {
          GVariantIter *iter;
          GVariant     *item;
          gchar        *driver;
          gchar        *match;

          for (j = 0; j < G_N_ELEMENTS (match_levels) && n < data->count; j++)
            {
              g_variant_get (array,
                             "a(ss)",
                             &iter);

              while ((item = g_variant_iter_next_value (iter)))
                {
                  g_variant_get (item,
                                 "(ss)",
                                 &driver,
                                 &match);

                  if (g_str_equal (match, match_levels[j]) && n < data->count)
                    {
                      ppd_item = g_new0 (PPDName, 1);
                      ppd_item->ppd_name = g_strdup (driver);

                      if (g_strcmp0 (match, "exact-cmd") == 0)
                        ppd_item->ppd_match_level = PPD_EXACT_CMD_MATCH;
                      else if (g_strcmp0 (match, "exact") == 0)
                        ppd_item->ppd_match_level = PPD_EXACT_MATCH;
                      else if (g_strcmp0 (match, "close") == 0)
                        ppd_item->ppd_match_level = PPD_CLOSE_MATCH;
                      else if (g_strcmp0 (match, "generic") == 0)
                        ppd_item->ppd_match_level = PPD_GENERIC_MATCH;
                      else if (g_strcmp0 (match, "none") == 0)
                        ppd_item->ppd_match_level = PPD_NO_MATCH;

                      driver_list = g_list_append (driver_list, ppd_item);

                      n++;
                    }

                  g_free (driver);
                  g_free (match);
                  g_variant_unref (item);
                }
            }

          g_variant_unref (array);
        }

      g_variant_unref (output);
    }
  else
    {
      if (error->code != G_IO_ERROR_CANCELLED)
        g_warning ("%s", error->message);
      g_error_free (error);
    }

  if (n > 0)
    {
      result = g_new0 (PPDName *, n + 1);
      i = 0;
      for (iter = driver_list; iter; iter = iter->next)
        {
          result[i] = iter->data;
          i++;
        }
    }

  if (result)
    {
      gchar **ppds_names;

      data->result = result;

      ppds_names = g_new0 (gchar *, n + 1);
      for (i = 0; i < n; i++)
        ppds_names[i] = g_strdup (result[i]->ppd_name);

      get_ppds_attribute_async (ppds_names,
                                "NickName",
                                get_ppd_names_async_cb,
                                data);

      g_strfreev (ppds_names);
    }
  else
    {
      data->callback (NULL,
                      data->printer_name,
                      g_cancellable_is_cancelled (data->cancellable),
                      data->user_data);

      if (data->cancellable)
        g_object_unref (data->cancellable);
      g_free (data->printer_name);
      g_free (data);
    }
}

static void
get_device_attributes_cb (gchar    *device_id,
                          gchar    *device_make_and_model,
                          gchar    *device_uri,
                          gpointer  user_data)
{
  GDBusConnection *bus;
  GError          *error = NULL;
  GPNData         *data = (GPNData *) user_data;

  if (g_cancellable_is_cancelled (data->cancellable))
    goto out;

  if (!device_id || !device_make_and_model || !device_uri)
    goto out;

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (!bus)
    {
      g_warning ("Failed to get system bus: %s", error->message);
      g_error_free (error);
      goto out;
    }

  g_dbus_connection_call (bus,
                          SCP_BUS,
                          SCP_PATH,
                          SCP_IFACE,
                          "GetBestDrivers",
                          g_variant_new ("(sss)",
                                         device_id,
                                         device_make_and_model,
                                         device_uri),
                          G_VARIANT_TYPE ("(a(ss))"),
                          G_DBUS_CALL_FLAGS_NONE,
                          DBUS_TIMEOUT,
                          data->cancellable,
                          get_ppd_names_async_dbus_scb,
                          data);

  return;

out:
  data->callback (NULL,
                  data->printer_name,
                  g_cancellable_is_cancelled (data->cancellable),
                  data->user_data);

  if (data->cancellable)
    g_object_unref (data->cancellable);
  g_free (data->printer_name);
  g_free (data);
}

static void
get_device_attributes_async_dbus_cb (GObject      *source_object,
                                     GAsyncResult *res,
                                     gpointer      user_data)

{
  GVariant *output;
  GDAData  *data = (GDAData *) user_data;
  GError   *error = NULL;
  GList    *tmp;
  gchar    *device_id = NULL;
  gchar    *device_make_and_model = NULL;

  output = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object),
                                          res,
                                          &error);
  g_object_unref (source_object);

  if (output)
    {
      const gchar *ret_error;
      GVariant    *devices_variant = NULL;

      g_variant_get (output, "(&s@a{ss})",
                     &ret_error,
                     &devices_variant);

      if (ret_error[0] != '\0')
        {
          g_warning ("%s", ret_error);
        }

      if (devices_variant)
        {
          GVariantIter *iter;
          GVariant     *item;
          gint          index = -1;

          if (data->device_uri)
            {
              gchar *key;
              gchar *value;
              gchar *number;
              gchar *endptr;
              gchar *suffix;

              g_variant_get (devices_variant,
                             "a{ss}",
                             &iter);

              while ((item = g_variant_iter_next_value (iter)))
                {
                  g_variant_get (item,
                                 "{ss}",
                                 &key,
                                 &value);

                  if (g_str_equal (value, data->device_uri))
                    {
                      number = g_strrstr (key, ":");
                      if (number != NULL)
                        {
                          number++;
                          index = g_ascii_strtoll (number, &endptr, 10);
                          if (index == 0 && endptr == (number))
                            index = -1;
                        }
                    }

                  g_free (key);
                  g_free (value);
                  g_variant_unref (item);
                }

              suffix = g_strdup_printf (":%d", index);

              g_variant_get (devices_variant,
                             "a{ss}",
                             &iter);

              while ((item = g_variant_iter_next_value (iter)))
                {
                  gchar *key;
                  gchar *value;

                  g_variant_get (item,
                                 "{ss}",
                                 &key,
                                 &value);

                  if (g_str_has_suffix (key, suffix))
                    {
                      if (g_str_has_prefix (key, "device-id"))
                        {
                          device_id = g_strdup (value);
                        }

                      if (g_str_has_prefix (key, "device-make-and-model"))
                        {
                          device_make_and_model = g_strdup (value);
                        }
                    }

                  g_free (key);
                  g_free (value);
                  g_variant_unref (item);
                }

              g_free (suffix);
            }

          g_variant_unref (devices_variant);
        }

      g_variant_unref (output);
    }
  else
    {
      if (error->code != G_IO_ERROR_CANCELLED)
        g_warning ("%s", error->message);
      g_error_free (error);
    }

  if (!device_id || !device_make_and_model)
    {
      GVariantBuilder include_scheme_builder;

      g_free (device_id);
      g_free (device_make_and_model);

      device_id = NULL;
      device_make_and_model = NULL;

      if (data->backend_list && !g_cancellable_is_cancelled (data->cancellable))
        {
          g_variant_builder_init (&include_scheme_builder, G_VARIANT_TYPE ("as"));
          g_variant_builder_add (&include_scheme_builder, "s", data->backend_list->data);

          tmp = data->backend_list;
          data->backend_list = g_list_remove_link (data->backend_list, tmp);
          g_list_free_full (tmp, g_free);

          g_dbus_connection_call (G_DBUS_CONNECTION (g_object_ref (source_object)),
                                  MECHANISM_BUS,
                                  "/",
                                  MECHANISM_BUS,
                                  "DevicesGet",
                                  g_variant_new ("(iiasas)",
                                                 0,
                                                 0,
                                                 &include_scheme_builder,
                                                 NULL),
                                  G_VARIANT_TYPE ("(sa{ss})"),
                                  G_DBUS_CALL_FLAGS_NONE,
                                  DBUS_TIMEOUT,
                                  data->cancellable,
                                  get_device_attributes_async_dbus_cb,
                                  user_data);
          return;
        }
    }

  g_object_unref (source_object);

  if (data->backend_list)
    {
      g_list_free_full (data->backend_list, g_free);
      data->backend_list = NULL;
    }

  data->callback (device_id,
                  device_make_and_model,
                  data->device_uri,
                  data->user_data);

  if (data->cancellable)
    g_object_unref (data->cancellable);
  g_free (data->device_uri);
  g_free (data->printer_name);
  g_free (data);
}

static void
get_device_attributes_async_scb (GHashTable *result,
                                 gpointer    user_data)
{
  GDBusConnection *bus;
  GVariantBuilder  include_scheme_builder;
  IPPAttribute    *attr;
  GDAData         *data = (GDAData *) user_data;
  GError          *error = NULL;
  GList           *tmp;
  gint             i;
  const gchar     *backends[] =
    {"hpfax", "ncp", "beh", "bluetooth", "snmp",
     "dnssd", "hp", "ipp", "lpd", "parallel",
     "serial", "socket", "usb", NULL};

  if (result)
    {
      attr = g_hash_table_lookup (result, "device-uri");
      if (attr && attr->attribute_type == IPP_ATTRIBUTE_TYPE_STRING &&
          attr->num_of_values > 0)
      data->device_uri = g_strdup (attr->attribute_values[0].string_value);
      g_hash_table_unref (result);
    }

  if (g_cancellable_is_cancelled (data->cancellable))
    goto out;

  if (!data->device_uri)
    goto out;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
    {
      g_warning ("Failed to get system bus: %s", error->message);
      g_error_free (error);
      goto out;
    }

  for (i = 0; backends[i]; i++)
    data->backend_list = g_list_prepend (data->backend_list, g_strdup (backends[i]));

  g_variant_builder_init (&include_scheme_builder, G_VARIANT_TYPE ("as"));
  g_variant_builder_add (&include_scheme_builder, "s", data->backend_list->data);

  tmp = data->backend_list;
  data->backend_list = g_list_remove_link (data->backend_list, tmp);
  g_list_free_full (tmp, g_free);

  g_dbus_connection_call (g_object_ref (bus),
                          MECHANISM_BUS,
                          "/",
                          MECHANISM_BUS,
                          "DevicesGet",
                          g_variant_new ("(iiasas)",
                                         0,
                                         0,
                                         &include_scheme_builder,
                                         NULL),
                          G_VARIANT_TYPE ("(sa{ss})"),
                          G_DBUS_CALL_FLAGS_NONE,
                          DBUS_TIMEOUT,
                          data->cancellable,
                          get_device_attributes_async_dbus_cb,
                          data);

  return;

out:
  data->callback (NULL, NULL, NULL, data->user_data);

  if (data->cancellable)
    g_object_unref (data->cancellable);
  g_free (data->device_uri);
  g_free (data->printer_name);
  g_free (data);
}

/*
 * Get device-id, device-make-and-model and device-uri for given printer.
 */
static void
get_device_attributes_async (const gchar  *printer_name,
                             GCancellable *cancellable,
                             GDACallback   callback,
                             gpointer      user_data)
{
  GDAData  *data;
  gchar   **attributes;

  if (!printer_name)
   {
     callback (NULL, NULL, NULL, user_data);

     return;
   }

  data = g_new0 (GDAData, 1);
  data->printer_name = g_strdup (printer_name);
  if (cancellable)
    data->cancellable = g_object_ref (cancellable);
  data->callback = callback;
  data->user_data = user_data;

  attributes = g_new0 (gchar *, 2);
  attributes[0] = g_strdup ("device-uri");

  get_ipp_attributes_async (printer_name,
                            attributes,
                            get_device_attributes_async_scb,
                            data);

  g_strfreev (attributes);
}

/*
 * Return "count" best matching driver names for given printer.
 */
void
get_ppd_names_async (gchar        *printer_name,
                     gint          count,
                     GCancellable *cancellable,
                     GPNCallback   callback,
                     gpointer      user_data)
{
  GPNData *data;

  if (!printer_name)
    {
      callback (NULL, NULL, TRUE, user_data);
      return;
    }

  data = g_new0 (GPNData, 1);
  data->printer_name = g_strdup (printer_name);
  data->count = count;
  if (cancellable)
    data->cancellable = g_object_ref (cancellable);
  data->callback = callback;
  data->user_data = user_data;

  /*
   * We have to find out device-id for this printer at first.
   */
  get_device_attributes_async (printer_name,
                               cancellable,
                               get_device_attributes_cb,
                               data);
}

typedef struct
{
  PPDList      *result;
  GCancellable *cancellable;
  GAPCallback   callback;
  gpointer      user_data;
  GMainContext *context;
} GAPData;

static gboolean
get_all_ppds_idle_cb (gpointer user_data)
{
  GAPData *data = (GAPData *) user_data;

  /* Don't call callback if cancelled */
  if (data->cancellable &&
      g_cancellable_is_cancelled (data->cancellable))
    {
      ppd_list_free (data->result);
      data->result = NULL;
    }
  else
    {
      data->callback (data->result, data->user_data);
    }

  return FALSE;
}

static void
get_all_ppds_data_free (gpointer user_data)
{
  GAPData *data = (GAPData *) user_data;

  if (data->context)
    g_main_context_unref (data->context);
  if (data->cancellable)
    g_object_unref (data->cancellable);
  g_free (data);
}

static void
get_all_ppds_cb (gpointer user_data)
{
  GAPData *data = (GAPData *) user_data;
  GSource *idle_source;

  idle_source = g_idle_source_new ();
  g_source_set_callback (idle_source,
                         get_all_ppds_idle_cb,
                         data,
                         get_all_ppds_data_free);
  g_source_attach (idle_source, data->context);
  g_source_unref (idle_source);
}

static const struct {
  const char *normalized_name;
  const char *display_name;
} manufacturers_names[] = {
  { "alps", "Alps" },
  { "anitech", "Anitech" },
  { "apple", "Apple" },
  { "apollo", "Apollo" },
  { "brother", "Brother" },
  { "canon", "Canon" },
  { "citizen", "Citizen" },
  { "citoh", "Citoh" },
  { "compaq", "Compaq" },
  { "dec", "DEC" },
  { "dell", "Dell" },
  { "dnp", "DNP" },
  { "dymo", "Dymo" },
  { "epson", "Epson" },
  { "fujifilm", "Fujifilm" },
  { "fujitsu", "Fujitsu" },
  { "gelsprinter", "Ricoh" },
  { "generic", "Generic" },
  { "genicom", "Genicom" },
  { "gestetner", "Gestetner" },
  { "hewlett packard", "Hewlett-Packard" },
  { "heidelberg", "Heidelberg" },
  { "hitachi", "Hitachi" },
  { "hp", "Hewlett-Packard" },
  { "ibm", "IBM" },
  { "imagen", "Imagen" },
  { "imagistics", "Imagistics" },
  { "infoprint", "InfoPrint" },
  { "infotec", "Infotec" },
  { "intellitech", "Intellitech" },
  { "kodak", "Kodak" },
  { "konica minolta", "Minolta" },
  { "kyocera", "Kyocera" },
  { "kyocera mita", "Kyocera" },
  { "lanier", "Lanier" },
  { "lexmark international", "Lexmark" },
  { "lexmark", "Lexmark" },
  { "minolta", "Minolta" },
  { "minolta qms", "Minolta" },
  { "mitsubishi", "Mitsubishi" },
  { "nec", "NEC" },
  { "nrg", "NRG" },
  { "oce", "Oce" },
  { "oki", "Oki" },
  { "oki data corp", "Oki" },
  { "olivetti", "Olivetti" },
  { "olympus", "Olympus" },
  { "panasonic", "Panasonic" },
  { "pcpi", "PCPI" },
  { "pentax", "Pentax" },
  { "qms", "QMS" },
  { "raven", "Raven" },
  { "raw", "Raw" },
  { "ricoh", "Ricoh" },
  { "samsung", "Samsung" },
  { "savin", "Savin" },
  { "seiko", "Seiko" },
  { "sharp", "Sharp" },
  { "shinko", "Shinko" },
  { "sipix", "SiPix" },
  { "sony", "Sony" },
  { "star", "Star" },
  { "tally", "Tally" },
  { "tektronix", "Tektronix" },
  { "texas instruments", "Texas Instruments" },
  { "toshiba", "Toshiba" },
  { "toshiba tec corp.", "Toshiba" },
  { "xante", "Xante" },
  { "xerox", "Xerox" },
  { "zebra", "Zebra" },
};

static gpointer
get_all_ppds_func (gpointer user_data)
{
  ipp_attribute_t *attr;
  GHashTable      *ppds_hash = NULL;
  GHashTable      *manufacturers_hash = NULL;
  GAPData         *data = (GAPData *) user_data;
  PPDName         *item;
  ipp_t           *request;
  ipp_t           *response;
  GList           *list;
  gchar           *ppd_make_and_model;
  gchar           *ppd_device_id;
  gchar           *ppd_name;
  gchar           *ppd_product;
  gchar           *ppd_make;
  gchar           *mfg;
  gchar           *mfg_normalized;
  gchar           *mdl;
  gchar           *manufacturer_display_name;
  gint             i, j;

  request = ippNewRequest (CUPS_GET_PPDS);
  response = cupsDoRequest (CUPS_HTTP_DEFAULT, request, "/");

  if (response &&
      response->request.status.status_code <= IPP_OK_CONFLICT)
    {
      /*
       * This hash contains names of manufacturers as keys and
       * values are GLists of PPD names.
       */
      ppds_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

      /*
       * This hash contains all possible names of manufacturers as keys
       * and values are just first occurences of their equivalents.
       * This is for mapping of e.g. "Hewlett Packard" and "HP" to the same name
       * (the one which comes first).
       */
      manufacturers_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

      for (i = 0; i < G_N_ELEMENTS (manufacturers_names); i++)
        {
          g_hash_table_insert (manufacturers_hash,
                               g_strdup (manufacturers_names[i].normalized_name),
                               g_strdup (manufacturers_names[i].display_name));
        }

      for (attr = response->attrs; attr != NULL; attr = attr->next)
        {
          while (attr != NULL && attr->group_tag != IPP_TAG_PRINTER)
            attr = attr->next;

          if (attr == NULL)
            break;

          ppd_device_id = NULL;
          ppd_make_and_model = NULL;
          ppd_name = NULL;
          ppd_product = NULL;
          ppd_make = NULL;
          mfg = NULL;
          mfg_normalized = NULL;
          mdl = NULL;

          while (attr != NULL && attr->group_tag == IPP_TAG_PRINTER)
            {
              if (g_strcmp0 (attr->name, "ppd-device-id") == 0 &&
                  attr->value_tag == IPP_TAG_TEXT)
                ppd_device_id = attr->values[0].string.text;
              else if (g_strcmp0 (attr->name, "ppd-make-and-model") == 0 &&
                       attr->value_tag == IPP_TAG_TEXT)
                ppd_make_and_model = attr->values[0].string.text;
              else if (g_strcmp0 (attr->name, "ppd-name") == 0 &&
                       attr->value_tag == IPP_TAG_NAME)
                ppd_name = attr->values[0].string.text;
              else if (g_strcmp0 (attr->name, "ppd-product") == 0 &&
                       attr->value_tag == IPP_TAG_TEXT)
                ppd_product = attr->values[0].string.text;
              else if (g_strcmp0 (attr->name, "ppd-make") == 0 &&
                       attr->value_tag == IPP_TAG_TEXT)
                ppd_make = attr->values[0].string.text;

              attr = attr->next;
            }

          /* Get manufacturer's name */
          if (ppd_device_id && ppd_device_id[0] != '\0')
            {
              mfg = get_tag_value (ppd_device_id, "mfg");
              if (!mfg)
                mfg = get_tag_value (ppd_device_id, "manufacturer");
              mfg_normalized = normalize (mfg);
            }

          if (!mfg &&
              ppd_make &&
              ppd_make[0] != '\0')
            {
              mfg = g_strdup (ppd_make);
              mfg_normalized = normalize (ppd_make);
            }

          /* Get model */
          if (ppd_make_and_model &&
              ppd_make_and_model[0] != '\0')
            {
              mdl = g_strdup (ppd_make_and_model);
            }

          if (!mdl &&
              ppd_product &&
              ppd_product[0] != '\0')
            {
              mdl = g_strdup (ppd_product);
            }

          if (!mdl &&
              ppd_device_id &&
              ppd_device_id[0] != '\0')
            {
              mdl = get_tag_value (ppd_device_id, "mdl");
              if (!mdl)
                mdl = get_tag_value (ppd_device_id, "model");
            }

          if (ppd_name && ppd_name[0] != '\0' &&
              mdl && mdl[0] != '\0' &&
              mfg && mfg[0] != '\0')
            {
              manufacturer_display_name = g_hash_table_lookup (manufacturers_hash, mfg_normalized);
              if (!manufacturer_display_name)
                {
                  g_hash_table_insert (manufacturers_hash, g_strdup (mfg_normalized), g_strdup (mfg));
                }
              else
                {
                  g_free (mfg_normalized);
                  mfg_normalized = normalize (manufacturer_display_name);
                }

              item = g_new0 (PPDName, 1);
              item->ppd_name = g_strdup (ppd_name);
              item->ppd_display_name = g_strdup (mdl);
              item->ppd_match_level = -1;

              list = g_hash_table_lookup (ppds_hash, mfg_normalized);
              if (list)
                {
                  list = g_list_append (list, item);
                }
              else
                {
                  list = g_list_append (list, item);
                  g_hash_table_insert (ppds_hash, g_strdup (mfg_normalized), list);
                }
            }

          g_free (mdl);
          g_free (mfg);
          g_free (mfg_normalized);

          if (attr == NULL)
            break;
        }
    }

  if (response)
    ippDelete(response);

  if (ppds_hash &&
      manufacturers_hash)
    {
      GHashTableIter  iter;
      gpointer        key;
      gpointer        value;
      GList          *ppd_item;
      GList          *sort_list = NULL;
      GList          *list_iter;
      gchar          *name;

      data->result = g_new0 (PPDList, 1);
      data->result->num_of_manufacturers = g_hash_table_size (ppds_hash);
      data->result->manufacturers = g_new0 (PPDManufacturerItem *, data->result->num_of_manufacturers);

      g_hash_table_iter_init (&iter, ppds_hash);
      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          sort_list = g_list_append (sort_list, g_strdup (key));
        }

      /* Sort list of manufacturers */
      sort_list = g_list_sort (sort_list, (GCompareFunc) g_strcmp0);

      /*
       * Fill resulting list of lists (list of manufacturers where
       * each item contains list of PPD names)
       */
      i = 0;
      for (list_iter = sort_list; list_iter; list_iter = list_iter->next)
        {
          name = (gchar *) list_iter->data;
          value = g_hash_table_lookup (ppds_hash, name);

          data->result->manufacturers[i] = g_new0 (PPDManufacturerItem, 1);
          data->result->manufacturers[i]->manufacturer_name = g_strdup (name);
          data->result->manufacturers[i]->manufacturer_display_name = g_strdup (g_hash_table_lookup (manufacturers_hash, name));
          data->result->manufacturers[i]->num_of_ppds = g_list_length ((GList *) value);
          data->result->manufacturers[i]->ppds = g_new0 (PPDName *, data->result->manufacturers[i]->num_of_ppds);

          for (ppd_item = (GList *) value, j = 0; ppd_item; ppd_item = ppd_item->next, j++)
            {
              data->result->manufacturers[i]->ppds[j] = ppd_item->data;
            }

          g_list_free ((GList *) value);

          i++;
        }

      g_list_free_full (sort_list, g_free);
      g_hash_table_destroy (ppds_hash);
      g_hash_table_destroy (manufacturers_hash);
    }

  get_all_ppds_cb (data);

  return NULL;
}

/*
 * Get names of all installed PPDs sorted by manufacturers names.
 */
void
get_all_ppds_async (GCancellable *cancellable,
                    GAPCallback   callback,
                    gpointer      user_data)
{
  GAPData *data;
  GThread *thread;
  GError  *error = NULL;

  data = g_new0 (GAPData, 1);
  if (cancellable)
    data->cancellable = g_object_ref (cancellable);
  data->callback = callback;
  data->user_data = user_data;
  data->context = g_main_context_ref_thread_default ();

  thread = g_thread_try_new ("get-all-ppds",
                             get_all_ppds_func,
                             data,
                             &error);

  if (!thread)
    {
      g_warning ("%s", error->message);
      callback (NULL, user_data);

      g_error_free (error);
      get_all_ppds_data_free (data);
    }
  else
    {
      g_thread_unref (thread);
    }
}

PPDList *
ppd_list_copy (PPDList *list)
{
  PPDList *result = NULL;
  gint     i, j;

  if (list)
    {
      result = g_new0 (PPDList, 1);
      result->num_of_manufacturers = list->num_of_manufacturers;
      result->manufacturers = g_new0 (PPDManufacturerItem *, list->num_of_manufacturers);

      for (i = 0; i < result->num_of_manufacturers; i++)
        {
          result->manufacturers[i] = g_new0 (PPDManufacturerItem, 1);
          result->manufacturers[i]->num_of_ppds = list->manufacturers[i]->num_of_ppds;
          result->manufacturers[i]->ppds = g_new0 (PPDName *, result->manufacturers[i]->num_of_ppds);

          result->manufacturers[i]->manufacturer_display_name =
            g_strdup (list->manufacturers[i]->manufacturer_display_name);

          result->manufacturers[i]->manufacturer_name =
            g_strdup (list->manufacturers[i]->manufacturer_name);

          for (j = 0; j < result->manufacturers[i]->num_of_ppds; j++)
            {
              result->manufacturers[i]->ppds[j] = g_new0 (PPDName, 1);

              result->manufacturers[i]->ppds[j]->ppd_display_name =
                g_strdup (list->manufacturers[i]->ppds[j]->ppd_display_name);

              result->manufacturers[i]->ppds[j]->ppd_name =
                g_strdup (list->manufacturers[i]->ppds[j]->ppd_name);

              result->manufacturers[i]->ppds[j]->ppd_match_level =
                list->manufacturers[i]->ppds[j]->ppd_match_level;
            }
        }
    }

  return result;
}

void
ppd_list_free (PPDList *list)
{
  gint i, j;

  if (list)
    {
      for (i = 0; i < list->num_of_manufacturers; i++)
        {
          for (j = 0; j < list->manufacturers[i]->num_of_ppds; j++)
            {
              g_free (list->manufacturers[i]->ppds[j]->ppd_name);
              g_free (list->manufacturers[i]->ppds[j]->ppd_display_name);
              g_free (list->manufacturers[i]->ppds[j]);
            }

          g_free (list->manufacturers[i]->manufacturer_name);
          g_free (list->manufacturers[i]->manufacturer_display_name);
          g_free (list->manufacturers[i]->ppds);
          g_free (list->manufacturers[i]);
        }

      g_free (list->manufacturers);
      g_free (list);
    }
}

gchar *
get_standard_manufacturers_name (gchar *name)
{
  gchar *normalized_name;
  gchar *result = NULL;
  gint   i;

  if (name)
    {
      normalized_name = normalize (name);

      for (i = 0; i < G_N_ELEMENTS (manufacturers_names); i++)
        {
          if (g_strcmp0 (manufacturers_names[i].normalized_name, normalized_name) == 0)
            {
              result = g_strdup (manufacturers_names[i].display_name);
              break;
            }
        }

      g_free (normalized_name);
    }

  return result;
}

typedef struct
{
  gchar        *printer_name;
  gchar        *result;
  PGPCallback   callback;
  gpointer      user_data;
  GMainContext *context;
} PGPData;

static gboolean
printer_get_ppd_idle_cb (gpointer user_data)
{
  PGPData *data = (PGPData *) user_data;

  data->callback (data->result, data->user_data);

  return FALSE;
}

static void
printer_get_ppd_data_free (gpointer user_data)
{
  PGPData *data = (PGPData *) user_data;

  if (data->context)
    g_main_context_unref (data->context);
  g_free (data->result);
  g_free (data->printer_name);
  g_free (data);
}

static void
printer_get_ppd_cb (gpointer user_data)
{
  PGPData *data = (PGPData *) user_data;
  GSource *idle_source;

  idle_source = g_idle_source_new ();
  g_source_set_callback (idle_source,
                         printer_get_ppd_idle_cb,
                         data,
                         printer_get_ppd_data_free);
  g_source_attach (idle_source, data->context);
  g_source_unref (idle_source);
}

static gpointer
printer_get_ppd_func (gpointer user_data)
{
  PGPData *data = (PGPData *) user_data;

  data->result = g_strdup (cupsGetPPD (data->printer_name));

  printer_get_ppd_cb (data);

  return NULL;
}

void
printer_get_ppd_async (const gchar *printer_name,
                       PGPCallback  callback,
                       gpointer     user_data)
{
  PGPData *data;
  GThread *thread;
  GError  *error = NULL;

  data = g_new0 (PGPData, 1);
  data->printer_name = g_strdup (printer_name);
  data->callback = callback;
  data->user_data = user_data;
  data->context = g_main_context_ref_thread_default ();

  thread = g_thread_try_new ("printer-get-ppd",
                             printer_get_ppd_func,
                             data,
                             &error);

  if (!thread)
    {
      g_warning ("%s", error->message);
      callback (NULL, user_data);

      g_error_free (error);
      printer_get_ppd_data_free (data);
    }
  else
    {
      g_thread_unref (thread);
    }
}

typedef struct
{
  gchar        *printer_name;
  cups_dest_t  *result;
  GNDCallback   callback;
  gpointer      user_data;
  GMainContext *context;
} GNDData;

static gboolean
get_named_dest_idle_cb (gpointer user_data)
{
  GNDData *data = (GNDData *) user_data;

  data->callback (data->result, data->user_data);

  return FALSE;
}

static void
get_named_dest_data_free (gpointer user_data)
{
  GNDData *data = (GNDData *) user_data;

  if (data->context)
    g_main_context_unref (data->context);
  g_free (data->printer_name);
  g_free (data);
}

static void
get_named_dest_cb (gpointer user_data)
{
  GNDData *data = (GNDData *) user_data;
  GSource *idle_source;

  idle_source = g_idle_source_new ();
  g_source_set_callback (idle_source,
                         get_named_dest_idle_cb,
                         data,
                         get_named_dest_data_free);
  g_source_attach (idle_source, data->context);
  g_source_unref (idle_source);
}

static gpointer
get_named_dest_func (gpointer user_data)
{
  GNDData *data = (GNDData *) user_data;

  data->result = cupsGetNamedDest (CUPS_HTTP_DEFAULT, data->printer_name, NULL);

  get_named_dest_cb (data);

  return NULL;
}

void
get_named_dest_async (const gchar *printer_name,
                      GNDCallback  callback,
                      gpointer     user_data)
{
  GNDData *data;
  GThread *thread;
  GError  *error = NULL;

  data = g_new0 (GNDData, 1);
  data->printer_name = g_strdup (printer_name);
  data->callback = callback;
  data->user_data = user_data;
  data->context = g_main_context_ref_thread_default ();

  thread = g_thread_try_new ("get-named-dest",
                             get_named_dest_func,
                             data,
                             &error);

  if (!thread)
    {
      g_warning ("%s", error->message);
      callback (NULL, user_data);

      g_error_free (error);
      get_named_dest_data_free (data);
    }
  else
    {
      g_thread_unref (thread);
    }
}

typedef struct
{
  GCancellable *cancellable;
  PAOCallback   callback;
  gpointer      user_data;
} PAOData;

static void
printer_add_option_async_dbus_cb (GObject      *source_object,
                                  GAsyncResult *res,
                                  gpointer      user_data)
{
  GVariant *output;
  gboolean  success = FALSE;
  PAOData  *data = (PAOData *) user_data;
  GError   *error = NULL;

  output = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object),
                                          res,
                                          &error);
  g_object_unref (source_object);

  if (output)
    {
      const gchar *ret_error;

      g_variant_get (output, "(&s)", &ret_error);
      if (ret_error[0] != '\0')
        g_warning ("%s", ret_error);
      else
        success = TRUE;

      g_variant_unref (output);
    }
  else
    {
      if (error->code != G_IO_ERROR_CANCELLED)
        g_warning ("%s", error->message);
      g_error_free (error);
    }

  data->callback (success, data->user_data);

  if (data->cancellable)
    g_object_unref (data->cancellable);
  g_free (data);
}

void
printer_add_option_async (const gchar   *printer_name,
                          const gchar   *option_name,
                          gchar        **values,
                          gboolean       set_default,
                          GCancellable  *cancellable,
                          PAOCallback    callback,
                          gpointer       user_data)
{
  GVariantBuilder  array_builder;
  GDBusConnection *bus;
  PAOData         *data;
  GError          *error = NULL;
  gint             i;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
   {
     g_warning ("Failed to get system bus: %s", error->message);
     g_error_free (error);
     callback (FALSE, user_data);
     return;
   }

  g_variant_builder_init (&array_builder, G_VARIANT_TYPE ("as"));
  if (values)
    {
      for (i = 0; values[i]; i++)
        g_variant_builder_add (&array_builder, "s", values[i]);
    }

  data = g_new0 (PAOData, 1);
  data->cancellable = cancellable;
  data->callback = callback;
  data->user_data = user_data;

  g_dbus_connection_call (bus,
                          MECHANISM_BUS,
                          "/",
                          MECHANISM_BUS,
                          set_default ? "PrinterAddOptionDefault" :
                                        "PrinterAddOption",
                          g_variant_new ("(ssas)",
                                         printer_name,
                                         option_name,
                                         &array_builder),
                          G_VARIANT_TYPE ("(s)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          cancellable,
                          printer_add_option_async_dbus_cb,
                          data);
}

typedef struct
{
  gchar        *printer_name;
  gboolean      my_jobs;
  gint          which_jobs;
  cups_job_t   *jobs;
  gint          num_of_jobs;
  CGJCallback   callback;
  gpointer      user_data;
  GMainContext *context;
} CGJData;

static gboolean
cups_get_jobs_idle_cb (gpointer user_data)
{
  CGJData *data = (CGJData *) user_data;

  data->callback (data->jobs,
                  data->num_of_jobs,
                  data->user_data);

  return FALSE;
}

static void
cups_get_jobs_data_free (gpointer user_data)
{
  CGJData *data = (CGJData *) user_data;

  if (data->context)
    g_main_context_unref (data->context);
  g_free (data->printer_name);
  g_free (data);
}

static void
cups_get_jobs_cb (gpointer user_data)
{
  CGJData *data = (CGJData *) user_data;
  GSource *idle_source;

  idle_source = g_idle_source_new ();
  g_source_set_callback (idle_source,
                         cups_get_jobs_idle_cb,
                         data,
                         cups_get_jobs_data_free);
  g_source_attach (idle_source, data->context);
  g_source_unref (idle_source);
}

static gpointer
cups_get_jobs_func (gpointer user_data)
{
  CGJData *data = (CGJData *) user_data;

  data->num_of_jobs = cupsGetJobs (&data->jobs,
                                   data->printer_name,
                                   data->my_jobs ? 1 : 0,
                                   data->which_jobs);

  cups_get_jobs_cb (data);

  return NULL;
}

void
cups_get_jobs_async (const gchar *printer_name,
                     gboolean     my_jobs,
                     gint         which_jobs,
                     CGJCallback  callback,
                     gpointer     user_data)
{
  CGJData *data;
  GThread *thread;
  GError  *error = NULL;

  data = g_new0 (CGJData, 1);
  data->printer_name = g_strdup (printer_name);
  data->my_jobs = my_jobs;
  data->which_jobs = which_jobs;
  data->callback = callback;
  data->user_data = user_data;
  data->context = g_main_context_ref_thread_default ();

  thread = g_thread_try_new ("cups-get-jobs",
                             cups_get_jobs_func,
                             data,
                             &error);

  if (!thread)
    {
      g_warning ("%s", error->message);
      callback (NULL, 0, user_data);

      g_error_free (error);
      cups_get_jobs_data_free (data);
    }
  else
    {
      g_thread_unref (thread);
    }
}

typedef struct
{
  GCancellable *cancellable;
  JCPCallback   callback;
  gpointer      user_data;
} JCPData;

static void
job_cancel_purge_async_dbus_cb (GObject      *source_object,
                                GAsyncResult *res,
                                gpointer      user_data)
{
  GVariant *output;
  JCPData  *data = (JCPData *) user_data;
  GError   *error = NULL;

  output = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object),
                                          res,
                                          &error);
  g_object_unref (source_object);

  if (output)
    {
      g_variant_unref (output);
    }
  else
    {
      if (!g_cancellable_is_cancelled (data->cancellable))
        g_warning ("%s", error->message);
      g_error_free (error);
    }

  data->callback (data->user_data);

  if (data->cancellable)
    g_object_unref (data->cancellable);
  g_free (data);
}

void
job_cancel_purge_async (gint          job_id,
                        gboolean      job_purge,
                        GCancellable *cancellable,
                        JCPCallback   callback,
                        gpointer      user_data)
{
  GDBusConnection *bus;
  JCPData         *data;
  GError          *error = NULL;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
    {
      g_warning ("Failed to get session bus: %s", error->message);
      g_error_free (error);
      callback (user_data);
      return;
    }

  data = g_new0 (JCPData, 1);
  if (cancellable)
    data->cancellable = g_object_ref (cancellable);
  data->callback = callback;
  data->user_data = user_data;

  g_dbus_connection_call (bus,
                          MECHANISM_BUS,
                          "/",
                          MECHANISM_BUS,
                          "JobCancelPurge",
                          g_variant_new ("(ib)",
                                         job_id,
                                         job_purge),
                          G_VARIANT_TYPE ("(s)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          NULL,
                          job_cancel_purge_async_dbus_cb,
                          data);
}

typedef struct
{
  GCancellable *cancellable;
  JSHUCallback  callback;
  gpointer      user_data;
} JSHUData;

static void
job_set_hold_until_async_dbus_cb (GObject      *source_object,
                                  GAsyncResult *res,
                                  gpointer      user_data)
{
  GVariant *output;
  JSHUData *data = (JSHUData *) user_data;
  GError   *error = NULL;

  output = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object),
                                          res,
                                          &error);
  g_object_unref (source_object);

  if (output)
    {
      g_variant_unref (output);
    }
  else
    {
      if (!g_cancellable_is_cancelled (data->cancellable))
        g_warning ("%s", error->message);
      g_error_free (error);
    }

  data->callback (data->user_data);

  if (data->cancellable)
    g_object_unref (data->cancellable);
  g_free (data);
}

void
job_set_hold_until_async (gint          job_id,
                          const gchar  *job_hold_until,
                          GCancellable *cancellable,
                          JSHUCallback  callback,
                          gpointer      user_data)
{
  GDBusConnection *bus;
  JSHUData        *data;
  GError          *error = NULL;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus)
    {
      g_warning ("Failed to get session bus: %s", error->message);
      g_error_free (error);
      callback (user_data);
      return;
    }

  data = g_new0 (JSHUData, 1);
  if (cancellable)
    data->cancellable = g_object_ref (cancellable);
  data->callback = callback;
  data->user_data = user_data;

  g_dbus_connection_call (bus,
                          MECHANISM_BUS,
                          "/",
                          MECHANISM_BUS,
                          "JobSetHoldUntil",
                          g_variant_new ("(is)",
                                         job_id,
                                         job_hold_until),
                          G_VARIANT_TYPE ("(s)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          NULL,
                          job_set_hold_until_async_dbus_cb,
                          data);
}
