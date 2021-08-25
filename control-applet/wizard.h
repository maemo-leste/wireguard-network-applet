/*
 * Copyright (c) 2021 Ivan J. <parazyd@dyne.org>
 *
 * This file is part of tor-applet
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
#ifndef __WIZARD_H__
#define __WIZARD_H__

enum wizard_button {
	WIZARD_BUTTON_FINISH = 0,
	WIZARD_BUTTON_PREVIOUS = 1,
	WIZARD_BUTTON_NEXT = 2,
	WIZARD_BUTTON_CLOSE = 3,
	WIZARD_BUTTON_ADVANCED = 4
};

struct wg_peer {
	gchar *public_key;
	gchar *preshared_key;
	gchar *endpoint;
	gchar *allowed_ips;
	GtkWidget *public_key_entry;
	GtkWidget *preshared_key_entry;
	GtkWidget *endpoint_entry;
	GtkWidget *allowed_ips_entry;
};

struct wizard_data {
	GtkWidget *assistant;

	const gchar *config_name;
	GtkWidget *name_entry;

	GtkWidget *transproxy_chk;
	gboolean transproxy_enabled;

	gint local_page;
	gint peers_page;

	GtkWidget *privkey_entry;
	GtkWidget *pubkey_entry;
	GtkWidget *addr_entry;
	GtkWidget *dnsaddr_entry;

	GtkWidget *peers_chk;

	struct wg_peer *peers[];
};

void start_new_wizard(gpointer config_data);

#endif
