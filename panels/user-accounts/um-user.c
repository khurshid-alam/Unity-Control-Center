/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
  *
  * Copyright (C) 2004-2005 James M. Cape <jcape@ignore-your.tv>.
  * Copyright (C) 2007-2008 William Jon McCann <mccann@jhu.edu>
  * Copyright (C) 2009 Red Hat, Inc.
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
  * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
  */

#define _XOPEN_SOURCE

#include "config.h"

#include <float.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <gio/gunixoutputstream.h>

#include <dbus/dbus-glib.h>

#include "um-user.h"
#include "um-account-type.h"
#include "um-utils.h"


 #define UM_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), UM_TYPE_USER, UmUserClass))
 #define UM_IS_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), UM_TYPE_USER))
#define UM_USER_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS ((object), UM_TYPE_USER, UmUserClass))

#define MAX_FILE_SIZE     65536

typedef struct {
        uid_t           uid;
        gchar          *user_name;
        gchar          *real_name;
        gint            account_type;
        gint            password_mode;
        gchar          *password_hint;
        gchar          *email;
        gchar          *language;
        gchar          *location;
        guint64         login_frequency;
        gchar          *icon_file;
        gboolean        locked;
        gboolean        automatic_login;
        gboolean        system_account;
} UserProperties;

static void
collect_props (const gchar    *key,
               const GValue   *value,
               UserProperties *props)
{
        gboolean handled = TRUE;

        if (strcmp (key, "Uid") == 0) {
                props->uid = g_value_get_uint64 (value);
        }
        else if (strcmp (key, "UserName") == 0) {
                props->user_name = g_value_dup_string (value);
        }
        else if (strcmp (key, "RealName") == 0) {
                props->real_name = g_value_dup_string (value);
        }
        else if (strcmp (key, "AccountType") == 0) {
                props->account_type = g_value_get_int (value);
        }
        else if (strcmp (key, "Email") == 0) {
                props->email = g_value_dup_string (value);
        }
        else if (strcmp (key, "Language") == 0) {
                props->language = g_value_dup_string (value);
        }
        else if (strcmp (key, "Location") == 0) {
                props->location = g_value_dup_string (value);
        }
        else if (strcmp (key, "LoginFrequency") == 0) {
                props->login_frequency = g_value_get_uint64 (value);
        }
        else if (strcmp (key, "IconFile") == 0) {
                props->icon_file = g_value_dup_string (value);
        }
        else if (strcmp (key, "Locked") == 0) {
                props->locked = g_value_get_boolean (value);
        }
        else if (strcmp (key, "AutomaticLogin") == 0) {
                props->automatic_login = g_value_get_boolean (value);
        }
        else if (strcmp (key, "SystemAccount") == 0) {
                props->system_account = g_value_get_boolean (value);
        }
        else if (strcmp (key, "PasswordMode") == 0) {
                props->password_mode = g_value_get_int (value);
        }
        else if (strcmp (key, "PasswordHint") == 0) {
                props->password_hint = g_value_dup_string (value);
        }
        else if (strcmp (key, "HomeDirectory") == 0) {
                /* ignore */
        }
        else if (strcmp (key, "Shell") == 0) {
                /* ignore */
        }
        else {
                handled = FALSE;
        }

        if (!handled)
                g_debug ("unhandled property %s", key);
}

static void
user_properties_free (UserProperties *props)
{
        g_free (props->user_name);
        g_free (props->real_name);
        g_free (props->password_hint);
        g_free (props->email);
        g_free (props->language);
        g_free (props->location);
        g_free (props->icon_file);
        g_free (props);
}

static UserProperties *
user_properties_get (DBusGConnection *bus,
                     const gchar     *object_path)
{
        UserProperties *props;
        GError *error;
        DBusGProxy *proxy;
        GHashTable *hash_table;

        props = g_new0 (UserProperties, 1);

        proxy = dbus_g_proxy_new_for_name (bus,
                                           "org.freedesktop.Accounts",
                                           object_path,
                                           "org.freedesktop.DBus.Properties");
        error = NULL;
        if (!dbus_g_proxy_call (proxy,
                                "GetAll",
                                &error,
                                G_TYPE_STRING,
                                "org.freedesktop.Accounts.User",
                                G_TYPE_INVALID,
                                dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE),
                                &hash_table,
                                G_TYPE_INVALID)) {
                g_debug ("Error calling GetAll() when retrieving properties for %s: %s", object_path, error->message);
                g_error_free (error);
                g_free (props);
                props = NULL;
                goto out;
        }
        g_hash_table_foreach (hash_table, (GHFunc) collect_props, props);
        g_hash_table_unref (hash_table);

 out:
        g_object_unref (proxy);
        return props;
}


struct _UmUser {
        GObject         parent;

        DBusGConnection *bus;
        DBusGProxy      *proxy;
        gchar           *object_path;

        UserProperties  *props;

        gchar           *display_name;
};

typedef struct _UmUserClass
{
        GObjectClass parent_class;
} UmUserClass;

enum {
        CHANGED,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void um_user_finalize (GObject *object);

G_DEFINE_TYPE (UmUser, um_user, G_TYPE_OBJECT)

static void
um_user_class_init (UmUserClass *class)
{
        GObjectClass *gobject_class;

        gobject_class = G_OBJECT_CLASS (class);

        gobject_class->finalize = um_user_finalize;

        signals[CHANGED] = g_signal_new ("changed",
                                         G_TYPE_FROM_CLASS (class),
                                         G_SIGNAL_RUN_LAST,
                                         0,
                                         NULL, NULL,
                                         g_cclosure_marshal_VOID__VOID,
                                         G_TYPE_NONE, 0);
}


static void
um_user_init (UmUser *user)
{
}

static void
um_user_finalize (GObject *object)
{
        UmUser *user;

        user = UM_USER (object);

        dbus_g_connection_unref (user->bus);
        g_free (user->object_path);

        if (user->proxy != NULL)
                g_object_unref (user->proxy);

        if (user->props != NULL)
                user_properties_free (user->props);

        (*G_OBJECT_CLASS (um_user_parent_class)->finalize) (object);
}

uid_t
um_user_get_uid (UmUser *user)
{
        g_return_val_if_fail (UM_IS_USER (user), -1);

        return user->props->uid;
}

const gchar *
um_user_get_real_name (UmUser *user)
{
        g_return_val_if_fail (UM_IS_USER (user), NULL);

        return user->props->real_name;
}

const gchar *
um_user_get_display_name (UmUser *user)
{
        g_return_val_if_fail (UM_IS_USER (user), NULL);

        if (user->display_name)
                return user->display_name;
        if (user->props->real_name &&
            *user->props->real_name != '\0')
                return user->props->real_name;

        return user->props->user_name;
}

const gchar *
um_user_get_user_name (UmUser *user)
{
        g_return_val_if_fail (UM_IS_USER (user), NULL);

        return user->props->user_name;
}

gint
um_user_get_account_type (UmUser *user)
{
        g_return_val_if_fail (UM_IS_USER (user), UM_ACCOUNT_TYPE_STANDARD);

        return user->props->account_type;
}

gulong
um_user_get_login_frequency (UmUser *user)
{
        g_return_val_if_fail (UM_IS_USER (user), 0);

        return user->props->login_frequency;
}

gint
um_user_collate (UmUser *user1,
                  UmUser *user2)
{
        const char *str1;
        const char *str2;
        gulong      num1;
        gulong      num2;

        g_return_val_if_fail (UM_IS_USER (user1), 0);
        g_return_val_if_fail (UM_IS_USER (user2), 0);

        num1 = user1->props->login_frequency;
        num2 = user2->props->login_frequency;
        if (num1 > num2) {
                return -1;
        }

        if (num1 < num2) {
                return 1;
        }

        /* if login frequency is equal try names */
        if (user1->props->real_name != NULL) {
                str1 = user1->props->real_name;
        } else {
                str1 = user1->props->user_name;
        }

        if (user2->props->real_name != NULL) {
                str2 = user2->props->real_name;
        } else {
                str2 = user2->props->user_name;
        }

        if (str1 == NULL && str2 != NULL) {
                return -1;
        }

        if (str1 != NULL && str2 == NULL) {
                return 1;
        }

        if (str1 == NULL && str2 == NULL) {
                return 0;
        }

        return g_utf8_collate (str1, str2);
}

static gboolean
check_user_file (const char *filename,
                 gssize      max_file_size)
{
        struct stat fileinfo;

        if (max_file_size < 0) {
                max_file_size = G_MAXSIZE;
        }

        /* Exists/Readable? */
        if (stat (filename, &fileinfo) < 0) {
                g_debug ("File does not exist");
                return FALSE;
        }

        /* Is a regular file */
        if (G_UNLIKELY (!S_ISREG (fileinfo.st_mode))) {
                g_debug ("File is not a regular file");
                return FALSE;
        }

        /* Size is kosher? */
        if (G_UNLIKELY (fileinfo.st_size > max_file_size)) {
                g_debug ("File is too large");
                return FALSE;
        }

        return TRUE;
}

static cairo_surface_t *
surface_from_pixbuf (GdkPixbuf *pixbuf)
{
        cairo_surface_t *surface;
        cairo_t         *cr;

        surface = cairo_image_surface_create (gdk_pixbuf_get_has_alpha (pixbuf) ?
                                              CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24,
                                              gdk_pixbuf_get_width (pixbuf),
                                              gdk_pixbuf_get_height (pixbuf));
        cr = cairo_create (surface);
        gdk_cairo_set_source_pixbuf (cr, pixbuf, 0, 0);
        cairo_paint (cr);
        cairo_destroy (cr);

        return surface;
}

/**
 * go_cairo_convert_data_to_pixbuf:
 * @src: a pointer to pixel data in cairo format
 * @dst: a pointer to pixel data in pixbuf format
 * @width: image width
 * @height: image height
 * @rowstride: data rowstride
 *
 * Converts the pixel data stored in @src in CAIRO_FORMAT_ARGB32 cairo format
 * to GDK_COLORSPACE_RGB pixbuf format and move them
 * to @dst. If @src == @dst, pixel are converted in place.
 **/

static void
go_cairo_convert_data_to_pixbuf (unsigned char *dst,
                                 unsigned char const *src,
                                 int width,
                                 int height,
                                 int rowstride)
{
        int i,j;
        unsigned int t;
        unsigned char a, b, c;

        g_return_if_fail (dst != NULL);

#define MULT(d,c,a,t) G_STMT_START { t = (a)? c * 255 / a: 0; d = t;} G_STMT_END

        if (src == dst || src == NULL) {
                for (i = 0; i < height; i++) {
                        for (j = 0; j < width; j++) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
                                MULT(a, dst[2], dst[3], t);
                                MULT(b, dst[1], dst[3], t);
                                MULT(c, dst[0], dst[3], t);
                                dst[0] = a;
                                dst[1] = b;
                                dst[2] = c;
#else
                                MULT(a, dst[1], dst[0], t);
                                MULT(b, dst[2], dst[0], t);
                                MULT(c, dst[3], dst[0], t);
                                dst[3] = dst[0];
                                dst[0] = a;
                                dst[1] = b;
                                dst[2] = c;
#endif
                                dst += 4;
                        }
                        dst += rowstride - width * 4;
                }
        } else {
                for (i = 0; i < height; i++) {
                        for (j = 0; j < width; j++) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
                                MULT(dst[0], src[2], src[3], t);
                                MULT(dst[1], src[1], src[3], t);
                                MULT(dst[2], src[0], src[3], t);
                                dst[3] = src[3];
#else
                                MULT(dst[0], src[1], src[0], t);
                                MULT(dst[1], src[2], src[0], t);
                                MULT(dst[2], src[3], src[0], t);
                                dst[3] = src[0];
#endif
                                src += 4;
                                dst += 4;
                        }
                        src += rowstride - width * 4;
                        dst += rowstride - width * 4;
                }
        }
#undef MULT
}

static void
cairo_to_pixbuf (guint8    *src_data,
                 GdkPixbuf *dst_pixbuf)
{
        unsigned char *src;
        unsigned char *dst;
        guint          w;
        guint          h;
        guint          rowstride;

        w = gdk_pixbuf_get_width (dst_pixbuf);
        h = gdk_pixbuf_get_height (dst_pixbuf);
        rowstride = gdk_pixbuf_get_rowstride (dst_pixbuf);

        dst = gdk_pixbuf_get_pixels (dst_pixbuf);
        src = src_data;

        go_cairo_convert_data_to_pixbuf (dst, src, w, h, rowstride);
}

static GdkPixbuf *
frame_pixbuf (GdkPixbuf *source)
{
        GdkPixbuf       *dest;
        cairo_t         *cr;
        cairo_surface_t *surface;
        guint            w;
        guint            h;
        guint            rowstride;
        int              frame_width;
        double           radius;
        guint8          *data;

        frame_width = 2;

        w = gdk_pixbuf_get_width (source) + frame_width * 2;
        h = gdk_pixbuf_get_height (source) + frame_width * 2;
        radius = w / 10;

        dest = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                               TRUE,
                               8,
                               w,
                               h);
        rowstride = gdk_pixbuf_get_rowstride (dest);


        data = g_new0 (guint8, h * rowstride);

        surface = cairo_image_surface_create_for_data (data,
                                                       CAIRO_FORMAT_ARGB32,
                                                       w,
                                                       h,
                                                       rowstride);
        cr = cairo_create (surface);
        cairo_surface_destroy (surface);

        /* set up image */
        cairo_rectangle (cr, 0, 0, w, h);
        cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.0);
        cairo_fill (cr);

        rounded_rectangle (cr, 1.0, 0.5, 0.5, radius, w - 1, h - 1);
        cairo_set_source_rgba (cr, 0.5, 0.5, 0.5, 0.3);
        cairo_fill_preserve (cr);

        surface = surface_from_pixbuf (source);
        cairo_set_source_surface (cr, surface, frame_width, frame_width);
        cairo_fill (cr);
        cairo_surface_destroy (surface);

        cairo_to_pixbuf (data, dest);

        cairo_destroy (cr);
        g_free (data);

        return dest;
}

GdkPixbuf *
um_user_render_icon (UmUser   *user,
                     gboolean  with_frame,
                     gint      icon_size)
{
        GdkPixbuf    *pixbuf;
        GdkPixbuf    *framed;
        gboolean      res;
        GError       *error;

        g_return_val_if_fail (UM_IS_USER (user), NULL);
        g_return_val_if_fail (icon_size > 12, NULL);

        pixbuf = NULL;
        if (user->props->icon_file) {
                res = check_user_file (user->props->icon_file,
                                       MAX_FILE_SIZE);
                if (res) {
                        pixbuf = gdk_pixbuf_new_from_file_at_size (user->props->icon_file,
                                                                   icon_size,
                                                                   icon_size,
                                                                   NULL);
                }
                else {
                        pixbuf = NULL;
                }
        }

        if (pixbuf != NULL) {
                goto out;
        }

        error = NULL;
        pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),

                                           "avatar-default",
                                           icon_size,
                                           GTK_ICON_LOOKUP_FORCE_SIZE,
                                           &error);
        if (error) {
                g_warning ("%s", error->message);
                g_error_free (error);
        }

 out:

        if (pixbuf != NULL && with_frame) {
                framed = frame_pixbuf (pixbuf);
                if (framed != NULL) {
                        g_object_unref (pixbuf);
                        pixbuf = framed;
                }
        }

        return pixbuf;
}

const gchar *
um_user_get_email (UmUser *user)
{
        g_return_val_if_fail (UM_IS_USER (user), NULL);

        return user->props->email;
}

const gchar *
um_user_get_language (UmUser *user)
{
        g_return_val_if_fail (UM_IS_USER (user), NULL);

        if (*user->props->language == '\0')
                return NULL;
        return user->props->language;
}

const gchar *
um_user_get_location (UmUser *user)
{
        g_return_val_if_fail (UM_IS_USER (user), NULL);

        return user->props->location;
}

gint
um_user_get_password_mode (UmUser *user)
{
        g_return_val_if_fail (UM_IS_USER (user), UM_PASSWORD_MODE_NONE);

        return user->props->password_mode;
}

const char *
um_user_get_password_hint (UmUser *user)
{
        g_return_val_if_fail (UM_IS_USER (user), NULL);

        return user->props->password_hint;
}

const char *
um_user_get_icon_file (UmUser *user)
{
        g_return_val_if_fail (UM_IS_USER (user), NULL);

        return user->props->icon_file;
}

gboolean
um_user_get_locked (UmUser *user)
{
        g_return_val_if_fail (UM_IS_USER (user), FALSE);

        return user->props->locked;
}
gboolean
um_user_get_automatic_login (UmUser *user)
{
        g_return_val_if_fail (UM_IS_USER (user), FALSE);

        return user->props->automatic_login;
}

gboolean
um_user_is_system_account (UmUser *user)
{
        g_return_val_if_fail (UM_IS_USER (user), FALSE);

        return user->props->system_account;
}

const gchar *
um_user_get_object_path (UmUser *user)
{
        g_return_val_if_fail (UM_IS_USER (user), NULL);

        return user->object_path;
}

static gboolean
update_info (UmUser *user)
{
        UserProperties *props;

        props = user_properties_get (user->bus, user->object_path);
        if (props != NULL) {
                if (user->props != NULL)
                        user_properties_free (user->props);
                user->props = props;
                return TRUE;
        }
        else {
                return FALSE;
        }
}

static void
changed_handler (DBusGProxy *proxy,
                 gpointer   *data)
{
        UmUser *user = UM_USER (data);

        if (update_info (user)) {
                if (user->display_name != NULL) {
                        um_user_show_full_display_name (user);
                }

                g_signal_emit (user, signals[CHANGED], 0);
        }
}

UmUser *
um_user_new_from_object_path (const gchar *object_path)
{
        UmUser *user;
        GError *error;

        user = (UmUser *)g_object_new (UM_TYPE_USER, NULL);
        user->object_path = g_strdup (object_path);

        error = NULL;
        user->bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (user->bus == NULL) {
                g_warning ("Couldn't connect to system bus: %s", error->message);
                goto error;
        }

        user->proxy = dbus_g_proxy_new_for_name (user->bus,
                                                 "org.freedesktop.Accounts",
                                                 user->object_path,
                                                 "org.freedesktop.Accounts.User");
        dbus_g_proxy_set_default_timeout (user->proxy, INT_MAX);
        dbus_g_proxy_add_signal (user->proxy, "Changed", G_TYPE_INVALID);

        dbus_g_proxy_connect_signal (user->proxy, "Changed",
                                     G_CALLBACK (changed_handler), user, NULL);

        if (!update_info (user))
                goto error;

        return user;

 error:
        g_object_unref (user);
        return NULL;
}

void
um_user_set_email (UmUser      *user,
                   const gchar *email)
{
        GError *error = NULL;

        if (!dbus_g_proxy_call (user->proxy,
                                "SetEmail",
                                &error,
                                G_TYPE_STRING, email,
                                G_TYPE_INVALID,
                                G_TYPE_INVALID)) {
                g_warning ("SetEmail call failed: %s", error->message);
                g_error_free (error);
                return;
        }
}

void
um_user_set_language (UmUser      *user,
                      const gchar *language)
{
        GError *error = NULL;

        if (!dbus_g_proxy_call (user->proxy,
                                "SetLanguage",
                                &error,
                                G_TYPE_STRING, language,
                                G_TYPE_INVALID,
                                G_TYPE_INVALID)) {
                g_warning ("SetLanguage call failed: %s", error->message);
                g_error_free (error);
                return;
        }
}

void
um_user_set_location (UmUser      *user,
                      const gchar *location)
{
        GError *error = NULL;

        if (!dbus_g_proxy_call (user->proxy,
                                "SetLocation",
                                &error,
                                G_TYPE_STRING, location,
                                G_TYPE_INVALID,
                                G_TYPE_INVALID)) {
                g_warning ("SetLocation call failed: %s", error->message);
                g_error_free (error);
                return;
        }
}

void
um_user_set_user_name (UmUser      *user,
                       const gchar *user_name)
{
        GError *error = NULL;

        if (!dbus_g_proxy_call (user->proxy,
                                "SetUserName",
                                &error,
                                G_TYPE_STRING, user_name,
                                G_TYPE_INVALID,
                                G_TYPE_INVALID)) {
                g_warning ("SetUserName call failed: %s", error->message);
                g_error_free (error);
                return;
        }
}

void
um_user_set_real_name (UmUser      *user,
                       const gchar *real_name)
{
        GError *error = NULL;

        if (!dbus_g_proxy_call (user->proxy,
                                "SetRealName",
                                &error,
                                G_TYPE_STRING, real_name,
                                G_TYPE_INVALID,
                                G_TYPE_INVALID)) {
                g_warning ("SetRealName call failed: %s", error->message);
                g_error_free (error);
                return;
        }
}

void
um_user_set_icon_file (UmUser      *user,
                       const gchar *icon_file)
{
        GError *error = NULL;

        if (!dbus_g_proxy_call (user->proxy,
                                "SetIconFile",
                                &error,
                                G_TYPE_STRING, icon_file,
                                G_TYPE_INVALID,
                                G_TYPE_INVALID)) {
                g_warning ("SetIconFile call failed: %s", error->message);
                g_error_free (error);
                return;
        }
}

void
um_user_set_icon_data (UmUser    *user,
                       GdkPixbuf *pixbuf)
{
        gchar *path;
        gint fd;
        GOutputStream *stream;
        GError *error;

        path = g_build_filename (g_get_tmp_dir (), "usericonXXXXXX", NULL);
        fd = g_mkstemp (path);

        if (fd == -1) {
                g_warning ("failed to create temporary file for image data");
                g_free (path);
                return;
        }

        stream = g_unix_output_stream_new (fd, TRUE);

        error = NULL;
        if (!gdk_pixbuf_save_to_stream (pixbuf, stream, "png", NULL, &error, NULL)) {
                g_warning ("failed to save image: %s", error->message);
                g_error_free (error);
                g_object_unref (stream);
                return;
        }

        g_object_unref (stream);

        um_user_set_icon_file (user, path);

        /* if we ever make the dbus call async, the g_remove call needs
         * to wait for its completion
         */
        g_remove (path);

        g_free (path);
}

void
um_user_set_account_type (UmUser *user,
                          gint    account_type)
{
        GError *error = NULL;

        if (!dbus_g_proxy_call (user->proxy,
                                "SetAccountType",
                                &error,
                                G_TYPE_INT, account_type,
                                G_TYPE_INVALID,
                                G_TYPE_INVALID)) {
                g_warning ("SetAccountType call failed: %s", error->message);
                g_error_free (error);
                return;
        }
}

static gchar
salt_char (GRand *rand)
{
        gchar salt[] = "ABCDEFGHIJKLMNOPQRSTUVXYZ"
                       "abcdefghijklmnopqrstuvxyz"
                       "./0123456789";

        return salt[g_rand_int_range (rand, 0, G_N_ELEMENTS (salt))];
}

static gchar *
make_crypted (const gchar *plain)
{
        GString *salt;
        gchar *result;
        GRand *rand;
        gint i;

        rand = g_rand_new ();
        salt = g_string_sized_new (21);

        /* SHA 256 */
        g_string_append (salt, "$6$");
        for (i = 0; i < 16; i++) {
                g_string_append_c (salt, salt_char (rand));
        }
        g_string_append_c (salt, '$');

        result = g_strdup (crypt (plain, salt->str));

        g_string_free (salt, TRUE);
        g_rand_free (rand);

        return result;
}

void
um_user_set_password (UmUser      *user,
                      gint         password_mode,
                      const gchar *password,
                      const gchar *hint)
{
        GError *error = NULL;
        gchar *crypted;

        if (password_mode == 0) {
                crypted = make_crypted (password);
                if (!dbus_g_proxy_call (user->proxy,
                                        "SetPassword",
                                        &error,
                                        G_TYPE_STRING, crypted,
                                        G_TYPE_STRING, hint,
                                        G_TYPE_INVALID,
                                        G_TYPE_INVALID)) {
                        g_warning ("SetPassword call failed: %s", error->message);
                        g_error_free (error);
                }
                memset (crypted, 0, strlen (crypted));
                g_free (crypted);
        }
        else if (password_mode == 3 || password_mode == 4) {
                if (!dbus_g_proxy_call (user->proxy,
                                        "SetLocked",
                                        &error,
                                        G_TYPE_BOOLEAN, (password_mode == 3),
                                        G_TYPE_INVALID,
                                        G_TYPE_INVALID)) {
                        g_warning ("SetLocked call failed: %s", error->message);
                        g_error_free (error);
                }
        }
        else {
                if (!dbus_g_proxy_call (user->proxy,
                                        "SetPasswordMode",
                                        &error,
                                        G_TYPE_INT, password_mode,
                                        G_TYPE_INVALID,
                                        G_TYPE_INVALID)) {
                        g_warning ("SetPasswordMode call failed: %s", error->message);
                        g_error_free (error);
                }
        }
}

gboolean
um_user_is_logged_in (UmUser *user)
{
        DBusGProxy *proxy;
        GPtrArray *array;
        GError *error;
        gint n_sessions;

        proxy = dbus_g_proxy_new_for_name (user->bus,
                                           "org.freedesktop.ConsoleKit",
                                           "/org/freedesktop/ConsoleKit/Manager",
                                           "org.freedesktop.ConsoleKit.Manager");

        array = NULL;
        error = NULL;
        if (!dbus_g_proxy_call (proxy,
                                "GetSessionsForUnixUser",
                                &error,
                                G_TYPE_UINT, um_user_get_uid (user),
                                G_TYPE_INVALID,
                                dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH), &array,
                                G_TYPE_INVALID)) {
                g_warning ("GetSessionsForUnixUser failed: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        n_sessions = array->len;

        g_ptr_array_foreach (array, (GFunc)g_free, NULL);
        g_ptr_array_free (array, TRUE);

        g_object_unref (proxy);

        return n_sessions > 0;
}


void
um_user_set_automatic_login (UmUser   *user,
                             gboolean  enabled)
{
        GError *error = NULL;

        if (!dbus_g_proxy_call (user->proxy,
                                "SetAutomaticLogin",
                                &error,
                                G_TYPE_BOOLEAN, enabled,
                                G_TYPE_INVALID,
                                G_TYPE_INVALID)) {
                g_warning ("SetAutomaticLogin call failed: %s", error->message);
                g_error_free (error);
        }
}

void
um_user_show_full_display_name (UmUser *user)
{
        char *uniq_name;

        g_return_if_fail (UM_IS_USER (user));

        if (user->props->real_name != NULL) {
                uniq_name = g_strdup_printf ("%s (%s)",
                                             user->props->real_name,
                                             user->props->user_name);
        } else {
                uniq_name = NULL;
        }

        if (uniq_name && g_strcmp0 (uniq_name, user->display_name) != 0) {
                g_free (user->display_name);
                user->display_name = uniq_name;
        }
        else {
                g_free (uniq_name);
        }
}

void
um_user_show_short_display_name (UmUser *user)
{
        g_return_if_fail (UM_IS_USER (user));

        if (user->display_name) {
                g_free (user->display_name);
                user->display_name = NULL;
        }
}

