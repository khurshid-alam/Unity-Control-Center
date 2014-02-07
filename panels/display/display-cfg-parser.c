/*
 * Copyright (C) 2014 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Eleni Maria Stea <elenimaria.stea@canonical.com>
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "display-cfg-parser.h"

struct Node {
  char *key;
  char *value;
};

/* ConfigString */

struct ConfigString {
  GHashTable *hash;
  char *string;
};

ConfigString *
cfgstr_create (const char *orig_str)
{
  char *sptr, *str;

  str = alloca (strlen (orig_str) + 1);
  strcpy(str, orig_str);

  ConfigString *cfgstr;
  if (!(cfgstr = malloc (sizeof *cfgstr)))
    return 0;

  cfgstr->hash = g_hash_table_new_full (g_str_hash, g_str_equal, free, free);
  cfgstr->string = NULL;

  /* parsing */

  sptr = str;
  while (sptr && *sptr != 0) {
    char *key, *value, *end, *start, *vstart;

    if (*sptr == ';')
    {
      sptr++;
      continue;
    }

    start = sptr;
    if ((end = strchr (start, ';')))
    {
      *end = 0;
      sptr = end + 1;
    }
    else
    {
      sptr = NULL;
    }

    /* parse key/value from the string */
    if (!(vstart = strchr (start, '=')))
    {
      fprintf (stderr, "%s: invalid key-value pair: %s\n", __func__, start);
      cfgstr_destroy (cfgstr);
      return 0;
    }
    *vstart++ = 0;  /* terminate the key part and move the pointer to the start of the value */

    start = g_strstrip (start);
    vstart = g_strstrip (vstart);

    if (!(key = malloc (strlen (start) + 1)))
    {
      cfgstr_destroy (cfgstr);
      return 0;
    }
    if(!(value = malloc (strlen (vstart) + 1)))
    {
      free (key);
      cfgstr_destroy (cfgstr);

      return 0;
    }

    strcpy (key, start);
    strcpy (value, vstart);

    /* insert to the hash table */
    g_hash_table_insert (cfgstr->hash, key, value);
  }

  return cfgstr;
}

void
cfgstr_destroy (ConfigString *cfgstr)
{
  if (!cfgstr)
    return;

  g_hash_table_destroy (cfgstr->hash);

  free (cfgstr->string);
  free (cfgstr);
}

const char *
cfgstr_get_string (const ConfigString *cfgstr)
{
  int len;
  char *end;
  char *key;
  char *value;

  GHashTableIter *iter;

  free (cfgstr->string);

  /* determine the string size */
  len = 0;
  g_hash_table_iter_init (iter, cfgstr->hash);
  while (g_hash_table_iter_next (iter, (void**)&key, (void**)&value))
  {
    len += strlen (key) + strlen (value) + 2;
  }

  if (!(((ConfigString*)cfgstr)->string = malloc (len + 1)))
    return 0;

  end = cfgstr->string;

  g_hash_table_iter_init (iter, cfgstr->hash);
  while (g_hash_table_iter_next (iter, (void**)&key, (void**)&value))
  {
    end += sprintf (end, "%s=%s;", key, value);
  }

  return cfgstr->string;
}

const char *
cfgstr_get (const ConfigString *cfgstr, const char *key)
{
  return g_hash_table_lookup (cfgstr->hash, key);
}

float
cfgstr_get_float (const ConfigString *cfgstr, const char *key)
{
  const char *val_str = cfgstr_get (cfgstr, key);
  return val_str ? atof (val_str) : 0;
}

int
cfgstr_get_int (const ConfigString *cfgstr, const char *key)
{
  const char *val_str = cfgstr_get (cfgstr, key);
  return val_str ? atoi (val_str) : 0;
}

/*
 * returns:
 * -1: on error
 *  0: successfuly updated value
 *  1: added new key-value pair
 * */

int
cfgstr_set (ConfigString *cfgstr, const char *key, const char *value)
{
  char *new_val;
  char *new_key;

  if ((!(new_key = malloc (strlen (key) + 1))))
    return -1;
  strcpy (new_key, key);

  if((!(new_val = malloc (strlen (value) + 1))))
  {
    free (new_key);
    return -1;
  }
  strcpy (new_val, value);

  g_hash_table_insert (cfgstr->hash, new_key, new_val);
  return 0;
}

int
cfgstr_set_float (ConfigString *cfgstr, const char *key, float value)
{
  char buf[512];
  snprintf (buf, sizeof buf, "%f", value);
  buf[sizeof buf - 1] = 0; /* make sure it's null terminated */

  return cfgstr_set (cfgstr, key, buf);
}

int
cfgstr_set_int (ConfigString *cfgstr, const char *key, int value)
{
  char buf[32];
  snprintf (buf, sizeof buf, "%d", value);
  buf[sizeof buf - 1] = 0; /* make sure it's null terminated */

  return cfgstr_set (cfgstr, key, buf);
}
