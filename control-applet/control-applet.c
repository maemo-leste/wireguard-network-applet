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

#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <hildon/hildon.h>
#include <hildon-cp-plugin/hildon-cp-plugin-interface.h>

#include "configuration.h"
#include "wizard.h"

enum {
	CONFIG_NEW,
	CONFIG_EDIT,
	CONFIG_DELETE,
	CONFIG_DONE,
};

enum {
	LIST_ITEM = 0,
	N_COLUMNS
};

GtkWidget *cfg_tree;

static void add_to_treeview(GtkWidget * tv, const gchar * str)
{
	GtkListStore *store;
	GtkTreeIter iter;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tv)));

	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, LIST_ITEM, str, -1);
}

static void fill_treeview_from_gconf(GtkWidget * tv)
{
	GConfClient *gconf;
	GSList *configs, *iter;

	gconf = gconf_client_get_default();
	configs = gconf_client_all_dirs(gconf, GC_WIREGUARD, NULL);
	g_object_unref(gconf);

	for (iter = configs; iter; iter = iter->next) {
		add_to_treeview(tv, g_path_get_basename(iter->data));
		g_free(iter->data);
	}

	g_slist_free(iter);
	g_slist_free(configs);
}

static GtkWidget *new_main_dialog(GtkWindow * parent)
{
	GtkWidget *dialog;
	GtkListStore *store;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	dialog =
	    gtk_dialog_new_with_buttons("Wireguard Configurations", parent, 0,
					"New", CONFIG_NEW,
					"Edit", CONFIG_EDIT,
					"Delete", CONFIG_DELETE,
					"Done", CONFIG_DONE, NULL);

	cfg_tree = gtk_tree_view_new();
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(cfg_tree), FALSE);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), cfg_tree, TRUE,
			   TRUE, 0);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Items",
							  renderer, "text",
							  LIST_ITEM, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(cfg_tree), column);
	store = gtk_list_store_new(1, G_TYPE_STRING);
	gtk_tree_view_set_model(GTK_TREE_VIEW(cfg_tree), GTK_TREE_MODEL(store));

	g_object_unref(store);

	fill_treeview_from_gconf(cfg_tree);

	return dialog;
}

static gchar *get_sel_in_treeview(GtkTreeView * tv)
{
	GtkTreeSelection *sel = gtk_tree_view_get_selection(tv);
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *ret = NULL;

	if (!gtk_tree_selection_get_selected(sel, &model, &iter))
		return NULL;

	gtk_tree_model_get(model, &iter, LIST_ITEM, &ret, -1);
	return ret;
}

static void delete_config(GtkWidget * parent, GtkTreeView * tv)
{
	GConfClient *gconf;
	gchar *gconf_cfg, *sel_cfg, *q;
	GtkWidget *note;
	gint i;

	if (!(sel_cfg = get_sel_in_treeview(tv)))
		return;

	/* Don't allow deletion of the default config */
	if (!strcmp(sel_cfg, "Default")) {
		g_free(sel_cfg);
		return;
	}

	q = g_strdup_printf
	    ("Are you sure you want to delete the \"%s\" configuration?",
	     sel_cfg);

	note = hildon_note_new_confirmation(GTK_WINDOW(parent), q);
	g_free(q);
	gtk_window_set_transient_for(GTK_WINDOW(note), GTK_WINDOW(parent));

	i = gtk_dialog_run(GTK_DIALOG(note));
	gtk_object_destroy(GTK_OBJECT(note));

	if (i != GTK_RESPONSE_OK) {
		g_free(sel_cfg);
		return;
	}

	gconf_cfg = g_strjoin("/", GC_WIREGUARD, sel_cfg, NULL);

	gconf = gconf_client_get_default();
	gconf_client_recursive_unset(gconf, gconf_cfg, 0, NULL);
	gconf_client_remove_dir(gconf, gconf_cfg, NULL);

	g_free(sel_cfg);
	g_free(gconf_cfg);
	g_object_unref(gconf);
}

static void append_peer(gpointer elem, gpointer data)
{
	struct wg_peer *peer;
	struct wizard_data *w_data = data;
	gchar *peername = elem;
	gchar *gc_pubkey, *gc_psk, *gc_endpoint, *gc_ips;
	gchar *pubkey, *psk, *endpoint, *ips;

/*
	gchar *path =
	    g_strjoin("/", GC_WIREGUARD, w_data->config_name, GC_CFG_PEERS,
		      peername, NULL);
		      */

	peer = g_new0(struct wg_peer, 1);

	gc_pubkey = g_strjoin("/", peername, GC_PEER_PUBLICKEY, NULL);
	pubkey = gconf_client_get_string(w_data->gconf, gc_pubkey, NULL);
	g_free(gc_pubkey);
	peer->public_key = g_strdup(pubkey);

	gc_psk = g_strjoin("/", peername, GC_PEER_PSK, NULL);
	psk = gconf_client_get_string(w_data->gconf, gc_psk, NULL);
	g_free(gc_psk);
	if (psk != NULL)
		peer->preshared_key = g_strdup(psk);
	else
		peer->preshared_key = NULL;

	gc_endpoint = g_strjoin("/", peername, GC_PEER_ENDPOINT, NULL);
	endpoint = gconf_client_get_string(w_data->gconf, gc_endpoint, NULL);
	g_free(gc_endpoint);
	peer->endpoint = g_strdup(endpoint);

	gc_ips = g_strjoin("/", peername, GC_PEER_ALLOWEDIPS, NULL);
	ips = gconf_client_get_string(w_data->gconf, gc_ips, NULL);
	g_free(gc_ips);
	peer->allowed_ips = g_strdup(ips);

	//g_free(path);

	g_ptr_array_add(w_data->peers, peer);
}

static struct wizard_data *fill_wizard_data_from_gconf(gchar * cfgname)
{
	struct wizard_data *w_data;
	gchar *config_path;
	gchar *g_transproxy, *g_privkey, *g_addr, *g_dns, *g_peers;

	if (cfgname == NULL)
		return NULL;

	w_data = g_new0(struct wizard_data, 1);

	w_data->gconf = gconf_client_get_default();

	config_path = g_strjoin("/", GC_WIREGUARD, cfgname, NULL);
	g_transproxy = g_strjoin("/", config_path, GC_CFG_TUNNELENABLED, NULL);
	g_privkey = g_strjoin("/", config_path, GC_CFG_PRIVATEKEY, NULL);
	g_addr = g_strjoin("/", config_path, GC_CFG_ADDRESS, NULL);
	g_dns = g_strjoin("/", config_path, GC_CFG_DNS, NULL);

	w_data->config_name = cfgname;

	w_data->transproxy_enabled =
	    gconf_client_get_bool(w_data->gconf, g_transproxy, NULL);
	g_free(g_transproxy);

	w_data->private_key =
	    gconf_client_get_string(w_data->gconf, g_privkey, NULL);
	g_free(g_privkey);

	w_data->address = gconf_client_get_string(w_data->gconf, g_addr, NULL);
	g_free(g_addr);

	w_data->dns_address =
	    gconf_client_get_string(w_data->gconf, g_dns, NULL);
	g_free(g_dns);

	g_peers = g_strjoin("/", config_path, GC_CFG_PEERS, NULL);
	if (gconf_client_dir_exists(w_data->gconf, g_peers, NULL)) {
		GSList *peerlist =
		    gconf_client_all_dirs(w_data->gconf, g_peers, NULL);
		if (g_slist_length(peerlist) > 0) {
			w_data->has_peers = TRUE;
			w_data->peers = g_ptr_array_new();
			g_slist_foreach(peerlist, append_peer, w_data);
			g_slist_foreach(peerlist, (GFunc) g_free, NULL);
			g_slist_free(peerlist);
		}
	}
	g_free(g_peers);

	g_free(config_path);
	g_object_unref(w_data->gconf);

	return w_data;
}

osso_return_t execute(osso_context_t * osso, gpointer data, gboolean user_act)
{
	(void)osso;
	(void)user_act;
	gboolean config_done = FALSE;
	GtkWidget *maindialog;
	gchar *cfgname;
	struct wizard_data *w_data;

	/* TODO: Write a better way to refresh the tree view */
	do {
		maindialog = new_main_dialog(GTK_WINDOW(data));
		gtk_widget_show_all(maindialog);

		switch (gtk_dialog_run(GTK_DIALOG(maindialog))) {
		case CONFIG_NEW:
			gtk_widget_hide(maindialog);
			start_new_wizard(NULL);
			break;
		case CONFIG_EDIT:
			cfgname = get_sel_in_treeview(GTK_TREE_VIEW(cfg_tree));
			w_data = fill_wizard_data_from_gconf(cfgname);
			start_new_wizard(w_data);
			break;
		case CONFIG_DELETE:
			delete_config(data, GTK_TREE_VIEW(cfg_tree));
			break;
		case CONFIG_DONE:
		default:
			config_done = TRUE;
			break;
		}

		gtk_widget_destroy(maindialog);
	} while (!config_done);

	return OSSO_OK;
}
