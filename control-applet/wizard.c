/*
 * Copyright (c) 2021 Ivan J. <parazyd@dyne.org>
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
#include <errno.h>

#include <gconf/gconf-client.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gprintf.h>

#include <hildon/hildon.h>
#include <hildon-cp-plugin/hildon-cp-plugin-interface.h>

#include <icd/wireguard/libicd_wireguard_shared.h>
#include "pipeutil.h"
#include "wizard.h"

static void free_peer(gpointer elem, gpointer data)
{
	(void)data;
	struct wg_peer *peer = elem;
	g_free(peer->public_key);
	g_free(peer->preshared_key);
	g_free(peer->endpoint);
	g_free(peer->allowed_ips);
}

static void on_assistant_close_cancel_wg(GtkWidget * widget, gpointer data)
{
	g_message("%s", G_STRFUNC);
	(void)widget;
	struct wizard_data *w_data = data;

	if (w_data->peers != NULL) {
		g_ptr_array_foreach(w_data->peers, free_peer, NULL);
		g_ptr_array_unref(w_data->peers);
	}

	GtkWidget **assistant = (GtkWidget **) data;
	gtk_widget_destroy(*assistant);
	*assistant = NULL;

	gtk_main_quit();
}

static gint find_next_wizard_page(gint cur_page, gpointer data)
{
	struct wizard_data *w_data = data;

	if (cur_page == 0) {
		if (w_data->local_page > 0)
			return w_data->local_page;

	}

	if (cur_page == w_data->local_page) {
		if (w_data->peers_page > 0)
			return w_data->peers_page;
	}

	return -1;
}

static void gconf_set_string(GConfClient * gconf, const gchar * key,
			     const gchar * string)
{
	GConfValue *v = gconf_value_new(GCONF_VALUE_STRING);
	gconf_value_set_string(v, string);
	gconf_client_set(gconf, key, v, NULL);
	gconf_value_free(v);
}

static void gconf_set_bool(GConfClient * gconf, const gchar * key, gboolean x)
{
	GConfValue *v;

	v = gconf_value_new(GCONF_VALUE_BOOL);
	gconf_value_set_bool(v, x);
	gconf_client_set(gconf, key, v, NULL);
	gconf_value_free(v);
}

static void save_peer(gpointer elem, gpointer data)
{
	struct wg_peer *peer = elem;
	struct wizard_data *w_data = data;
	gchar *peer_name, *gconf_path, *gconf_pubkey, *gconf_psk, *gconf_ips,
	    *gconf_endpoint;

	w_data->peer_idx++;
	peer_name = g_strdup_printf("peer%d", w_data->peer_idx);

	gconf_path =
	    g_strjoin("/", GC_WIREGUARD, w_data->config_name, GC_PEERS,
		      peer_name, NULL);

	g_free(peer_name);

	gconf_client_add_dir(w_data->gconf, gconf_path,
			     GCONF_CLIENT_PRELOAD_NONE, NULL);

	gconf_pubkey = g_strjoin("/", gconf_path, GC_PEER_PUBKEY, NULL);
	gconf_set_string(w_data->gconf, gconf_pubkey, peer->public_key);
	g_free(gconf_pubkey);

	gconf_psk = g_strjoin("/", gconf_path, GC_PEER_PSK, NULL);
	/* Optional key */
	if (peer->preshared_key && strlen(peer->preshared_key) == 44) {
		gconf_set_string(w_data->gconf, gconf_psk, peer->preshared_key);
	} else {
		gconf_client_unset(w_data->gconf, gconf_psk, NULL);
	}
	g_free(gconf_psk);

	/* TODO: Check if this can be missing from the config */
	if (peer->allowed_ips != NULL) {
		gconf_ips = g_strjoin("/", gconf_path, GC_PEER_IPS, NULL);
		gconf_set_string(w_data->gconf, gconf_ips, peer->allowed_ips);
		g_free(gconf_ips);
	}

	gconf_endpoint = g_strjoin("/", gconf_path, GC_PEER_ENDPOINT, NULL);
	gconf_set_string(w_data->gconf, gconf_endpoint, peer->endpoint);
	g_free(gconf_endpoint);

	g_free(gconf_path);
}

static void on_assistant_apply_wg(GtkWidget * widget, gpointer data)
{
	(void)widget;
	struct wizard_data *w_data = data;
	GtkAssistant *assistant = GTK_ASSISTANT(w_data->assistant);

	if (gtk_assistant_get_current_page(assistant) == 0)
		return;

	w_data->gconf = gconf_client_get_default();
	gchar *gconf_tpbool, *gconf_privkey, *gconf_addr, *gconf_dns,
	    *gconf_peers;

	w_data->config_name = gtk_entry_get_text(GTK_ENTRY(w_data->name_entry));
	gchar *confname =
	    g_strjoin("/", GC_WIREGUARD, w_data->config_name, NULL);

	gconf_client_add_dir(w_data->gconf, confname, GCONF_CLIENT_PRELOAD_NONE,
			     NULL);

	g_object_get(G_OBJECT(w_data->transproxy_chk), "active",
		     &w_data->transproxy_enabled, NULL);

	gconf_tpbool = g_strjoin("/", confname, GC_SYSTUNNEL, NULL);
	gconf_set_bool(w_data->gconf, gconf_tpbool, w_data->transproxy_enabled);
	g_free(gconf_tpbool);

	w_data->private_key =
	    gtk_entry_get_text(GTK_ENTRY(w_data->privkey_entry));

	gconf_privkey = g_strjoin("/", confname, GC_CFG_PRIVATEKEY, NULL);
	gconf_set_string(w_data->gconf, gconf_privkey, w_data->private_key);
	g_free(gconf_privkey);

	w_data->address = gtk_entry_get_text(GTK_ENTRY(w_data->addr_entry));

	gconf_addr = g_strjoin("/", confname, GC_CFG_ADDRESS, NULL);
	gconf_set_string(w_data->gconf, gconf_addr, w_data->address);
	g_free(gconf_addr);

	w_data->dns_address =
	    gtk_entry_get_text(GTK_ENTRY(w_data->dnsaddr_entry));

	gconf_dns = g_strjoin("/", confname, GC_CFG_DNS, NULL);
	if (g_strcmp0(w_data->dns_address, "") && g_strcmp0(w_data->dns_address, "(optional)"))
		gconf_set_string(w_data->gconf, gconf_dns, w_data->dns_address);
	else
		gconf_client_unset(w_data->gconf, gconf_dns, NULL);
	g_free(gconf_dns);

	gconf_peers = g_strjoin("/", confname, GC_PEERS, NULL);

	/* Nuke old peers data */
	gconf_client_recursive_unset(w_data->gconf, gconf_peers, 0, NULL);
	gconf_client_remove_dir(w_data->gconf, gconf_peers, NULL);

	if (w_data->has_peers && w_data->peers->len > 0) {
		gconf_client_add_dir(w_data->gconf, gconf_peers,
				     GCONF_CLIENT_PRELOAD_NONE, NULL);

		w_data->peer_idx = -1;
		g_ptr_array_foreach(w_data->peers, save_peer, w_data);
		g_ptr_array_foreach(w_data->peers, free_peer, NULL);
	}
	g_ptr_array_unref(w_data->peers);
	w_data->peers = NULL;

	g_free(gconf_peers);

	g_free(confname);
	g_object_unref(w_data->gconf);
}

static void on_assistant_prepare(GtkWidget * assistant,
				 GtkWidget * page, gpointer data)
{
	struct wizard_data *w_data = data;

	gint page_number =
	    gtk_assistant_get_current_page(GTK_ASSISTANT(assistant));

	if (page_number == w_data->local_page) {
		if (w_data->peers_page > 0)
			gtk_assistant_set_page_type(GTK_ASSISTANT(assistant),
						    page,
						    GTK_ASSISTANT_PAGE_CONTENT);
		else
			gtk_assistant_set_page_type(GTK_ASSISTANT(assistant),
						    page,
						    GTK_ASSISTANT_PAGE_CONFIRM);
		return;
	}

	if (page_number == w_data->peers_page) {
		gtk_assistant_set_page_type(GTK_ASSISTANT(assistant), page,
					    GTK_ASSISTANT_PAGE_CONFIRM);
		return;
	}
}

static void wg_privkey_generate_cb(GtkWidget * widget, gpointer data)
{
	struct wizard_data *w_data = data;
	gchar *pk, **private_key;
	gchar *cmd_pk[] = { "/usr/bin/wg", "genkey", NULL };

	if (pipe_cmd(cmd_pk, NULL, &pk)) {
		g_critical("Failed to generate Wireguard private key");
		if (pk != NULL)
			g_free(pk);
		return;
	}

	private_key = g_strsplit(pk, "\n", 2);
	g_free(pk);

	gtk_entry_set_text(GTK_ENTRY(w_data->privkey_entry), private_key[0]);
	g_strfreev(private_key);
}

static void validate_interface_cb(GtkWidget * widget, gpointer data)
{
	struct wizard_data *w_data = data;
	GtkAssistant *assistant = GTK_ASSISTANT(w_data->assistant);
	gint page_number;
	GtkWidget *cur_page;
	const gchar *dns_addr, *iface_addr, *privkey, *pubkey;
	gchar **addr_toks;
	gint64 subnet;

	page_number = gtk_assistant_get_current_page(assistant);
	cur_page = gtk_assistant_get_nth_page(assistant, page_number);

	/* Validate the optional DNS address */
	dns_addr = gtk_entry_get_text(GTK_ENTRY(w_data->dnsaddr_entry));
	if (g_strcmp0(dns_addr, "") && g_strcmp0(dns_addr, "(optional)")) {
		if (!g_hostname_is_ip_address(dns_addr)) {
			g_warning("DNS Address is invalid");
			goto invalid;
		}
	}

	/* 
	 * This is in the form of 10.0.0.1/24, so we split it and do some
	 * kind of validation
	 */
	iface_addr = gtk_entry_get_text(GTK_ENTRY(w_data->addr_entry));
	addr_toks = g_strsplit(iface_addr, "/", 2);
	if (g_strv_length(addr_toks) != 2) {
		g_strfreev(addr_toks);
		goto invalid;
	}

	if (!g_hostname_is_ip_address(addr_toks[0])) {
		g_warning("Address is invalid");
		g_strfreev(addr_toks);
		goto invalid;
	}

	subnet = g_ascii_strtoll(addr_toks[1], NULL, 10);

	if (subnet < 16 || subnet > 30) {
		g_warning("Subnet is invalid");
		g_strfreev(addr_toks);
		goto invalid;
	}

	g_strfreev(addr_toks);

	/* And finally we try to check if the keys are valid */
	/* They're always 44 bytes encoded. */
	privkey = gtk_entry_get_text(GTK_ENTRY(w_data->privkey_entry));
	pubkey = gtk_entry_get_text(GTK_ENTRY(w_data->pubkey_entry));

	if (strlen(privkey) != 44 || strlen(pubkey) != 44) {
		g_warning("Keys are invalid");
		goto invalid;
	}

	gtk_assistant_set_page_complete(assistant, cur_page, TRUE);
	return;

 invalid:
	gtk_assistant_set_page_complete(assistant, cur_page, FALSE);
}

static void validate_privkey_cb(GtkWidget * widget, gpointer data)
{
	struct wizard_data *w_data = data;
	const gchar *privkey;
	gchar **pubkey, *pk = NULL;
	gchar *cmd_pk[] = { "/usr/bin/wg", "pubkey", NULL };

	privkey = gtk_entry_get_text(GTK_ENTRY(w_data->privkey_entry));
	gtk_entry_set_text(GTK_ENTRY(w_data->pubkey_entry), "");

	if (strlen(privkey) != 44)
		return;

	if (pipe_cmd(cmd_pk, (gchar *) privkey, &pk)) {
		g_warning("Failed to calculate Wireguard public key");
		if (pk != NULL)
			g_free(pk);
		return;
	}

	if (pk == NULL)
		return;

	pubkey = g_strsplit(pk, "\n", 2);
	g_free(pk);

	gtk_entry_set_text(GTK_ENTRY(w_data->pubkey_entry), pubkey[0]);
	g_strfreev(pubkey);
}

static gint new_wizard_local_page(struct wizard_data *w_data)
{
	gint rv;
	GtkWidget *vbox, *btn_generate;
	GtkWidget *privkey_lbl, *pubkey_lbl, *addr_lbl, *dnsaddr_lbl;

	vbox = gtk_vbox_new(TRUE, 2);

	gtk_container_set_border_width(GTK_CONTAINER(vbox), 15);

	/* Private key entry and generation button */
	GtkWidget *hb0 = gtk_hbox_new(FALSE, 2);

	privkey_lbl = gtk_label_new("Private key:");
	w_data->privkey_entry = gtk_entry_new();

	if (w_data->private_key)
		gtk_entry_set_text(GTK_ENTRY(w_data->privkey_entry),
				   w_data->private_key);

	btn_generate = gtk_button_new_with_label("Generate");

	gtk_box_pack_start(GTK_BOX(hb0), privkey_lbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hb0), w_data->privkey_entry, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hb0), btn_generate, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hb0, TRUE, TRUE, 0);

	g_signal_connect(G_OBJECT(btn_generate), "clicked",
			 G_CALLBACK(wg_privkey_generate_cb), w_data);

	g_signal_connect(G_OBJECT(w_data->privkey_entry), "changed",
			 G_CALLBACK(validate_privkey_cb), w_data);

	/* Public key entry (insensitive) */
	GtkWidget *hb1 = gtk_hbox_new(FALSE, 2);

	pubkey_lbl = gtk_label_new("Public key:");
	w_data->pubkey_entry = gtk_entry_new();
	g_object_set(G_OBJECT(w_data->pubkey_entry), "editable", FALSE, NULL);

	gtk_box_pack_start(GTK_BOX(hb1), pubkey_lbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hb1), w_data->pubkey_entry, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hb1, TRUE, TRUE, 0);

	/* Address entry */
	GtkWidget *hb2 = gtk_hbox_new(FALSE, 2);
	addr_lbl = gtk_label_new("Address:");
	w_data->addr_entry = gtk_entry_new();

	if (w_data->address)
		gtk_entry_set_text(GTK_ENTRY(w_data->addr_entry),
				   w_data->address);

	gtk_box_pack_start(GTK_BOX(hb2), addr_lbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hb2), w_data->addr_entry, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hb2, TRUE, TRUE, 0);

	g_signal_connect(G_OBJECT(w_data->addr_entry), "changed",
			 G_CALLBACK(validate_interface_cb), w_data);

	/* DNS Address entry */
	GtkWidget *hb3 = gtk_hbox_new(FALSE, 2);
	dnsaddr_lbl = gtk_label_new("DNS Address:");
	w_data->dnsaddr_entry = gtk_entry_new();

	if (w_data->dns_address)
		gtk_entry_set_text(GTK_ENTRY(w_data->dnsaddr_entry),
				   w_data->dns_address);
	else
		gtk_entry_set_text(GTK_ENTRY(w_data->dnsaddr_entry),
				   "(optional)");

	gtk_box_pack_start(GTK_BOX(hb3), dnsaddr_lbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hb3), w_data->dnsaddr_entry, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hb3, TRUE, TRUE, 0);

	g_signal_connect(G_OBJECT(w_data->dnsaddr_entry), "changed",
			 G_CALLBACK(validate_interface_cb), w_data);

	gtk_widget_show_all(vbox);

	rv = gtk_assistant_append_page(GTK_ASSISTANT(w_data->assistant), vbox);
	gtk_assistant_set_page_title(GTK_ASSISTANT(w_data->assistant), vbox,
				     "Interface configuration");

	if (w_data->private_key) {
		validate_privkey_cb(NULL, w_data);
		validate_interface_cb(NULL, w_data);
	}

	return rv;
}

static void prev_peer_cb(GtkWidget * widget, gpointer data)
{
	struct wizard_data *w_data = data;
	struct wg_peer *peer;

	w_data->peer_idx--;

	if (w_data->peer_idx <= 0) {
		w_data->peer_idx = 0;
		gtk_widget_set_sensitive(widget, FALSE);
	}

	if (w_data->peers->len > 0)
		gtk_widget_set_sensitive(w_data->p_next_btn, TRUE);

	peer = w_data->peers->pdata[w_data->peer_idx];

	gtk_entry_set_text(GTK_ENTRY(w_data->p_pubkey_entry), peer->public_key);
	gtk_entry_set_text(GTK_ENTRY(w_data->p_psk_entry), peer->preshared_key);
	gtk_entry_set_text(GTK_ENTRY(w_data->p_endpoint_entry), peer->endpoint);
	if (peer->allowed_ips != NULL)
		gtk_entry_set_text(GTK_ENTRY(w_data->p_ips_entry), peer->allowed_ips);

	gtk_widget_set_sensitive(w_data->p_del_btn, TRUE);
}

static void next_peer_cb(GtkWidget * widget, gpointer data)
{
	/* TODO: There is some bug in these prev/next functions */
	struct wizard_data *w_data = data;
	struct wg_peer *peer;

	w_data->peer_idx++;

	if (w_data->peer_idx >= w_data->peers->len || w_data->peers->len == 0) {
		gtk_entry_set_text(GTK_ENTRY(w_data->p_pubkey_entry), "");
		gtk_entry_set_text(GTK_ENTRY(w_data->p_psk_entry), "");
		gtk_entry_set_text(GTK_ENTRY(w_data->p_endpoint_entry), "");
		gtk_entry_set_text(GTK_ENTRY(w_data->p_ips_entry), "");

		gtk_widget_set_sensitive(widget, FALSE);
		gtk_widget_set_sensitive(w_data->p_del_btn, FALSE);
		gtk_widget_set_sensitive(w_data->p_prev_btn,
					 w_data->peer_idx > 0 ? TRUE : FALSE);
		return;
	}

	peer = w_data->peers->pdata[w_data->peer_idx];
	gtk_entry_set_text(GTK_ENTRY(w_data->p_pubkey_entry), peer->public_key);
	gtk_entry_set_text(GTK_ENTRY(w_data->p_psk_entry), peer->preshared_key);
	gtk_entry_set_text(GTK_ENTRY(w_data->p_endpoint_entry), peer->endpoint);
	gtk_entry_set_text(GTK_ENTRY(w_data->p_ips_entry), peer->allowed_ips);

	if (w_data->peer_idx == w_data->peers->len - 1)
		gtk_widget_set_sensitive(widget, FALSE);

	gtk_widget_set_sensitive(w_data->p_del_btn, TRUE);
}

static void del_peer_cb(GtkWidget * widget, gpointer data)
{
	struct wizard_data *w_data = data;
	struct wg_peer *peer;

	if (w_data->peers->pdata[w_data->peer_idx] != NULL) {
		peer = g_ptr_array_steal_index(w_data->peers, w_data->peer_idx);
		free_peer(peer, NULL);
		w_data->peer_idx--;
		next_peer_cb(w_data->p_next_btn, w_data);
	}
}

static void validate_peer_cb(GtkWidget * widget, gpointer data)
{
	(void)widget;
	struct wizard_data *w_data = data;
	struct wg_peer *peer;
	const gchar *pubkey, *psk, *fendpoint, *fips;
	gchar **endpoint;
	gint64 port;
	GtkAssistant *assistant = GTK_ASSISTANT(w_data->assistant);
	gint page_number;
	GtkWidget *cur_page;

	page_number = gtk_assistant_get_current_page(assistant);
	cur_page = gtk_assistant_get_nth_page(assistant, page_number);

	pubkey = gtk_entry_get_text(GTK_ENTRY(w_data->p_pubkey_entry));
	psk = gtk_entry_get_text(GTK_ENTRY(w_data->p_psk_entry));
	fendpoint = gtk_entry_get_text(GTK_ENTRY(w_data->p_endpoint_entry));
	fips = gtk_entry_get_text(GTK_ENTRY(w_data->p_ips_entry));

	if (w_data->peers->len > 0) {
		if (w_data->peers->pdata[w_data->peer_idx] != NULL) {
			gboolean same = TRUE;
			struct wg_peer *p =
			    w_data->peers->pdata[w_data->peer_idx];

			if (g_strcmp0(p->public_key, pubkey))
				same = FALSE;

			if (g_strcmp0(p->preshared_key, psk))
				same = FALSE;

			if (g_strcmp0(p->endpoint, fendpoint))
				same = FALSE;

			if (g_strcmp0(p->allowed_ips, fips))
				same = FALSE;

			gtk_assistant_set_page_complete(assistant, cur_page,
							same);

			if (same)
				return;
		}
	}

	if (!g_strcmp0(pubkey, "")
	    && (!g_strcmp0(psk, "(optional)") || !g_strcmp0(psk, ""))
	    && !g_strcmp0(fendpoint, "")) {
	    //&& !g_strcmp0(fendpoint, "") && !g_strcmp0(fips, "")) {
		gtk_assistant_set_page_complete(assistant, cur_page, TRUE);
		return;
	}

	/* TODO: Better validation */
	if (strlen(pubkey) != 44) {
		hildon_banner_show_information(NULL, NULL, "Invalid pubkey");
		goto invalid;
	}

	if (strlen(psk) != 44
	    && (g_strcmp0(psk, "(optional)") && g_strcmp0("", psk))) {
		hildon_banner_show_information(NULL, NULL, "Invalid PSK");
		goto invalid;
	}

	endpoint = g_strsplit(fendpoint, ":", 2);
	if (g_strv_length(endpoint) != 2) {
		hildon_banner_show_information(NULL, NULL, "Invalid Endpoint");
		g_strfreev(endpoint);
		goto invalid;
	}

	/* TODO: Maybe support domains? */
	if (!g_hostname_is_ip_address(endpoint[0])) {
		hildon_banner_show_information(NULL, NULL,
					       "Endpoint is not a valid IPv4");
		g_strfreev(endpoint);
		goto invalid;
	}

	port = g_ascii_strtoll(endpoint[1], NULL, 10);
	if (port < 1 || port > 65535) {
		hildon_banner_show_information(NULL, NULL,
					       "Endpoint has an invalid port number");
		g_strfreev(endpoint);
		goto invalid;
	}

	/* TODO: Support multiple, i.e.: 0.0.0.0/0, ::/0 */
	/*
	gchar **ips = g_strsplit(fips, "/", 2);
	if (g_strv_length(ips) != 2) {
		hildon_banner_show_information(NULL, NULL,
					       "Allowed IP is invalid");
		g_strfreev(ips);
		goto invalid;
	}

	if (!g_hostname_is_ip_address(ips[0])) {
		hildon_banner_show_information(NULL, NULL,
					       "Allowed IP is invalid");
		g_strfreev(ips);
		goto invalid;
	}

	gint64 subnet = g_ascii_strtoll(ips[1], NULL, 10);

	if (subnet != 0) {
		if (subnet < 16 || subnet > 30) {
			hildon_banner_show_information(NULL, NULL,
						       "Allowed IP has invalid subnet");
			g_strfreev(ips);
			goto invalid;
		}
	}
	*/

	/* At this point, we consider the entries valid */
	peer = NULL;
	peer = g_new0(struct wg_peer, 1);
	peer->public_key = g_strdup(pubkey);
	peer->preshared_key = g_strdup(psk);
	peer->endpoint = g_strdup(fendpoint);
	if (strlen(fips) > 0)
		peer->allowed_ips = g_strdup(fips);
	else
		peer->allowed_ips = NULL;

	if (w_data->peer_idx >= w_data->peers->len) {
		g_ptr_array_add(w_data->peers, peer);
	} else {
		struct wg_peer *p;
		p = g_ptr_array_steal_index(w_data->peers, w_data->peer_idx);
		free_peer(p, NULL);
		g_ptr_array_insert(w_data->peers, w_data->peer_idx, peer);
	}

	gtk_assistant_set_page_complete(assistant, cur_page, TRUE);

	hildon_banner_show_information(NULL, NULL, "Saved");

	next_peer_cb(w_data->p_next_btn, w_data);
	return;

 invalid:
	gtk_assistant_set_page_complete(assistant, cur_page, FALSE);
}

static gint new_wizard_peer_page(struct wizard_data *w_data)
{
	gint rv;
	GtkWidget *vbox;
	GtkWidget *pubkey_lbl, *psk_lbl, *endpoint_lbl, *ips_lbl;
	struct wg_peer *peer = NULL;

	if (w_data->has_peers && w_data->peers->len > 0)
		peer = w_data->peers->pdata[0];

	vbox = gtk_vbox_new(TRUE, 2);

	gtk_container_set_border_width(GTK_CONTAINER(vbox), 15);

	/* Public key entry */
	GtkWidget *hb0 = gtk_hbox_new(FALSE, 2);
	pubkey_lbl = gtk_label_new("Public key:");
	w_data->p_pubkey_entry = gtk_entry_new();

	if (peer)
		gtk_entry_set_text(GTK_ENTRY(w_data->p_pubkey_entry),
				   peer->public_key);

	gtk_box_pack_start(GTK_BOX(hb0), pubkey_lbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hb0), w_data->p_pubkey_entry, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hb0, TRUE, TRUE, 0);

	/* PSK entry */
	GtkWidget *hb1 = gtk_hbox_new(FALSE, 2);
	psk_lbl = gtk_label_new("Preshared key:");
	w_data->p_psk_entry = gtk_entry_new();

	if (peer && peer->preshared_key != NULL)
		gtk_entry_set_text(GTK_ENTRY(w_data->p_psk_entry),
				   peer->preshared_key);
	else
		gtk_entry_set_text(GTK_ENTRY(w_data->p_psk_entry),
				   "(optional)");

	gtk_box_pack_start(GTK_BOX(hb1), psk_lbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hb1), w_data->p_psk_entry, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hb1, TRUE, TRUE, 0);

	/* Endpoint entry */
	GtkWidget *hb2 = gtk_hbox_new(FALSE, 2);
	endpoint_lbl = gtk_label_new("Endpoint:");
	w_data->p_endpoint_entry = gtk_entry_new();

	if (peer)
		gtk_entry_set_text(GTK_ENTRY(w_data->p_endpoint_entry),
				   peer->endpoint);

	gtk_box_pack_start(GTK_BOX(hb2), endpoint_lbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hb2), w_data->p_endpoint_entry, TRUE, TRUE,
			   0);
	gtk_box_pack_start(GTK_BOX(vbox), hb2, TRUE, TRUE, 0);

	/* AllowedIPs entry */
	GtkWidget *hb3 = gtk_hbox_new(FALSE, 2);
	ips_lbl = gtk_label_new("Allowed IPs:");
	w_data->p_ips_entry = gtk_entry_new();

	if (peer)
		gtk_entry_set_text(GTK_ENTRY(w_data->p_ips_entry),
				   peer->allowed_ips);

	gtk_box_pack_start(GTK_BOX(hb3), ips_lbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hb3), w_data->p_ips_entry, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hb3, TRUE, TRUE, 0);

	/* Save/Delete/Previous/Next */
	GtkWidget *hb4 = gtk_hbox_new(FALSE, 2);
	w_data->p_save_btn = gtk_button_new_with_label("Save peer");
	w_data->p_del_btn = gtk_button_new_with_label("Delete peer");

	w_data->p_prev_btn = gtk_button_new_with_label("Previous");
	gtk_widget_set_sensitive(w_data->p_prev_btn, FALSE);

	w_data->p_next_btn = gtk_button_new_with_label("Next");
	if (w_data->has_peers && w_data->peers->len < 2)
		gtk_widget_set_sensitive(w_data->p_next_btn, FALSE);

	if (w_data->peers == NULL || w_data->peers->len == 0)
		gtk_widget_set_sensitive(w_data->p_del_btn, FALSE);

	g_signal_connect(G_OBJECT(w_data->p_save_btn), "clicked",
			 G_CALLBACK(validate_peer_cb), w_data);

	g_signal_connect(G_OBJECT(w_data->p_del_btn), "clicked",
			 G_CALLBACK(del_peer_cb), w_data);

	g_signal_connect(G_OBJECT(w_data->p_prev_btn), "clicked",
			 G_CALLBACK(prev_peer_cb), w_data);

	g_signal_connect(G_OBJECT(w_data->p_next_btn), "clicked",
			 G_CALLBACK(next_peer_cb), w_data);

	gtk_box_pack_start(GTK_BOX(hb4), w_data->p_save_btn, TRUE, TRUE, 2);
	gtk_box_pack_start(GTK_BOX(hb4), w_data->p_del_btn, TRUE, TRUE, 2);
	gtk_box_pack_start(GTK_BOX(hb4), w_data->p_prev_btn, TRUE, TRUE, 2);
	gtk_box_pack_start(GTK_BOX(hb4), w_data->p_next_btn, TRUE, TRUE, 2);
	gtk_box_pack_start(GTK_BOX(vbox), hb4, TRUE, TRUE, 0);

	gtk_widget_show_all(vbox);

	rv = gtk_assistant_append_page(GTK_ASSISTANT(w_data->assistant), vbox);
	gtk_assistant_set_page_title(GTK_ASSISTANT(w_data->assistant), vbox,
				     "Peer configuration");

	if (peer)
		gtk_assistant_set_page_complete(GTK_ASSISTANT
						(w_data->assistant), vbox,
						TRUE);

	return rv;
}

static void on_conf_name_entry_changed(GtkWidget * widget, gpointer data)
{
	struct wizard_data *w_data = data;
	GtkAssistant *assistant = GTK_ASSISTANT(w_data->assistant);
	GtkWidget *cur_page;
	gint page_number;
	const gchar *text;
	gboolean valid = TRUE;

	page_number = gtk_assistant_get_current_page(assistant);
	cur_page = gtk_assistant_get_nth_page(assistant, page_number);
	text = gtk_entry_get_text(GTK_ENTRY(widget));

	/* TODO: Also check for name clash if we're not editing an existing cfg */
	if (text && *text) {
		/* For sanity's sake, we'll only allow alphanumeric names */
		for (int i = 0; i < strlen(text); i++) {
			if (!g_ascii_isalnum(text[i])) {
				valid = FALSE;
				break;
			}
		}
	} else {
		valid = FALSE;
	}

	gtk_assistant_set_page_complete(assistant, cur_page, valid);
}

static void on_peers_chk_toggled(GtkWidget * widget, gpointer data)
{
	(void)widget;
	struct wizard_data *w_data = data;
	gboolean active;

	/* We assume page should be 0, as the checkbox is there */
	g_object_get(G_OBJECT(w_data->peers_chk), "active", &active, NULL);

	if (active) {
		w_data->peers_page = new_wizard_peer_page(w_data);
		w_data->has_peers = TRUE;
		return;
	}

	w_data->peers_page = -1;
	w_data->has_peers = FALSE;
}

static void new_wizard_main_page(struct wizard_data *w_data)
{
	GtkWidget *vbox, *box, *info, *name_label;

	vbox = gtk_vbox_new(FALSE, 5);
	box = gtk_hbox_new(FALSE, 12);

	gtk_container_set_border_width(GTK_CONTAINER(vbox), 15);
	gtk_container_set_border_width(GTK_CONTAINER(box), 12);

	info = gtk_label_new("This wizard will help you configure Wireguard");

	name_label = gtk_label_new("Configuration name:");

	w_data->name_entry = gtk_entry_new();
	if (w_data->config_name != NULL)
		gtk_entry_set_text(GTK_ENTRY(w_data->name_entry),
				   w_data->config_name);

	g_signal_connect(G_OBJECT(w_data->name_entry), "changed",
			 G_CALLBACK(on_conf_name_entry_changed), w_data);

	w_data->transproxy_chk =
	    gtk_check_button_new_with_label("Enable system-wide tunneling");
	if (w_data->transproxy_enabled)
		g_object_set(G_OBJECT(w_data->transproxy_chk), "active", TRUE,
			     NULL);

	w_data->peers_chk = gtk_check_button_new_with_label("Add peers");
	if (w_data->has_peers)
		g_object_set(G_OBJECT(w_data->peers_chk), "active", TRUE, NULL);

	g_signal_connect(G_OBJECT(w_data->peers_chk), "toggled",
			 G_CALLBACK(on_peers_chk_toggled), w_data);

	gtk_box_pack_start(GTK_BOX(box), name_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), w_data->name_entry, TRUE, TRUE, 0);
	gtk_widget_show_all(box);

	gtk_box_pack_start(GTK_BOX(vbox), info, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), box, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), w_data->transproxy_chk, FALSE, FALSE,
			   0);
	gtk_box_pack_start(GTK_BOX(vbox), w_data->peers_chk, FALSE, FALSE, 0);

	gtk_widget_show_all(vbox);

	gtk_assistant_append_page(GTK_ASSISTANT(w_data->assistant), vbox);

	gtk_assistant_set_page_title(GTK_ASSISTANT
				     (w_data->assistant), vbox,
				     "Wireguard Configuration Wizard");

	if (w_data->config_name)
		on_conf_name_entry_changed(w_data->name_entry, w_data);

	if (w_data->has_peers)
		on_peers_chk_toggled(w_data->peers_chk, w_data);
}

void start_new_wizard(gpointer config_data)
{
	struct wizard_data *w_data;

	if (config_data == NULL) {
		w_data = g_new0(struct wizard_data, 1);
		w_data->peers = g_ptr_array_new();
		w_data->has_peers = FALSE;
		w_data->peer_idx = 0;
	} else {
		w_data = config_data;
		w_data->peer_idx = 0;
	}

	w_data->assistant = gtk_assistant_new();

	gtk_window_set_title(GTK_WINDOW(w_data->assistant),
			     "Wireguard configuration");
	gtk_assistant_set_forward_page_func(GTK_ASSISTANT
					    (w_data->assistant),
					    find_next_wizard_page,
					    w_data, NULL);

	new_wizard_main_page(w_data);

	g_signal_connect(G_OBJECT(w_data->assistant),
			 "cancel",
			 G_CALLBACK
			 (on_assistant_close_cancel_wg), &w_data->assistant);

	g_signal_connect(G_OBJECT(w_data->assistant),
			 "close",
			 G_CALLBACK
			 (on_assistant_close_cancel_wg), &w_data->assistant);

	g_signal_connect(G_OBJECT(w_data->assistant), "prepare",
			 G_CALLBACK(on_assistant_prepare), w_data);

	g_signal_connect(G_OBJECT(w_data->assistant),
			 "apply", G_CALLBACK(on_assistant_apply_wg), w_data);

	w_data->local_page = new_wizard_local_page(w_data);

	gtk_widget_show_all(w_data->assistant);

	gtk_main();
	g_free(w_data);
}
