/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2009  Bastien Nocera <hadess@hadess.net>
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>

#include <gtk/gtk.h>
#include <bluetooth-plugin.h>
#include <gio/gio.h>

#include "bluetooth-client.h"

/* http://cgit.freedesktop.org/geoclue/tree/src/org.freedesktop.Geoclue.gschema.xml */
#define GPS_SCHEMA "org.freedesktop.Geoclue"
#define GPS_KEY "gps-device"

static gboolean
gsettings_schema_exists(const char *schema)
{
	const char * const *schemas;
	gboolean schema_exists = FALSE;
	gint i;

	schemas = g_settings_list_schemas ();
	for (i = 0; schemas[i] != NULL; i++) {
		if (g_strcmp0 (schemas[i], schema) == 0) {
			schema_exists = TRUE;
			break;
		}
	}

	return schema_exists;
}

static char *
get_name_and_type (const char *address, BluetoothType *ret_type)
{
	BluetoothClient *client;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean cont;
	char *found_name;

	found_name = NULL;
	*ret_type = 0;
	client = bluetooth_client_new (); 
	model = bluetooth_client_get_device_model (client, NULL);
	if (model == NULL) {
		g_object_unref (client);
		return NULL;
	}

	cont = gtk_tree_model_get_iter_first(model, &iter);
	while (cont != FALSE) {
		char *bdaddr, *name;
		BluetoothType type;

		gtk_tree_model_get(model, &iter,
				   BLUETOOTH_COLUMN_ADDRESS, &bdaddr,
				   BLUETOOTH_COLUMN_ALIAS, &name,
				   BLUETOOTH_COLUMN_TYPE, &type,
				   -1);
		if (g_strcmp0 (bdaddr, address) == 0) {
			g_free (bdaddr);
			found_name = name;
			*ret_type = type;
			break;
		}
		g_free (bdaddr);
		g_free (name);

		cont = gtk_tree_model_iter_next(model, &iter);
	}

	g_object_unref (model);
	g_object_unref (client);

	return found_name;
}

static gboolean
has_config_widget (const char *bdaddr, const char **uuids)
{
	gboolean has_sp = FALSE;
	BluetoothType type;
	char *name;
	guint i;

	if (uuids == NULL)
		return FALSE;
	for (i = 0; uuids[i] != NULL; i++) {
		if (g_str_equal (uuids[i], "SerialPort")) {
			has_sp = TRUE;
			break;
		}
	}
	if (has_sp == FALSE)
		return FALSE;

	/* Just SP? Probably a GPS */
	if (g_strv_length ((char **) uuids) == 1)
		return TRUE;

	/* Type is ANY, probably a GPS device */
	name = get_name_and_type (bdaddr, &type);
	if (name != NULL && type == BLUETOOTH_TYPE_ANY) {
		g_free (name);
		return TRUE;
	}

	if (name == NULL)
		return FALSE;
	/* GPS in the name? */
	if (strstr (name, "GPS") != NULL) {
		g_free (name);
		return TRUE;
	}

	g_free (name);
	return FALSE;
}

static void
toggle_button (GtkToggleButton *button, gpointer user_data)
{
	gboolean state;
	GSettings *settings;
	const char *bdaddr;

	settings = g_object_get_data (G_OBJECT (button), "settings");
	bdaddr = g_object_get_data (G_OBJECT (button), "bdaddr");

	state = gtk_toggle_button_get_active (button);
	if (state == FALSE) {
		const char *old_bdaddr;
		old_bdaddr = g_object_get_data (G_OBJECT (button), "old-bdaddr");
		g_settings_set_string (settings, GPS_KEY, old_bdaddr ? old_bdaddr : "");
	} else {
		char *old_bdaddr;

		old_bdaddr = g_settings_get_string (settings, GPS_KEY);
		g_object_set_data_full (G_OBJECT (button), "old-bdaddr",
					old_bdaddr, g_free);
		g_settings_set_string (settings, GPS_KEY, bdaddr);
	}
}

static GtkWidget *
get_config_widgets (const char *bdaddr, const char **uuids)
{
	GtkWidget *button;
	GSettings *settings = NULL;
	char *old_bdaddr;

	if (gsettings_schema_exists (GPS_SCHEMA))
		settings = g_settings_new (GPS_SCHEMA);
	if (settings == NULL)
		return NULL;

	button = gtk_check_button_new_with_label (_("Use this GPS device for Geolocation services"));
	g_object_set_data_full (G_OBJECT (button), "bdaddr", g_strdup (bdaddr), g_free);
	g_object_set_data_full (G_OBJECT (button), "settings", settings, g_object_unref);

	/* Is it already setup? */
	old_bdaddr = g_settings_get_string (settings, GPS_KEY);
	if (g_strcmp0 (old_bdaddr, bdaddr) == 0) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
		g_object_set_data (G_OBJECT (button), "bdaddr", old_bdaddr);
	} else {
		g_free (old_bdaddr);
	}

	/* And set the signal */
	g_signal_connect (G_OBJECT (button), "toggled",
			  G_CALLBACK (toggle_button), NULL);

	return button;
}

static void
device_removed (const char *bdaddr)
{
	GSettings *settings = NULL;
	char *str;

	if (gsettings_schema_exists (GPS_SCHEMA))
		settings = g_settings_new (GPS_SCHEMA);
	if (settings == NULL)
		return;

	str = g_settings_get_string (settings, GPS_KEY);
	if (g_strcmp0 (str, bdaddr) == 0) {
		g_settings_set_string (settings, GPS_KEY, "");
		g_message ("Device '%s' got disabled as a Geoclue GPS", bdaddr);
	}

	g_free (str);
	g_object_unref (settings);
}

static GbtPluginInfo plugin_info = {
	"geoclue",
	has_config_widget,
	get_config_widgets,
	device_removed
};

GBT_INIT_PLUGIN(plugin_info)

