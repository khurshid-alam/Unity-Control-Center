/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2012 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib-object.h>
#include <glib/gi18n.h>

//#include <arpa/inet.h>
#include <netinet/ether.h>

#include <nm-client.h>
#include <nm-utils.h>
#include <nm-device.h>
#include <nm-device-wifi.h>
#include <nm-device-ethernet.h>
#include <nm-setting-wireless-security.h>
#include <nm-remote-connection.h>
#include <nm-setting-wireless.h>

#include "network-dialogs.h"
#include "panel-common.h"
#include "panel-cell-renderer-mode.h"
#include "panel-cell-renderer-signal.h"
#include "panel-cell-renderer-security.h"

#include "net-device-wifi.h"

#define NET_DEVICE_WIFI_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NET_TYPE_DEVICE_WIFI, NetDeviceWifiPrivate))

static void nm_device_wifi_refresh_ui (NetDeviceWifi *device_wifi);

struct _NetDeviceWifiPrivate
{
        GtkBuilder              *builder;
        gboolean                 updating_device;
};

G_DEFINE_TYPE (NetDeviceWifi, net_device_wifi, NET_TYPE_DEVICE)

enum {
        COLUMN_ID,
        COLUMN_TITLE,
        COLUMN_SORT,
        COLUMN_STRENGTH,
        COLUMN_MODE,
        COLUMN_SECURITY,
        COLUMN_ACTIVE,
        COLUMN_LAST
};

static GtkWidget *
device_wifi_proxy_add_to_notebook (NetObject *object,
                                    GtkNotebook *notebook,
                                    GtkSizeGroup *heading_size_group)
{
        GtkWidget *widget;
        GtkWindow *window;
        NetDeviceWifi *device_wifi = NET_DEVICE_WIFI (object);

        /* add widgets to size group */
        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder,
                                                     "heading_ipv4"));
        gtk_size_group_add_widget (heading_size_group, widget);

        /* reparent */
        window = GTK_WINDOW (gtk_builder_get_object (device_wifi->priv->builder,
                                                     "window_tmp"));
        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder,
                                                     "notebook_view"));
        g_object_ref (widget);
        gtk_container_remove (GTK_CONTAINER (window), widget);
        gtk_notebook_append_page (notebook, widget, NULL);
        g_object_unref (widget);

        return widget;
}

static guint
get_access_point_security (NMAccessPoint *ap)
{
        NM80211ApFlags flags;
        NM80211ApSecurityFlags wpa_flags;
        NM80211ApSecurityFlags rsn_flags;
        guint type;

        flags = nm_access_point_get_flags (ap);
        wpa_flags = nm_access_point_get_wpa_flags (ap);
        rsn_flags = nm_access_point_get_rsn_flags (ap);

        if (!(flags & NM_802_11_AP_FLAGS_PRIVACY) &&
            wpa_flags == NM_802_11_AP_SEC_NONE &&
            rsn_flags == NM_802_11_AP_SEC_NONE)
                type = NM_AP_SEC_NONE;
        else if ((flags & NM_802_11_AP_FLAGS_PRIVACY) &&
                 wpa_flags == NM_802_11_AP_SEC_NONE &&
                 rsn_flags == NM_802_11_AP_SEC_NONE)
                type = NM_AP_SEC_WEP;
        else if (!(flags & NM_802_11_AP_FLAGS_PRIVACY) &&
                 wpa_flags != NM_802_11_AP_SEC_NONE &&
                 rsn_flags != NM_802_11_AP_SEC_NONE)
                type = NM_AP_SEC_WPA;
        else
                type = NM_AP_SEC_WPA2;

        return type;
}

static void
add_access_point (NetDeviceWifi *device_wifi, NMAccessPoint *ap, NMAccessPoint *active, NMDevice *device)
{
        NetDeviceWifiPrivate *priv = device_wifi->priv;
        const GByteArray *ssid;
        const gchar *ssid_text;
        const gchar *object_path;
        GtkListStore *liststore_network;
        GtkTreeIter treeiter;
        GtkWidget *widget;
        gboolean is_active_ap;

        ssid = nm_access_point_get_ssid (ap);
        if (ssid == NULL)
                return;
        ssid_text = nm_utils_escape_ssid (ssid->data, ssid->len);

        is_active_ap = active && nm_utils_same_ssid (ssid, nm_access_point_get_ssid (active), TRUE);

        liststore_network = GTK_LIST_STORE (gtk_builder_get_object (priv->builder,
                                            "liststore_network"));

        object_path = nm_object_get_path (NM_OBJECT (ap));
        gtk_list_store_insert_with_values (liststore_network,
                                           &treeiter,
                                           -1,
                                           COLUMN_ID, object_path,
                                           COLUMN_TITLE, ssid_text,
                                           COLUMN_SORT, ssid_text,
                                           COLUMN_STRENGTH, nm_access_point_get_strength (ap),
                                           COLUMN_MODE, nm_access_point_get_mode (ap),
                                           COLUMN_SECURITY, get_access_point_security (ap),
                                           COLUMN_ACTIVE, is_active_ap,
                                           -1);

        /* is this what we're on already? */
        if (is_active_ap) {
                widget = GTK_WIDGET (gtk_builder_get_object (priv->builder,
                                                             "combobox_network_name"));
                gtk_combo_box_set_active_iter (GTK_COMBO_BOX (widget), &treeiter);
        }

//        if (priv->arg_operation == OPERATION_CONNECT_8021X &&
//            g_strcmp0(priv->arg_device, nm_object_get_path (NM_OBJECT (device))) == 0 &&
//            g_strcmp0(priv->arg_access_point, object_path) == 0) {
//                cc_network_panel_connect_to_8021x_network (panel,
//                                                           priv->client,
//                                                           priv->remote_settings,
//                                                           device,
//                                                           ap);
//                priv->arg_operation = OPERATION_NULL; /* done */
//        }
}

static void
add_access_point_other (NetDeviceWifi *device_wifi)
{
        NetDeviceWifiPrivate *priv = device_wifi->priv;
        GtkListStore *liststore_network;
        GtkTreeIter treeiter;

        liststore_network = GTK_LIST_STORE (gtk_builder_get_object (priv->builder,
                                                     "liststore_network"));

        gtk_list_store_append (liststore_network, &treeiter);
        gtk_list_store_set (liststore_network,
                            &treeiter,
                            COLUMN_ID, "ap-other...",
                            /* TRANSLATORS: this is when the access point is not listed
                             * in the dropdown (or hidden) and the user has to select
                             * another entry manually */
                            COLUMN_TITLE, C_("Wireless access point", "Other..."),
                            /* always last */
                            COLUMN_SORT, "",
                            COLUMN_STRENGTH, 0,
                            COLUMN_MODE, NM_802_11_MODE_UNKNOWN,
                            COLUMN_SECURITY, NM_AP_SEC_UNKNOWN,
                            -1);
}

static GPtrArray *
panel_get_strongest_unique_aps (const GPtrArray *aps)
{
        const GByteArray *ssid;
        const GByteArray *ssid_tmp;
        GPtrArray *aps_unique = NULL;
        gboolean add_ap;
        guint i;
        guint j;
        NMAccessPoint *ap;
        NMAccessPoint *ap_tmp;

        /* we will have multiple entries for typical hotspots, just
         * filter to the one with the strongest signal */
        aps_unique = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
        if (aps != NULL)
                for (i = 0; i < aps->len; i++) {
                        ap = NM_ACCESS_POINT (g_ptr_array_index (aps, i));

                        /* Hidden SSIDs don't get shown in the list */
                        ssid = nm_access_point_get_ssid (ap);
                        if (!ssid)
                                continue;

                        add_ap = TRUE;

                        /* get already added list */
                        for (j=0; j<aps_unique->len; j++) {
                                ap_tmp = NM_ACCESS_POINT (g_ptr_array_index (aps_unique, j));
                                ssid_tmp = nm_access_point_get_ssid (ap_tmp);
                                g_assert (ssid_tmp);
        
                                /* is this the same type and data? */
                                if (nm_utils_same_ssid (ssid, ssid_tmp, TRUE)) {

                                        g_debug ("found duplicate: %s",
                                                 nm_utils_escape_ssid (ssid_tmp->data,
                                                                       ssid_tmp->len));

                                        /* the new access point is stronger */
                                        if (nm_access_point_get_strength (ap) >
                                            nm_access_point_get_strength (ap_tmp)) {
                                                g_debug ("removing %s",
                                                         nm_utils_escape_ssid (ssid_tmp->data,
                                                                               ssid_tmp->len));
                                                g_ptr_array_remove (aps_unique, ap_tmp);
                                                add_ap = TRUE;
                                        } else {
                                                add_ap = FALSE;
                                        }

                                        break;
                                }
                        }
                        if (add_ap) {
                                g_debug ("adding %s",
                                         nm_utils_escape_ssid (ssid->data,
                                                               ssid->len));
                                g_ptr_array_add (aps_unique, g_object_ref (ap));
                        }
                }
        return aps_unique;
}

static gchar *
get_ap_security_string (NMAccessPoint *ap)
{
        NM80211ApSecurityFlags wpa_flags, rsn_flags;
        NM80211ApFlags flags;
        GString *str;

        flags = nm_access_point_get_flags (ap);
        wpa_flags = nm_access_point_get_wpa_flags (ap);
        rsn_flags = nm_access_point_get_rsn_flags (ap);

        str = g_string_new ("");
        if ((flags & NM_802_11_AP_FLAGS_PRIVACY) &&
            (wpa_flags == NM_802_11_AP_SEC_NONE) &&
            (rsn_flags == NM_802_11_AP_SEC_NONE)) {
                /* TRANSLATORS: this WEP WiFi security */
                g_string_append_printf (str, "%s, ", _("WEP"));
        }
        if (wpa_flags != NM_802_11_AP_SEC_NONE) {
                /* TRANSLATORS: this WPA WiFi security */
                g_string_append_printf (str, "%s, ", _("WPA"));
        }
        if (rsn_flags != NM_802_11_AP_SEC_NONE) {
                /* TRANSLATORS: this WPA WiFi security */
                g_string_append_printf (str, "%s, ", _("WPA2"));
        }
        if ((wpa_flags & NM_802_11_AP_SEC_KEY_MGMT_802_1X) ||
            (rsn_flags & NM_802_11_AP_SEC_KEY_MGMT_802_1X)) {
                /* TRANSLATORS: this Enterprise WiFi security */
                g_string_append_printf (str, "%s, ", _("Enterprise"));
        }
        if (str->len > 0)
                g_string_set_size (str, str->len - 2);
        else {
                /* TRANSLATORS: this no (!) WiFi security */
                g_string_append (str, _("None"));
        }
        return g_string_free (str, FALSE);
}

static void
wireless_enabled_toggled (NMClient       *client,
                          GParamSpec     *pspec,
                          NetDeviceWifi *device_wifi)
{
        gboolean enabled;
        GtkSwitch *sw;
        NMDevice *device;

        device = net_device_get_nm_device (NET_DEVICE (device_wifi));
        if (nm_device_get_device_type (device) != NM_DEVICE_TYPE_WIFI)
                return;

        enabled = nm_client_wireless_get_enabled (client);
        sw = GTK_SWITCH (gtk_builder_get_object (device_wifi->priv->builder,
                                                 "device_off_switch"));

        device_wifi->priv->updating_device = TRUE;
        gtk_switch_set_active (sw, enabled);
        device_wifi->priv->updating_device = FALSE;
}

#if 0
static void
update_off_switch_from_device_state (GtkSwitch *sw,
                                     NMDeviceState state,
                                     NetDeviceWifi *device_wifi)
{
        device_wifi->priv->updating_device = TRUE;
        switch (state) {
                case NM_DEVICE_STATE_UNMANAGED:
                case NM_DEVICE_STATE_UNAVAILABLE:
                case NM_DEVICE_STATE_DISCONNECTED:
                case NM_DEVICE_STATE_DEACTIVATING:
                case NM_DEVICE_STATE_FAILED:
                        gtk_switch_set_active (sw, FALSE);
                        break;
                default:
                        gtk_switch_set_active (sw, TRUE);
                        break;
        }
        device_wifi->priv->updating_device = FALSE;
}
#endif

static NMConnection *
find_connection_for_device (NetDeviceWifi *device_wifi,
                            NMDevice       *device)
{
        NetDevice *tmp;
        NMConnection *connection;
        NMRemoteSettings *remote_settings;
        NMClient *client;

        client = net_object_get_client (NET_OBJECT (device_wifi));
        remote_settings = net_object_get_remote_settings (NET_OBJECT (device_wifi));
        tmp = g_object_new (NET_TYPE_DEVICE,
                            "client", client,
                            "remote-settings", remote_settings,
                            "nm-device", device,
                            NULL);
        connection = net_device_get_find_connection (tmp);
        g_object_unref (tmp);
        return connection;
}

static gboolean
device_is_hotspot (NetDeviceWifi *device_wifi)
{
        NMConnection *c;
        NMSettingIP4Config *s_ip4;
        NMDevice *device;

        device = net_device_get_nm_device (NET_DEVICE (device_wifi));
        if (nm_device_get_device_type (device) != NM_DEVICE_TYPE_WIFI) {
                return FALSE;
        }

        c = find_connection_for_device (device_wifi, device);
        if (c == NULL) {
                return FALSE;
        }

        s_ip4 = nm_connection_get_setting_ip4_config (c);
        if (g_strcmp0 (nm_setting_ip4_config_get_method (s_ip4),
                       NM_SETTING_IP4_CONFIG_METHOD_SHARED) != 0) {
                return FALSE;
        }

        return TRUE;
}

static const GByteArray *
device_get_hotspot_ssid (NetDeviceWifi *device_wifi,
                         NMDevice *device)
{
        NMConnection *c;
        NMSettingWireless *sw;

        c = find_connection_for_device (device_wifi, device);
        if (c == NULL) {
                return FALSE;
        }

        sw = nm_connection_get_setting_wireless (c);
        return nm_setting_wireless_get_ssid (sw);
}

static void
get_secrets_cb (NMRemoteConnection *c,
                GHashTable         *secrets,
                GError             *error,
                gpointer            data)
{
        NetDeviceWifi *device_wifi = data;
        NMSettingWireless *sw;

        sw = nm_connection_get_setting_wireless (NM_CONNECTION (c));

        nm_connection_update_secrets (NM_CONNECTION (c),
                                      nm_setting_wireless_get_security (sw),
                                      secrets, NULL);

        nm_device_wifi_refresh_ui (device_wifi);
}

static void
device_get_hotspot_security_details (NetDeviceWifi *device_wifi,
                                     NMDevice *device,
                                     gchar **secret,
                                     gchar **security)
{
        NMConnection *c;
        NMSettingWireless *sw;
        NMSettingWirelessSecurity *sws;
        const gchar *key_mgmt;
        const gchar *tmp_secret;
        const gchar *tmp_security;

        c = find_connection_for_device (device_wifi, device);
        if (c == NULL) {
                return;
        }

        sw = nm_connection_get_setting_wireless (c);
        sws = nm_connection_get_setting_wireless_security (c);
        if (sw == NULL || sws == NULL) {
                return;
        }

        tmp_secret = NULL;
        tmp_security = _("None");

        key_mgmt = nm_setting_wireless_security_get_key_mgmt (sws);
        if (strcmp (key_mgmt, "none") == 0) {
                tmp_secret = nm_setting_wireless_security_get_wep_key (sws, 0);
                tmp_security = _("WEP");
        }
        else if (strcmp (key_mgmt, "wpa-none") == 0) {
                tmp_secret = nm_setting_wireless_security_get_psk (sws);
                tmp_security = _("WPA");
        } else {
                g_warning ("unhandled security key-mgmt: %s", key_mgmt);
        }

        /* If we don't have secrets, request them from NM and bail.
         * We'll refresh the UI when secrets arrive.
         */
        if (tmp_secret == NULL) {
                nm_remote_connection_get_secrets ((NMRemoteConnection*)c,
                                                  nm_setting_wireless_get_security (sw),
                                                  get_secrets_cb,
                                                  device_wifi);
                return;
        }

        if (secret) {
                *secret = g_strdup (tmp_secret);
        }

        if (security) {
                *security = g_strdup (tmp_security);
        }
}

static void
nm_device_wifi_refresh_ui (NetDeviceWifi *device_wifi)
{
        char *wid_name;
        const char *str;
        const GPtrArray *aps;
        gboolean can_start_hotspot;
        gboolean is_connected;
        gboolean is_hotspot;
        gchar *hotspot_secret;
        gchar *hotspot_security;
        gchar *hotspot_ssid;
        gchar *str_tmp;
        GPtrArray *aps_unique = NULL;
        GString *string;
        GtkListStore *liststore_network;
        GtkWidget *heading;
        GtkWidget *sw;
        GtkWidget *widget;
        guint i;
        guint speed = 0;
        NMAccessPoint *active_ap;
        NMAccessPoint *ap;
        NMClientPermissionResult perm;
        NMDevice *nm_device;
        NMDeviceState state;
        NMDeviceType type;
        NMClient *client;
        NetDeviceWifiPrivate *priv = device_wifi->priv;

        nm_device = net_device_get_nm_device (NET_DEVICE (device_wifi));
        type = nm_device_get_device_type (nm_device);
        state = nm_device_get_state (nm_device);
        is_hotspot = device_is_hotspot (device_wifi);

        /* set device kind */
        wid_name = g_strdup ("label_device");
        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder, wid_name));
        g_free (wid_name);
        gtk_label_set_label (GTK_LABEL (widget),
                             panel_device_to_localized_string (nm_device));

        /* set up the device on/off switch */
        wid_name = g_strdup ("device_off_switch");
        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder, wid_name));
        g_free (wid_name);

        /* keep this in sync with the signal handler setup in cc_network_panel_init */
        gtk_widget_show (widget);
        client = net_object_get_client (NET_OBJECT (device_wifi));
        wireless_enabled_toggled (client, NULL, device_wifi);
        if (state != NM_DEVICE_STATE_UNAVAILABLE)
                speed = nm_device_wifi_get_bitrate (NM_DEVICE_WIFI (nm_device));
        speed /= 1000;

        /* set device state, with status and optionally speed */
        wid_name = g_strdup ("label_status");
        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder, wid_name));
        g_free (wid_name);
        if (is_hotspot) {
                string = g_string_new (_("Hotspot"));
        } else {
                string = g_string_new (panel_device_state_to_localized_string (nm_device));
        }
        if (speed  > 0) {
                g_string_append (string, " - ");
                /* Translators: network device speed */
                g_string_append_printf (string, _("%d Mb/s"), speed);
        }
        gtk_label_set_label (GTK_LABEL (widget), string->str);
        gtk_widget_set_tooltip_text (widget, panel_device_state_reason_to_localized_string (nm_device));

        /* The options button is always enabled for wired connections,
         * and is sensitive for other connection types if the device
         * is currently connected */
        wid_name = g_strdup ("button_options");
        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder, wid_name));
        g_free (wid_name);
        if (widget != NULL) {
                switch (type) {
                default:
                        is_connected = find_connection_for_device (device_wifi, nm_device) != NULL;
                        gtk_widget_set_sensitive (widget, is_connected);
                        break;
                }
        }
        g_string_free (string, TRUE);

        /* sort out hotspot ui */
        is_hotspot = device_is_hotspot (device_wifi);
        hotspot_ssid = NULL;
        hotspot_secret = NULL;
        hotspot_security = NULL;
        if (is_hotspot) {
                const GByteArray *ssid;
                ssid = device_get_hotspot_ssid (device_wifi, nm_device);
                if (ssid) {
                        hotspot_ssid = nm_utils_ssid_to_utf8 (ssid);
                }
                device_get_hotspot_security_details (device_wifi, nm_device, &hotspot_secret, &hotspot_security);
        }

        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder,
                                                     "start_hotspot_button"));
        gtk_widget_set_visible (widget, !is_hotspot);

        sw = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder,
                                                 "device_off_switch"));
        perm = nm_client_get_permission_result (client, NM_CLIENT_PERMISSION_WIFI_SHARE_OPEN);
        can_start_hotspot = gtk_switch_get_active (GTK_SWITCH (sw)) &&
                            (perm == NM_CLIENT_PERMISSION_RESULT_YES ||
                             perm == NM_CLIENT_PERMISSION_RESULT_AUTH);
        gtk_widget_set_sensitive (widget, can_start_hotspot);

        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder,
                                                     "stop_hotspot_button"));
        gtk_widget_set_visible (widget, is_hotspot);

        panel_set_device_widget_details (device_wifi->priv->builder,
                                         "hotspot_network_name",
                                         hotspot_ssid);
        g_free (hotspot_ssid);

        panel_set_device_widget_details (device_wifi->priv->builder,
                                         "hotspot_security_key",
                                         hotspot_secret);
        g_free (hotspot_secret);

        /* device MAC */
        str = nm_device_wifi_get_hw_address (NM_DEVICE_WIFI (nm_device));
        panel_set_device_widget_details (device_wifi->priv->builder,
                                         "mac",
                                         str);
        /* security */
        active_ap = nm_device_wifi_get_active_access_point (NM_DEVICE_WIFI (nm_device));
        if (state == NM_DEVICE_STATE_UNAVAILABLE)
                str_tmp = NULL;
        else if (is_hotspot)
                str_tmp = hotspot_security;
        else if (active_ap != NULL)
                str_tmp = get_ap_security_string (active_ap);
        else
                str_tmp = g_strdup ("");
        panel_set_device_widget_details (device_wifi->priv->builder,
                                         "security",
                                         str_tmp);
        g_free (str_tmp);

        heading = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder,
                                                      "heading_network_name"));
        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder,
                                                     "combobox_network_name"));
        /* populate access point dropdown */
        if (is_hotspot || state == NM_DEVICE_STATE_UNAVAILABLE) {
                gtk_widget_hide (heading);
                gtk_widget_hide (widget);
        } else {
                gtk_widget_show (heading);
                gtk_widget_show (widget);
                liststore_network = GTK_LIST_STORE (gtk_builder_get_object (device_wifi->priv->builder,
                                                                                     "liststore_network"));
                device_wifi->priv->updating_device = TRUE;
                gtk_list_store_clear (liststore_network);
                aps = nm_device_wifi_get_access_points (NM_DEVICE_WIFI (nm_device));
                aps_unique = panel_get_strongest_unique_aps (aps);

                for (i = 0; i < aps_unique->len; i++) {
                        ap = NM_ACCESS_POINT (g_ptr_array_index (aps_unique, i));
                        add_access_point (device_wifi, ap, active_ap, nm_device);
                }
                add_access_point_other (device_wifi);
                if (active_ap == NULL) {
                        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder,
                                                                     "combobox_network_name"));
                        gtk_combo_box_set_active_iter (GTK_COMBO_BOX (widget), NULL);
                        gtk_entry_set_text (GTK_ENTRY (gtk_bin_get_child (GTK_BIN (widget))), "");
                }

                device_wifi->priv->updating_device = FALSE;

                g_ptr_array_unref (aps_unique);
        }

        /* setup wireless button */
        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder,
                                                     "button_forget"));
        gtk_widget_set_visible (widget, active_ap != NULL);

        /* device MAC */
        str = nm_device_wifi_get_hw_address (NM_DEVICE_WIFI (nm_device));
        panel_set_device_widget_details (priv->builder, "mac", str);

        /* set IP entries */
        panel_set_device_widgets (priv->builder, nm_device);
}

static void
device_wifi_refresh (NetObject *object)
{
        NetDeviceWifi *device_wifi = NET_DEVICE_WIFI (object);
        nm_device_wifi_refresh_ui (device_wifi);
}

static void
device_off_toggled (GtkSwitch *sw,
                    GParamSpec *pspec,
                    NetDeviceWifi *device_wifi)
{
        NMClient *client;
        gboolean active;

        if (device_wifi->priv->updating_device)
                return;

        client = net_object_get_client (NET_OBJECT (device_wifi));
        active = gtk_switch_get_active (sw);
        nm_client_wireless_set_enabled (client, active);
}

static void
forget_network_connection_delete_cb (NMRemoteConnection *connection,
                                     GError *error,
                                     gpointer user_data)
{
        if (error == NULL)
                return;
        g_warning ("failed to delete connection %s: %s",
                   nm_object_get_path (NM_OBJECT (connection)),
                   error->message);
}

static void
forget_network_response_cb (GtkWidget *dialog,
                            gint response,
                            NetDeviceWifi *device_wifi)
{
        NMDevice *device;
        NMRemoteConnection *remote_connection;

        if (response != GTK_RESPONSE_OK)
                goto out;

        device = net_device_get_nm_device (NET_DEVICE (device_wifi));
        if (device == NULL)
                goto out;

        /* delete the connection */
        remote_connection = NM_REMOTE_CONNECTION (find_connection_for_device (device_wifi, device));
        if (remote_connection == NULL) {
                g_warning ("failed to get remote connection");
                goto out;
        }
        nm_remote_connection_delete (remote_connection,
                                     forget_network_connection_delete_cb,
                                     device_wifi);
out:
        gtk_widget_destroy (dialog);
}

static void
forget_button_clicked_cb (GtkButton *button, NetDeviceWifi *device_wifi)
{
        gboolean ret;
        gchar *ssid_pretty = NULL;
        gchar *ssid_target = NULL;
        gchar *warning = NULL;
        GtkComboBox *combobox;
        GtkTreeIter iter;
        GtkTreeModel *model;
        GtkWidget *dialog;
        GtkWidget *window;
        CcNetworkPanel *panel;

        combobox = GTK_COMBO_BOX (gtk_builder_get_object (device_wifi->priv->builder,
                                                          "combobox_network_name"));
        ret = gtk_combo_box_get_active_iter (combobox, &iter);
        if (!ret)
                goto out;

        /* get entry */
        model = gtk_combo_box_get_model (GTK_COMBO_BOX (combobox));
        gtk_tree_model_get (model, &iter,
                            COLUMN_TITLE, &ssid_target,
                            -1);

        ssid_pretty = g_strdup_printf ("<b>%s</b>", ssid_target);
        warning = g_strdup_printf (_("Network details for %s including password and any custom configuration will be lost"), ssid_pretty);
        panel = net_object_get_panel (NET_OBJECT (device_wifi));
        window = gtk_widget_get_toplevel (GTK_WIDGET (panel));
        dialog = gtk_message_dialog_new (GTK_WINDOW (window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_OTHER,
                                         GTK_BUTTONS_NONE,
                                         NULL);
        gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog), warning);
        gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                _("Forget"), GTK_RESPONSE_OK,
                                NULL);
        g_signal_connect (dialog, "response",
                          G_CALLBACK (forget_network_response_cb), device_wifi);
        gtk_window_present (GTK_WINDOW (dialog));
out:
        g_free (ssid_target);
        g_free (ssid_pretty);
        g_free (warning);
}


static void
connect_to_hidden_network (NetDeviceWifi *device_wifi)
{
        NMRemoteSettings *remote_settings;
        NMClient *client;
        CcNetworkPanel *panel;

        remote_settings = net_object_get_remote_settings (NET_OBJECT (device_wifi));
        client = net_object_get_client (NET_OBJECT (device_wifi));
        panel = net_object_get_panel (NET_OBJECT (device_wifi));
        cc_network_panel_connect_to_hidden_network (panel, client, remote_settings);
}

static void
connection_add_activate_cb (NMClient *client,
                            NMActiveConnection *connection,
                            const char *path,
                            GError *error,
                            gpointer user_data)
{
        NetDeviceWifi *device_wifi = user_data;

        if (connection == NULL) {
                /* failed to activate */
                nm_device_wifi_refresh_ui (device_wifi);
        }
}

static void
connection_activate_cb (NMClient *client,
                        NMActiveConnection *connection,
                        GError *error,
                        gpointer user_data)
{
        NetDeviceWifi *device_wifi = user_data;

        if (connection == NULL) {
                /* failed to activate */
                nm_device_wifi_refresh_ui (device_wifi);
        }
}

static void
wireless_ap_changed_cb (GtkComboBox *combo_box, NetDeviceWifi *device_wifi)
{
        const GByteArray *ssid;
        const gchar *ssid_tmp;
        gboolean ret;
        gchar *object_path = NULL;
        gchar *ssid_target = NULL;
        GSList *list, *l;
        GSList *filtered;
        GtkTreeIter iter;
        GtkTreeModel *model;
        NMConnection *connection;
        NMConnection *connection_activate = NULL;
        NMDevice *device;
        NMSettingWireless *setting_wireless;
        NMRemoteSettings *remote_settings;
        NMClient *client;

        if (device_wifi->priv->updating_device)
                goto out;

        ret = gtk_combo_box_get_active_iter (combo_box, &iter);
        if (!ret)
                goto out;

        device = net_device_get_nm_device (NET_DEVICE (device_wifi));
        if (device == NULL)
                goto out;

        /* get entry */
        model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo_box));
        gtk_tree_model_get (model, &iter,
                            COLUMN_ID, &object_path,
                            COLUMN_TITLE, &ssid_target,
                            -1);
        g_debug ("try to connect to WIFI network %s [%s]",
                 ssid_target, object_path);
        if (g_strcmp0 (object_path, "ap-other...") == 0) {
                connect_to_hidden_network (device_wifi);
                goto out;
        }

        /* look for an existing connection we can use */
        remote_settings = net_object_get_remote_settings (NET_OBJECT (device_wifi));
        list = nm_remote_settings_list_connections (remote_settings);
        g_debug ("%i existing remote connections available",
                 g_slist_length (list));
        filtered = nm_device_filter_connections (device, list);
        g_debug ("%i suitable remote connections to check",
                 g_slist_length (filtered));
        for (l = filtered; l; l = g_slist_next (l)) {
                connection = NM_CONNECTION (l->data);
                setting_wireless = nm_connection_get_setting_wireless (connection);
                if (!NM_IS_SETTING_WIRELESS (setting_wireless))
                        continue;
                ssid = nm_setting_wireless_get_ssid (setting_wireless);
                if (ssid == NULL)
                        continue;
                ssid_tmp = nm_utils_escape_ssid (ssid->data, ssid->len);
                if (g_strcmp0 (ssid_target, ssid_tmp) == 0) {
                        g_debug ("we found an existing connection %s to activate!",
                                 nm_connection_get_id (connection));
                        connection_activate = connection;
                        break;
                }
        }

        g_slist_free (list);
        g_slist_free (filtered);

        /* activate the connection */
        client = net_object_get_client (NET_OBJECT (device_wifi));
        if (connection_activate != NULL) {
                nm_client_activate_connection (client,
                                               connection_activate,
                                               device, NULL,
                                               connection_activate_cb, device_wifi);
                goto out;
        }

        /* create one, as it's missing */
        g_debug ("no existing connection found for %s, creating",
                 ssid_target);
        nm_client_add_and_activate_connection (client,
                                               NULL,
                                               device, object_path,
                                               connection_add_activate_cb, device_wifi);
out:
        g_free (ssid_target);
        g_free (object_path);
}

static gint
wireless_ap_model_sort_cb (GtkTreeModel *model,
                           GtkTreeIter *a,
                           GtkTreeIter *b,
                           gpointer user_data)
{
        gchar *str_a;
        gchar *str_b;
        gint strength_a;
        gint strength_b;
        gboolean active_a;
        gboolean active_b;
        gint retval;

        gtk_tree_model_get (model, a,
                            COLUMN_SORT, &str_a,
                            COLUMN_STRENGTH, &strength_a,
                            COLUMN_ACTIVE, &active_a,
                            -1);
        gtk_tree_model_get (model, b,
                            COLUMN_SORT, &str_b,
                            COLUMN_STRENGTH, &strength_b,
                            COLUMN_ACTIVE, &active_b,
                            -1);

        /* special case blank entries to the bottom */
        if (g_strcmp0 (str_a, "") == 0) {
                retval = 1;
                goto out;
        }
        if (g_strcmp0 (str_b, "") == 0) {
                retval = -1;
                goto out;
        }

        if (active_a) {
                retval = -1;
                goto out;
        }

        if (active_b) {
                retval = 1;
                goto out;
        }

        /* case sensitive search like before */
        g_debug ("compare %s (%d) with %s (%d)", str_a, strength_a, str_b, strength_b);
        retval = strength_b - strength_a;

out:
        g_free (str_a);
        g_free (str_b);

        return retval;
}

static GByteArray *
ssid_to_byte_array (const gchar *ssid)
{
        guint32 len;
        GByteArray *ba;

        len = strlen (ssid);
        ba = g_byte_array_sized_new (len);
        g_byte_array_append (ba, (guchar *)ssid, len);

        return ba;
}

static gchar *
get_hostname (void)
{
        GDBusConnection *bus;
        GVariant *res;
        GVariant *inner;
        gchar *str;
        GError *error;

        error = NULL;
        bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (error != NULL) {
                g_warning ("Failed to get system bus connection: %s", error->message);
                g_error_free (error);

                return NULL;
        }
        res = g_dbus_connection_call_sync (bus,
                                           "org.freedesktop.hostname1",
                                           "/org/freedesktop/hostname1",
                                           "org.freedesktop.DBus.Properties",
                                           "Get",
                                           g_variant_new ("(ss)",
                                                          "org.freedesktop.hostname1",
                                                          "PrettyHostname"),
                                           (GVariantType*)"(v)",
                                           G_DBUS_CALL_FLAGS_NONE,
                                           -1,
                                           NULL,
                                           &error);
        g_object_unref (bus);

        if (error != NULL) {
                g_warning ("Getting pretty hostname failed: %s", error->message);
                g_error_free (error);
        }

        str = NULL;

        if (res != NULL) {
                g_variant_get (res, "(v)", &inner);
                str = g_variant_dup_string (inner, NULL);
                g_variant_unref (res);
        }

        if (str == NULL || *str == '\0') {
                str = g_strdup (g_get_host_name ());
        }

        if (str == NULL || *str == '\0') {
                str = g_strdup ("GNOME");
	}

        return str;
}

static GByteArray *
generate_ssid_for_hotspot (NetDeviceWifi *device_wifi)
{
        GByteArray *ssid_array;
        gchar *ssid;

        ssid = get_hostname ();
        ssid_array = ssid_to_byte_array (ssid);
        g_free (ssid);

        return ssid_array;
}

static gchar *
generate_wep_key (NetDeviceWifi *device_wifi)
{
        gchar key[11];
        gint i;
        const gchar *hexdigits = "0123456789abcdef";

        /* generate a 10-digit hex WEP key */
        for (i = 0; i < 10; i++) {
                gint digit;
                digit = g_random_int_range (0, 16);
                key[i] = hexdigits[digit];
        }
        key[10] = 0;

        return g_strdup (key);
}

static gboolean
is_hotspot_connection (NMConnection *connection)
{
        NMSettingConnection *sc;
        NMSettingWireless *sw;
        NMSettingIP4Config *sip;

        sc = nm_connection_get_setting_connection (connection);
        if (g_strcmp0 (nm_setting_connection_get_connection_type (sc), "802-11-wireless") != 0) {
                return FALSE;
        }
        sw = nm_connection_get_setting_wireless (connection);
        if (g_strcmp0 (nm_setting_wireless_get_mode (sw), "adhoc") != 0) {
                return FALSE;
        }
        if (g_strcmp0 (nm_setting_wireless_get_security (sw), "802-11-wireless-security") != 0) {
                return FALSE;
        }
        sip = nm_connection_get_setting_ip4_config (connection);
        if (g_strcmp0 (nm_setting_ip4_config_get_method (sip), "shared") != 0) {
                return FALSE;
        }

        return TRUE;
}

static void
activate_cb (NMClient           *client,
             NMActiveConnection *connection,
             GError             *error,
             NetDeviceWifi     *device_wifi)
{
        if (error) {
                g_warning ("Failed to add new connection: (%d) %s",
                           error->code,
                           error->message);
                return;
        }

        nm_device_wifi_refresh_ui (device_wifi);
}

static void
activate_new_cb (NMClient           *client,
                 NMActiveConnection *connection,
                 const gchar        *path,
                 GError             *error,
                 NetDeviceWifi     *device_wifi)
{
        activate_cb (client, connection, error, device_wifi);
}

static void
start_shared_connection (NetDeviceWifi *device_wifi)
{
        NMConnection *c;
        NMConnection *tmp;
        NMSettingConnection *sc;
        NMSettingWireless *sw;
        NMSettingIP4Config *sip;
        NMSettingWirelessSecurity *sws;
        NMDevice *device;
        GByteArray *ssid_array;
        gchar *wep_key;
        const gchar *str_mac;
        struct ether_addr *bin_mac;
        GSList *connections;
        GSList *filtered;
        GSList *l;
        NMClient *client;
        NMRemoteSettings *remote_settings;

        device = net_device_get_nm_device (NET_DEVICE (device_wifi));
        g_assert (nm_device_get_device_type (device) == NM_DEVICE_TYPE_WIFI);

        remote_settings = net_object_get_remote_settings (NET_OBJECT (device_wifi));
        connections = nm_remote_settings_list_connections (remote_settings);
        filtered = nm_device_filter_connections (device, connections);
        g_slist_free (connections);
        c = NULL;
        for (l = filtered; l; l = l->next) {
                tmp = l->data;
                if (is_hotspot_connection (tmp)) {
                        c = tmp;
                        break;
                }
        }
        g_slist_free (filtered);

        client = net_object_get_client (NET_OBJECT (device_wifi));
        if (c != NULL) {
                g_debug ("activate existing hotspot connection\n");
                nm_client_activate_connection (client,
                                               c,
                                               device,
                                               NULL,
                                               (NMClientActivateFn)activate_cb,
                                               device_wifi);
                return;
        }

        g_debug ("create new hotspot connection\n");
        c = nm_connection_new ();

        sc = (NMSettingConnection *)nm_setting_connection_new ();
        g_object_set (sc,
                      "type", "802-11-wireless",
                      "id", "Hotspot",
                      "autoconnect", FALSE,
                      NULL);
        nm_connection_add_setting (c, (NMSetting *)sc);

        sw = (NMSettingWireless *)nm_setting_wireless_new ();
        g_object_set (sw,
                      "mode", "adhoc",
                      "security", "802-11-wireless-security",
                      NULL);

        str_mac = nm_device_wifi_get_permanent_hw_address (NM_DEVICE_WIFI (device));
        bin_mac = ether_aton (str_mac);
        if (bin_mac) {
                GByteArray *hw_address;

                hw_address = g_byte_array_sized_new (ETH_ALEN);
                g_byte_array_append (hw_address, bin_mac->ether_addr_octet, ETH_ALEN);
                g_object_set (sw,
                              "mac-address", hw_address,
                              NULL);
                g_byte_array_unref (hw_address);
        }
        nm_connection_add_setting (c, (NMSetting *)sw);

        sip = (NMSettingIP4Config*) nm_setting_ip4_config_new ();
        g_object_set (sip, "method", "shared", NULL);
        nm_connection_add_setting (c, (NMSetting *)sip);

        ssid_array = generate_ssid_for_hotspot (device_wifi);
        g_object_set (sw,
                      "ssid", ssid_array,
                      NULL);
        g_byte_array_unref (ssid_array);

        sws = (NMSettingWirelessSecurity*) nm_setting_wireless_security_new ();
        wep_key = generate_wep_key (device_wifi);
        g_object_set (sws,
                      "key-mgmt", "none",
                      "wep-key0", wep_key,
                      "wep-key-type", NM_WEP_KEY_TYPE_KEY,
                      NULL);
        g_free (wep_key);
        nm_connection_add_setting (c, (NMSetting *)sws);

        nm_client_add_and_activate_connection (client,
                                               c,
                                               device,
                                               NULL,
                                               (NMClientAddActivateFn)activate_new_cb,
                                               device_wifi);

        g_object_unref (c);
}

static void
start_hotspot_response_cb (GtkWidget *dialog, gint response, NetDeviceWifi *device_wifi)
{
        if (response == GTK_RESPONSE_OK) {
                start_shared_connection (device_wifi);
        }
        gtk_widget_destroy (dialog);
}

static void
start_hotspot (GtkButton *button, NetDeviceWifi *device_wifi)
{
        NMDevice *device;
        gboolean is_default;
        const GPtrArray *connections;
        const GPtrArray *devices;
        NMActiveConnection *c;
        NMAccessPoint *ap;
        gchar *active_ssid;
        gchar *warning;
        gint i;
        NMClient *client;

        warning = NULL;

        client = net_object_get_client (NET_OBJECT (device_wifi));
        device = net_device_get_nm_device (NET_DEVICE (device_wifi));
        connections = nm_client_get_active_connections (client);
        if (connections == NULL || connections->len == 0) {
                warning = g_strdup_printf ("%s\n\n%s",
                                           _("Not connected to the internet."),
                                           _("Create the hotspot anyway?"));
        } else {
                is_default = FALSE;
                active_ssid = NULL;
                for (i = 0; i < connections->len; i++) {
                        c = (NMActiveConnection *)connections->pdata[i];
                        devices = nm_active_connection_get_devices (c);
                        if (devices && devices->pdata[0] == device) {
                                ap = nm_device_wifi_get_active_access_point (NM_DEVICE_WIFI (device));
                                active_ssid = nm_utils_ssid_to_utf8 (nm_access_point_get_ssid (ap));
                                is_default = nm_active_connection_get_default (c);
                                break;
                        }
                }

                if (active_ssid != NULL) {
                        GString *str;
                        str = g_string_new ("");
                        g_string_append_printf (str, _("Disconnect from %s and create a new hotspot?"), active_ssid);
                        if (is_default) {
                                g_string_append (str, "\n\n");
                                g_string_append (str, _("This is your only connection to the internet."));
                        }
                        warning = g_string_free (str, FALSE);
                }
        }

        if (warning != NULL) {
                GtkWidget *dialog;
                GtkWidget *window;
                CcNetworkPanel *panel;

                panel = net_object_get_panel (NET_OBJECT (device_wifi));
                window = gtk_widget_get_toplevel (GTK_WIDGET (panel));
                dialog = gtk_message_dialog_new (GTK_WINDOW (window),
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 GTK_MESSAGE_OTHER,
                                                 GTK_BUTTONS_NONE,
                                                 "%s", warning);
                gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        _("Create _Hotspot"), GTK_RESPONSE_OK,
                                        NULL);
                g_signal_connect (dialog, "response",
                                  G_CALLBACK (start_hotspot_response_cb), device_wifi);
                gtk_window_present (GTK_WINDOW (dialog));

                return;
          }

        /* if we get here, things look good to go ahead */
        start_shared_connection (device_wifi);
}

static void
stop_shared_connection (NetDeviceWifi *device_wifi)
{
        const GPtrArray *connections;
        const GPtrArray *devices;
        NMDevice *device;
        gint i;
        NMActiveConnection *c;
        NMClient *client;

        device = net_device_get_nm_device (NET_DEVICE (device_wifi));
        client = net_object_get_client (NET_OBJECT (device_wifi));
        connections = nm_client_get_active_connections (client);
        for (i = 0; i < connections->len; i++) {
                c = (NMActiveConnection *)connections->pdata[i];

                devices = nm_active_connection_get_devices (c);
                if (devices && devices->pdata[0] == device) {
                        nm_client_deactivate_connection (client, c);
                        break;
                }
        }

        nm_device_wifi_refresh_ui (device_wifi);
}

static void
stop_hotspot_response_cb (GtkWidget *dialog, gint response, NetDeviceWifi *device_wifi)
{
        if (response == GTK_RESPONSE_OK) {
                stop_shared_connection (device_wifi);
        }
        gtk_widget_destroy (dialog);
}

static void
stop_hotspot (GtkButton *button, NetDeviceWifi *device_wifi)
{
        GtkWidget *dialog;
        GtkWidget *window;
        CcNetworkPanel *panel;

        panel = net_object_get_panel (NET_OBJECT (device_wifi));
        window = gtk_widget_get_toplevel (GTK_WIDGET (panel));
        dialog = gtk_message_dialog_new (GTK_WINDOW (window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_OTHER,
                                         GTK_BUTTONS_NONE,
                                         _("Stop hotspot and disconnect any users?"));
        gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                _("_Stop Hotspot"), GTK_RESPONSE_OK,
                                NULL);
        g_signal_connect (dialog, "response",
                          G_CALLBACK (stop_hotspot_response_cb), device_wifi);
        gtk_window_present (GTK_WINDOW (dialog));
}

static void
edit_connection (GtkButton *button, NetDeviceWifi *device_wifi)
{
        net_object_edit (NET_OBJECT (device_wifi));
}

static void
net_device_wifi_constructed (GObject *object)
{
        NetDeviceWifi *device_wifi = NET_DEVICE_WIFI (object);
        NMClient *client;

        G_OBJECT_CLASS (net_device_wifi_parent_class)->constructed (object);

        client = net_object_get_client (NET_OBJECT (device_wifi));
        g_signal_connect (client, "notify::wireless-enabled",
                          G_CALLBACK (wireless_enabled_toggled), device_wifi);

        nm_device_wifi_refresh_ui (device_wifi);
}

static void
net_device_wifi_finalize (GObject *object)
{
        NetDeviceWifi *device_wifi = NET_DEVICE_WIFI (object);
        NetDeviceWifiPrivate *priv = device_wifi->priv;

        g_object_unref (priv->builder);

        G_OBJECT_CLASS (net_device_wifi_parent_class)->finalize (object);
}

static void
net_device_wifi_class_init (NetDeviceWifiClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        NetObjectClass *parent_class = NET_OBJECT_CLASS (klass);

        object_class->finalize = net_device_wifi_finalize;
        object_class->constructed = net_device_wifi_constructed;
        parent_class->add_to_notebook = device_wifi_proxy_add_to_notebook;
        parent_class->refresh = device_wifi_refresh;
        g_type_class_add_private (klass, sizeof (NetDeviceWifiPrivate));
}

static void
net_device_wifi_init (NetDeviceWifi *device_wifi)
{
        GError *error = NULL;
        GtkWidget *widget;
        GtkCellRenderer *renderer;
        GtkComboBox *combobox;
        GtkTreeSortable *sortable;

        device_wifi->priv = NET_DEVICE_WIFI_GET_PRIVATE (device_wifi);

        device_wifi->priv->builder = gtk_builder_new ();
        gtk_builder_add_from_file (device_wifi->priv->builder,
                                   GNOMECC_UI_DIR "/network-wifi.ui",
                                   &error);
        if (error != NULL) {
                g_warning ("Could not load interface file: %s", error->message);
                g_error_free (error);
                return;
        }

        /* setup wifi combobox model */
        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder,
                                                     "device_off_switch"));
        g_signal_connect (widget, "notify::active",
                          G_CALLBACK (device_off_toggled), device_wifi);

        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder,
                                                     "button_options"));
        g_signal_connect (widget, "clicked",
                          G_CALLBACK (edit_connection), device_wifi);

        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder,
                                                     "button_forget"));
        g_signal_connect (widget, "clicked",
                          G_CALLBACK (forget_button_clicked_cb), device_wifi);

        /* setup wireless combobox model */
        combobox = GTK_COMBO_BOX (gtk_builder_get_object (device_wifi->priv->builder,
                                                          "combobox_network_name"));
        g_signal_connect (combobox, "changed",
                          G_CALLBACK (wireless_ap_changed_cb),
                          device_wifi);

        /* sort networks in drop down */
        sortable = GTK_TREE_SORTABLE (gtk_builder_get_object (device_wifi->priv->builder,
                                                              "liststore_network"));
        gtk_tree_sortable_set_sort_column_id (sortable,
                                              COLUMN_SORT,
                                              GTK_SORT_ASCENDING);
        gtk_tree_sortable_set_sort_func (sortable,
                                         COLUMN_SORT,
                                         wireless_ap_model_sort_cb,
                                         device_wifi,
                                         NULL);

        renderer = panel_cell_renderer_mode_new ();
        gtk_cell_renderer_set_padding (renderer, 4, 0);
        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combobox),
                                    renderer,
                                    FALSE);
        gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combobox), renderer,
                                        "mode", COLUMN_MODE,
                                        NULL);

        renderer = panel_cell_renderer_signal_new ();
        gtk_cell_renderer_set_padding (renderer, 4, 0);
        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combobox),
                                    renderer,
                                    FALSE);
        gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combobox), renderer,
                                        "signal", COLUMN_STRENGTH,
                                        NULL);

        renderer = panel_cell_renderer_security_new ();
        gtk_cell_renderer_set_padding (renderer, 4, 0);
        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combobox),
                                    renderer,
                                    FALSE);
        gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combobox), renderer,
                                        "security", COLUMN_SECURITY,
                                        NULL);

        /* setup view */
        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder,
                                                     "notebook_view"));
        gtk_notebook_set_show_tabs (GTK_NOTEBOOK (widget), FALSE);
        gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), 1);

        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder,
                                                     "start_hotspot_button"));
        g_signal_connect (widget, "clicked",
                          G_CALLBACK (start_hotspot), device_wifi);
        widget = GTK_WIDGET (gtk_builder_get_object (device_wifi->priv->builder,
                                                     "stop_hotspot_button"));
        g_signal_connect (widget, "clicked",
                          G_CALLBACK (stop_hotspot), device_wifi);

}
