/*
 * Copyright (c) 2020-2021 Ivan Jelincic <parazyd@dyne.org>
 *
 * This file is part of wireguard-network-applet
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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <dbus/dbus-glib-lowlevel.h>
#include <gconf/gconf-client.h>
#include <gtk/gtk.h>
#include <hildon/hildon.h>
#include <libhildondesktop/libhildondesktop.h>
#include <libosso.h>
#include <icd/wireguard/libicd_wireguard_shared.h>

/* Use this for debugging */
#include <syslog.h>
#define status_debug(...) syslog(1, __VA_ARGS__)

#define STATUS_APPLET_WIREGUARD_TYPE (status_applet_wireguard_get_type())
#define STATUS_APPLET_WIREGUARD(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
		STATUS_APPLET_WIREGUARD_TYPE, StatusAppletWireguard))

#define SETTINGS_RESPONSE -69

#define DBUS_SIGNAL "type='signal',interface='"ICD_WIREGUARD_DBUS_INTERFACE"',member='"ICD_WIREGUARD_SIGNAL_STATUSCHANGED"'"

typedef struct _StatusAppletWireguard StatusAppletWireguard;
typedef struct _StatusAppletWireguardClass StatusAppletWireguardClass;
typedef struct _StatusAppletWireguardPrivate StatusAppletWireguardPrivate;

typedef enum {
	WIREGUARD_NOT_CONNECTED = 0,
	WIREGUARD_CONNECTING = 1,
	WIREGUARD_CONNECTED = 2,
} WireguardConnState;

typedef enum {
	STATUS_ICON_NONE,
	STATUS_ICON_CONNECTING,
	STATUS_ICON_CONNECTED,
} CurStatusIcon;

struct _StatusAppletWireguard {
	HDStatusMenuItem parent;
	StatusAppletWireguardPrivate *priv;
};

struct _StatusAppletWireguardClass {
	HDStatusMenuItemClass parent;
};

struct _StatusAppletWireguardPrivate {
	osso_context_t *osso;
	DBusConnection *dbus;

	gchar *active_config;
	GtkWidget *menu_button;

	WireguardConnState connection_state;

	gboolean systemwide_enabled;
	gboolean provider_connected;

	GtkWidget *settings_dialog;
	GtkWidget *wg_chkbtn;
	GtkWidget *config_btn;
	GtkWidget *touch_selector;

	GdkPixbuf *pix18_wg_connected;
	GdkPixbuf *pix18_wg_connecting;
	GdkPixbuf *pix48_wg_disabled;
	GdkPixbuf *pix48_wg_enabled;

	CurStatusIcon current_status_icon;
};

HD_DEFINE_PLUGIN_MODULE_WITH_PRIVATE(StatusAppletWireguard,
				     status_applet_wireguard,
				     HD_TYPE_STATUS_MENU_ITEM);
#define GET_PRIVATE(x) status_applet_wireguard_get_instance_private(x)

static void save_settings(StatusAppletWireguard * self)
{
	StatusAppletWireguardPrivate *p = GET_PRIVATE(self);
	GConfClient *gconf = gconf_client_get_default();
	gboolean new_systemwide_enabled;
	gchar *saved_config;

	saved_config =
	    gconf_client_get_string(gconf, GC_WIREGUARD_ACTIVE, NULL);
	if (saved_config == NULL)
		goto out;

	p->active_config =
	    hildon_touch_selector_get_current_text(HILDON_TOUCH_SELECTOR
						   (p->touch_selector));

	if (g_strcmp0(saved_config, p->active_config))
		gconf_client_set_string(gconf, GC_WIREGUARD_ACTIVE,
					p->active_config, NULL);

	new_systemwide_enabled =
	    hildon_check_button_get_active(HILDON_CHECK_BUTTON(p->wg_chkbtn));

	if (p->systemwide_enabled != new_systemwide_enabled) {
		gconf_client_set_bool(gconf, GC_WIREGUARD_SYSTEM,
				      new_systemwide_enabled, NULL);
		p->systemwide_enabled = new_systemwide_enabled;
	}

 out:
	g_object_unref(gconf);
}

static void execute_cp_plugin(GtkWidget * btn, StatusAppletWireguard * self)
{
	StatusAppletWireguardPrivate *p = GET_PRIVATE(self);
	GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(self));
	gtk_widget_hide(toplevel);

	if (osso_cp_plugin_execute
	    (p->osso, "control-applet-wireguard.so", self, TRUE)
	    == OSSO_ERROR) {
		hildon_banner_show_information(NULL, NULL,
					       "Failed to show Wireguard settings");
	}
}

static void set_buttons_sensitivity(StatusAppletWireguard * obj, gboolean state)
{
	StatusAppletWireguardPrivate *p = GET_PRIVATE(obj);

	if (p->wg_chkbtn && p->config_btn) {
		gtk_widget_set_sensitive(p->wg_chkbtn, state);
		gtk_widget_set_sensitive(p->config_btn, state);
	}
}

static void status_menu_clicked_cb(GtkWidget * btn,
				   StatusAppletWireguard * self)
{
	StatusAppletWireguardPrivate *p = GET_PRIVATE(self);
	GtkWidget *toplevel = gtk_widget_get_toplevel(btn);
	GConfClient *gconf = gconf_client_get_default();
	GtkSizeGroup *size_group;

	gtk_widget_hide(toplevel);

	p->settings_dialog = hildon_dialog_new_with_buttons("Wireguard",
							    GTK_WINDOW
							    (toplevel),
							    GTK_DIALOG_MODAL |
							    GTK_DIALOG_DESTROY_WITH_PARENT,
							    "Settings",
							    SETTINGS_RESPONSE,
							    GTK_STOCK_SAVE,
							    GTK_RESPONSE_ACCEPT,
							    NULL);

	p->wg_chkbtn =
	    hildon_check_button_new(HILDON_SIZE_FINGER_HEIGHT |
				    HILDON_SIZE_AUTO_WIDTH);

	gtk_button_set_label(GTK_BUTTON(p->wg_chkbtn),
			     "Enable system-wide tunneling");

	size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
	p->touch_selector = hildon_touch_selector_new_text();

	p->config_btn = hildon_picker_button_new(HILDON_SIZE_FINGER_HEIGHT, 0);
	hildon_picker_button_set_selector(HILDON_PICKER_BUTTON(p->config_btn),
					  HILDON_TOUCH_SELECTOR
					  (p->touch_selector));
	hildon_button_set_title(HILDON_BUTTON(p->config_btn),
				"Current configuration");
	hildon_button_set_alignment(HILDON_BUTTON(p->config_btn), 0.0, 0.5, 1.0,
				    1.0);

	/* Fill the selector with available configs */
	hildon_check_button_set_active(HILDON_CHECK_BUTTON(p->wg_chkbtn),
				       gconf_client_get_bool(gconf,
							     GC_WIREGUARD_SYSTEM,
							     NULL));

	GSList *configs, *iter;
	configs = gconf_client_all_dirs(gconf, GC_WIREGUARD, NULL);

	/* Counter for figuring out the active config */
	int i = -1;
	for (iter = configs; iter; iter = iter->next) {
		i++;
		hildon_touch_selector_append_text(HILDON_TOUCH_SELECTOR
						  (p->touch_selector),
						  g_path_get_basename
						  (iter->data));

		if (!strcmp(g_path_get_basename(iter->data), p->active_config))
			hildon_touch_selector_set_active(HILDON_TOUCH_SELECTOR
							 (p->touch_selector), 0,
							 i);

		g_free(iter->data);
	}
	g_slist_free(iter);
	g_slist_free(configs);

	hildon_button_add_title_size_group(HILDON_BUTTON(p->config_btn),
					   size_group);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(p->settings_dialog)->vbox),
			   p->wg_chkbtn, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(p->settings_dialog)->vbox),
			   p->config_btn, TRUE, TRUE, 0);

	/* Make the buttons insensitive when provider is connected. */
	set_buttons_sensitivity(self, !p->provider_connected);

	gtk_widget_show_all(p->settings_dialog);
	switch (gtk_dialog_run(GTK_DIALOG(p->settings_dialog))) {
	case GTK_RESPONSE_ACCEPT:
		save_settings(self);
		break;
	case SETTINGS_RESPONSE:
		execute_cp_plugin(btn, self);
	default:
		break;
	}

	g_object_unref(gconf);
	gtk_widget_hide_all(p->settings_dialog);
	gtk_widget_destroy(p->settings_dialog);

	p->wg_chkbtn = NULL;
	p->config_btn = NULL;
}

static void set_status_icon(gpointer obj, GdkPixbuf * pixbuf)
{
	hd_status_plugin_item_set_status_area_icon(HD_STATUS_PLUGIN_ITEM(obj),
						   pixbuf);
}

static int blink_status_icon(gpointer obj)
{
	StatusAppletWireguard *sa = STATUS_APPLET_WIREGUARD(obj);
	StatusAppletWireguardPrivate *p = GET_PRIVATE(sa);

	if (p->connection_state != WIREGUARD_CONNECTING)
		return FALSE;

	switch (p->current_status_icon) {
	case STATUS_ICON_NONE:
	case STATUS_ICON_CONNECTED:
		set_status_icon(obj, p->pix18_wg_connecting);
		p->current_status_icon = STATUS_ICON_CONNECTING;
		break;
	case STATUS_ICON_CONNECTING:
		set_status_icon(obj, p->pix18_wg_connected);
		p->current_status_icon = STATUS_ICON_CONNECTED;
		break;
	}

	return TRUE;
}

static void status_applet_wireguard_set_icons(StatusAppletWireguard * self)
{
	StatusAppletWireguard *sa = STATUS_APPLET_WIREGUARD(self);
	StatusAppletWireguardPrivate *p = GET_PRIVATE(sa);
	GdkPixbuf *menu_pixbuf = NULL;

	switch (p->connection_state) {
	case WIREGUARD_NOT_CONNECTED:
		menu_pixbuf = p->pix48_wg_disabled;
		hildon_button_set_value(HILDON_BUTTON(p->menu_button),
					"Disconnected");
		set_status_icon(self, NULL);
		p->current_status_icon = STATUS_ICON_NONE;
		break;
	case WIREGUARD_CONNECTING:
		hildon_button_set_value(HILDON_BUTTON(p->menu_button),
					"Connecting");
		set_status_icon(self, p->pix18_wg_connecting);
		p->current_status_icon = STATUS_ICON_CONNECTING;
		g_timeout_add_seconds(1, blink_status_icon, sa);
		break;
	case WIREGUARD_CONNECTED:
		menu_pixbuf = p->pix48_wg_enabled;
		hildon_button_set_value(HILDON_BUTTON(p->menu_button),
					"Connected");
		set_status_icon(self, p->pix18_wg_connected);
		p->current_status_icon = STATUS_ICON_CONNECTED;
		break;
	default:
		g_critical("%s: Invalid connection_state", G_STRLOC);
		break;
	};

	if (menu_pixbuf) {
		hildon_button_set_image(HILDON_BUTTON(p->menu_button),
					gtk_image_new_from_pixbuf(menu_pixbuf));
		hildon_button_set_image_position(HILDON_BUTTON(p->menu_button),
						 0);
	}
}

static int handle_running(gpointer obj, DBusMessage * msg)
{
	/* Either show or hide status icon */
	StatusAppletWireguardPrivate *p = GET_PRIVATE(obj);
	const gchar *status = NULL;
	const gchar *mode = NULL;

	dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &status,
			      DBUS_TYPE_STRING, &mode, DBUS_TYPE_INVALID);

	if (!g_strcmp0(status, ICD_WIREGUARD_SIGNALS_STATUS_STATE_CONNECTED))
		p->connection_state = WIREGUARD_CONNECTED;
	else if (!g_strcmp0(status, ICD_WIREGUARD_SIGNALS_STATUS_STATE_STARTED))
		p->connection_state = WIREGUARD_CONNECTING;
	else if (!g_strcmp0(status, ICD_WIREGUARD_SIGNALS_STATUS_STATE_STOPPED))
		p->connection_state = WIREGUARD_NOT_CONNECTED;
	else
		p->connection_state = WIREGUARD_NOT_CONNECTED;

	if (!g_strcmp0(mode, ICD_WIREGUARD_SIGNALS_STATUS_MODE_PROVIDER)) {
		p->provider_connected = TRUE;
		set_buttons_sensitivity(obj, FALSE);
	} else {
		p->provider_connected = FALSE;
		set_buttons_sensitivity(obj, TRUE);
	}

	status_applet_wireguard_set_icons(obj);

	return 0;
}

static int on_icd_signal(DBusConnection * dbus, DBusMessage * msg, gpointer obj)
{
	(void)dbus;

	if (dbus_message_is_signal
	    (msg, ICD_WIREGUARD_DBUS_INTERFACE,
	     ICD_WIREGUARD_SIGNAL_STATUSCHANGED))
		return handle_running(obj, msg);

	return 1;
}

static void setup_dbus_matching(StatusAppletWireguard * self)
{
	StatusAppletWireguardPrivate *p = GET_PRIVATE(self);

	p->dbus =
	    hd_status_plugin_item_get_dbus_connection(HD_STATUS_PLUGIN_ITEM
						      (self), DBUS_BUS_SYSTEM,
						      NULL);

	dbus_connection_setup_with_g_main(p->dbus, NULL);

	if (p->dbus)
		dbus_bus_add_match(p->dbus, DBUS_SIGNAL, NULL);

	if (!dbus_connection_add_filter
	    (p->dbus, (DBusHandleMessageFunction) on_icd_signal, self, NULL)) {
		status_debug("wg-sb: Failed to add dbus filter");
		return;
	}
}

static void get_provider_status(StatusAppletWireguard * self)
{
	StatusAppletWireguardPrivate *p = GET_PRIVATE(self);
	DBusMessage *msg;
	DBusMessageIter args;
	DBusPendingCall *pending;
	const gchar *status, *mode;

	msg = dbus_message_new_method_call(ICD_WIREGUARD_DBUS_INTERFACE,
					   ICD_WIREGUARD_DBUS_PATH,
					   ICD_WIREGUARD_METHOD_GETSTATUS,
					   "GetStatus");

	if (msg == NULL) {
		status_debug("wg-sb: %s: msg == NULL", G_STRFUNC);
		goto noprovider;
	}

	if (!dbus_connection_send_with_reply(p->dbus, msg, &pending, -1)) {
		status_debug("wg-sb: OOM at %s:%s", G_STRFUNC, G_STRLOC);
		goto noprovider;
	}

	if (pending == NULL) {
		status_debug("wg-sb: %s: pending == NULL", G_STRFUNC);
		goto noprovider;
	}

	dbus_connection_flush(p->dbus);
	dbus_message_unref(msg);

	dbus_pending_call_block(pending);
	msg = dbus_pending_call_steal_reply(pending);
	dbus_pending_call_unref(pending);

	if (msg == NULL) {
		status_debug("wg-sb: %s: method reply is NULL", G_STRFUNC);
		goto noprovider;
	}

	if (!dbus_message_iter_init(msg, &args)) {
		status_debug("wg-sb: %s: reply has no arguments", G_STRFUNC);
		dbus_message_unref(msg);
		goto noprovider;
	}

	dbus_message_iter_get_basic(&args, &status);

	if (!dbus_message_iter_next(&args)) {
		status_debug("wg-sb: %s: reply has too few arguments",
			     G_STRFUNC);
		dbus_message_unref(msg);
		goto noprovider;
	}

	dbus_message_iter_get_basic(&args, &mode);
	dbus_message_unref(msg);

	if (!g_strcmp0(ICD_WIREGUARD_SIGNALS_STATUS_MODE_PROVIDER, mode))
		p->provider_connected = TRUE;
	else
		p->provider_connected = FALSE;

	if (!g_strcmp0(ICD_WIREGUARD_SIGNALS_STATUS_STATE_CONNECTED, status)) {
		p->connection_state = WIREGUARD_CONNECTED;
		p->current_status_icon = STATUS_ICON_CONNECTED;
	} else
	    if (!g_strcmp0(ICD_WIREGUARD_SIGNALS_STATUS_STATE_STARTED, status))
	{
		p->connection_state = WIREGUARD_CONNECTING;
		p->current_status_icon = STATUS_ICON_CONNECTING;
	} else {
		/* ICD_WIREGUARD_SIGNALS_STATUS_STATE_STOPPED */
		p->connection_state = WIREGUARD_NOT_CONNECTED;
		p->current_status_icon = STATUS_ICON_NONE;
	}

	return;

 noprovider:
	p->connection_state = WIREGUARD_NOT_CONNECTED;
	p->provider_connected = FALSE;
	p->current_status_icon = STATUS_ICON_NONE;
}

static void status_applet_wireguard_init(StatusAppletWireguard * self)
{
	StatusAppletWireguard *sa = STATUS_APPLET_WIREGUARD(self);
	StatusAppletWireguardPrivate *p = GET_PRIVATE(sa);
	DBusError err;
	GConfClient *gconf;
	GtkIconTheme *theme;

	p->osso = osso_initialize("wg-sb", VERSION, FALSE, NULL);

	dbus_error_init(&err);

	/* Dbus setup for icd provider */
	setup_dbus_matching(self);

	/* Check if we're connected to a provider */
	get_provider_status(self);

	/* Get current config; make sure to keep this up to date */
	gconf = gconf_client_get_default();

	p->active_config =
	    gconf_client_get_string(gconf, GC_WIREGUARD_ACTIVE, NULL);
	p->systemwide_enabled =
	    gconf_client_get_bool(gconf, GC_WIREGUARD_SYSTEM, NULL);

	g_object_unref(gconf);

	if (p->active_config == NULL)
		p->active_config = "Default";

	/* Icons */
	theme = gtk_icon_theme_get_default();
	p->pix18_wg_connected =
	    gtk_icon_theme_load_icon(theme, "statusarea_wireguard_connected",
				     18, 0, NULL);
	p->pix18_wg_connecting =
	    gtk_icon_theme_load_icon(theme, "statusarea_wireguard_connecting",
				     18, 0, NULL);
	p->pix48_wg_disabled =
	    gtk_icon_theme_load_icon(theme, "statusarea_wireguard_disabled", 48,
				     0, NULL);
	p->pix48_wg_enabled =
	    gtk_icon_theme_load_icon(theme, "statusarea_wireguard_enabled", 48,
				     0, NULL);

	/* Gtk items */
	p->menu_button =
	    hildon_button_new_with_text(HILDON_SIZE_FINGER_HEIGHT,
					HILDON_BUTTON_ARRANGEMENT_VERTICAL,
					"Wireguard", NULL);
	hildon_button_set_alignment(HILDON_BUTTON(p->menu_button),
				    0.0, 0.5, 0.0, 0.0);
	hildon_button_set_style(HILDON_BUTTON(p->menu_button),
				HILDON_BUTTON_STYLE_PICKER);

	status_applet_wireguard_set_icons(sa);

	g_signal_connect(p->menu_button, "clicked",
			 G_CALLBACK(status_menu_clicked_cb), self);

	gtk_container_add(GTK_CONTAINER(sa), p->menu_button);
	gtk_widget_show_all(GTK_WIDGET(sa));
}

static void status_applet_wireguard_finalize(GObject * obj)
{
	StatusAppletWireguard *sa = STATUS_APPLET_WIREGUARD(obj);
	StatusAppletWireguardPrivate *p = GET_PRIVATE(sa);

	if (p->dbus) {
		dbus_bus_remove_match(p->dbus, DBUS_SIGNAL, NULL);
		dbus_connection_remove_filter(p->dbus,
					      (DBusHandleMessageFunction)
					      on_icd_signal, sa);
		dbus_connection_unref(p->dbus);
		p->dbus = NULL;
	}

	if (p->osso)
		osso_deinitialize(p->osso);

	G_OBJECT_CLASS(status_applet_wireguard_parent_class)->finalize(obj);
}

static void status_applet_wireguard_class_init(StatusAppletWireguardClass *
					       klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = status_applet_wireguard_finalize;
}

static void status_applet_wireguard_class_finalize(StatusAppletWireguardClass *
						   klass)
{
	(void)klass;
}

StatusAppletWireguard *status_applet_wireguard_new(void)
{
	return g_object_new(STATUS_APPLET_WIREGUARD_TYPE, NULL);
}
