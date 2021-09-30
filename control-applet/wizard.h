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
};

struct wizard_data {
	GtkWidget *assistant;
	GConfClient *gconf;

	const gchar *config_name;
	GtkWidget *name_entry;

	gint local_page;
	gint peers_page;

	const gchar *private_key;
	const gchar *address;
	const gchar *dns_address;
	GtkWidget *privkey_entry;
	GtkWidget *pubkey_entry;
	GtkWidget *addr_entry;
	GtkWidget *dnsaddr_entry;

	GtkWidget *peers_chk;
	gboolean has_peers;

	GPtrArray *peers;
	gint peer_idx;

	GtkWidget *p_pubkey_entry;
	GtkWidget *p_psk_entry;
	GtkWidget *p_endpoint_entry;
	GtkWidget *p_ips_entry;

	GtkWidget *p_save_btn;
	GtkWidget *p_del_btn;
	GtkWidget *p_prev_btn;
	GtkWidget *p_next_btn;
};

void start_new_wizard(gpointer config_data);

#endif
