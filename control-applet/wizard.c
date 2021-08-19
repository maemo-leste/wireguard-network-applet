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

#include "configuration.h"
#include "pipeutil.h"
#include "wizard.h"

static void on_assistant_close_cancel(GtkWidget * widget, gpointer data)
{
	(void)widget;

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

	return -1;
}

/*
static void gconf_set_string(GConfClient * gconf, gchar * key, gchar * string)
{
	GConfValue *v = gconf_value_new(GCONF_VALUE_STRING);
	gconf_value_set_string(v, string);
	gconf_client_set(gconf, key, v, NULL);
	gconf_value_free(v);
}

static void gconf_set_int(GConfClient * gconf, gchar * key, gint x)
{
	GConfValue *v;

	v = gconf_value_new(GCONF_VALUE_INT);
	gconf_value_set_int(v, x);
	gconf_client_set(gconf, key, v, NULL);
	gconf_value_free(v);
}

static void gconf_set_bool(GConfClient * gconf, gchar * key, gboolean x)
{
	GConfValue *v;

	v = gconf_value_new(GCONF_VALUE_BOOL);
	gconf_value_set_bool(v, x);
	gconf_client_set(gconf, key, v, NULL);
	gconf_value_free(v);
}
*/

static void on_assistant_apply(GtkWidget * widget, gpointer data)
{
	(void)widget;
	(void)data;
	return;
}

/*
static void on_assistant_prepare(GtkWidget * assistant,
				 GtkWidget * page, gpointer data)
{
	struct wizard_data *w_data = data;

	gint page_number =
	    gtk_assistant_get_current_page(GTK_ASSISTANT(assistant));

	if (page_number == w_data->bridges_page
	    || page_number == w_data->hs_page) {
		if (w_data->hs_page > 0 || w_data->adv_page > 0)
			gtk_assistant_set_page_type(GTK_ASSISTANT(assistant),
						    page,
						    GTK_ASSISTANT_PAGE_CONTENT);
		else
			gtk_assistant_set_page_type(GTK_ASSISTANT(assistant),
						    page,
						    GTK_ASSISTANT_PAGE_CONFIRM);
		return;
	}

	if (page_number == w_data->adv_page) {
		gtk_assistant_set_page_type(GTK_ASSISTANT(assistant), page,
					    GTK_ASSISTANT_PAGE_CONFIRM);
		return;
	}
}
*/

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

	gtk_box_pack_start(GTK_BOX(box), name_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), w_data->name_entry, TRUE, TRUE, 0);
	gtk_widget_show_all(box);

	gtk_box_pack_start(GTK_BOX(vbox), info, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), box, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), w_data->transproxy_chk, FALSE, FALSE,
			   0);

	gtk_widget_show_all(vbox);

	gtk_assistant_append_page(GTK_ASSISTANT(w_data->assistant), vbox);
	gtk_assistant_set_page_title(GTK_ASSISTANT
				     (w_data->assistant), vbox,
				     "Wireguard Configuration Wizard");
	gtk_assistant_set_page_type(GTK_ASSISTANT(w_data->assistant),
				    vbox, GTK_ASSISTANT_PAGE_CONFIRM);

	if (w_data->config_name)
		on_conf_name_entry_changed(w_data->name_entry, w_data);
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

	gtk_entry_set_text(GTK_ENTRY(w_data->private_key_entry),
			   private_key[0]);
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

	/* Validate the DNS address */
	dns_addr = gtk_entry_get_text(GTK_ENTRY(w_data->dns_address_entry));
	if (!g_hostname_is_ip_address(dns_addr)) {
		g_warning("DNS Address is invalid");
		goto invalid;
	}

	/* 
	 * This is in the form of 10.0.0.1/24, so we split it and do some
	 * kind of validation
	 */
	iface_addr = gtk_entry_get_text(GTK_ENTRY(w_data->address_entry));
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
	g_message("%ld", subnet);
	if (subnet < 16 || subnet > 30) {
		g_warning("Subnet is invalid");
		g_strfreev(addr_toks);
		goto invalid;
	}
	g_strfreev(addr_toks);

	/* And we try to check if the keys are valid */
	/* They're always 44 bytes encoded. */
	privkey = gtk_entry_get_text(GTK_ENTRY(w_data->private_key_entry));
	pubkey = gtk_entry_get_text(GTK_ENTRY(w_data->public_key_entry));
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
	const gchar *secret_key;
	gchar **public_key, *pk = NULL;
	gchar *cmd_pk[] = { "/usr/bin/wg", "pubkey", NULL };

	secret_key = gtk_entry_get_text(GTK_ENTRY(w_data->private_key_entry));
	gtk_entry_set_text(GTK_ENTRY(w_data->public_key_entry), "");

	if (strlen(secret_key) != 44)
		return;

	if (pipe_cmd(cmd_pk, (gchar *) secret_key, &pk)) {
		g_warning("Failed to calculate Wireguard public key");
		if (pk != NULL)
			g_free(pk);
		return;
	}

	if (pk == NULL)
		return;

	public_key = g_strsplit(pk, "\n", 2);
	g_free(pk);

	gtk_entry_set_text(GTK_ENTRY(w_data->public_key_entry), public_key[0]);
	g_strfreev(public_key);
}

static gint new_wizard_local_page(struct wizard_data *w_data)
{
	gint rv;
	GtkWidget *vbox;
	GtkWidget *btn_generate, *private_key_label, *public_key_label,
	    *address_label, *dns_address_label;

	vbox = gtk_vbox_new(TRUE, 2);

	gtk_container_set_border_width(GTK_CONTAINER(vbox), 15);

	GtkWidget *hbox0 = gtk_hbox_new(FALSE, 2);

	private_key_label = gtk_label_new("Private key:");
	w_data->private_key_entry = gtk_entry_new();
	btn_generate = gtk_button_new_with_label("Generate");
	gtk_box_pack_start(GTK_BOX(hbox0), private_key_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox0), w_data->private_key_entry, TRUE,
			   TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox0), btn_generate, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox0, TRUE, TRUE, 0);
	g_signal_connect(G_OBJECT(btn_generate), "clicked",
			 G_CALLBACK(wg_privkey_generate_cb), w_data);

	GtkWidget *hbox1 = gtk_hbox_new(FALSE, 2);
	public_key_label = gtk_label_new("Public key:");
	w_data->public_key_entry = gtk_entry_new();
	gtk_widget_set_sensitive(w_data->public_key_entry, FALSE);
	gtk_box_pack_start(GTK_BOX(hbox1), public_key_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox1), w_data->public_key_entry, TRUE,
			   TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox1, TRUE, TRUE, 0);

	GtkWidget *hbox2 = gtk_hbox_new(FALSE, 2);
	address_label = gtk_label_new("Address:");
	w_data->address_entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox2), address_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox2), w_data->address_entry, TRUE,
			   TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox2, TRUE, TRUE, 0);

	GtkWidget *hbox3 = gtk_hbox_new(FALSE, 2);
	dns_address_label = gtk_label_new("DNS Address:");
	w_data->dns_address_entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox3), dns_address_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox3), w_data->dns_address_entry, TRUE,
			   TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox3, TRUE, TRUE, 0);

	GtkWidget *hbox4 = gtk_hbox_new(FALSE, 2);
	w_data->has_peers_chk = gtk_check_button_new_with_label("Add peers");
	gtk_box_pack_start(GTK_BOX(hbox4), w_data->has_peers_chk, FALSE, FALSE,
			   0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox4, TRUE, TRUE, 0);

	g_signal_connect(G_OBJECT(w_data->private_key_entry), "changed",
			 G_CALLBACK(validate_privkey_cb), w_data);

	g_signal_connect(G_OBJECT(w_data->address_entry), "changed",
			 G_CALLBACK(validate_interface_cb), w_data);

	g_signal_connect(G_OBJECT(w_data->dns_address_entry), "changed",
			 G_CALLBACK(validate_interface_cb), w_data);

	gtk_widget_show_all(vbox);

	rv = gtk_assistant_append_page(GTK_ASSISTANT(w_data->assistant), vbox);
	gtk_assistant_set_page_title(GTK_ASSISTANT(w_data->assistant), vbox,
				     "Interface configuration");
	return rv;
}

void start_new_wizard(gpointer config_data)
{
	struct wizard_data *w_data;
	if (config_data == NULL) {
		w_data = g_new0(struct wizard_data, 1);
	} else {
		w_data = config_data;
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
			 (on_assistant_close_cancel), &w_data->assistant);
	g_signal_connect(G_OBJECT(w_data->assistant),
			 "close",
			 G_CALLBACK
			 (on_assistant_close_cancel), &w_data->assistant);
	/*
	   g_signal_connect(G_OBJECT(w_data->assistant), "prepare",
	   G_CALLBACK(on_assistant_prepare), w_data);
	 */
	g_signal_connect(G_OBJECT(w_data->assistant),
			 "apply", G_CALLBACK(on_assistant_apply), w_data);
	GtkWidget *page =
	    gtk_assistant_get_nth_page(GTK_ASSISTANT(w_data->assistant), 0);
	gtk_assistant_set_page_type(GTK_ASSISTANT
				    (w_data->assistant),
				    page, GTK_ASSISTANT_PAGE_INTRO);
	w_data->local_page = new_wizard_local_page(w_data);
	gtk_widget_show_all(w_data->assistant);
	gtk_main();
	g_free(w_data);
}
