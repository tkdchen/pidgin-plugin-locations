/*
 * Locations Plugin
 *
 * Copyright (C) 2011, Chenxiong Qi	<qcxhome@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02111-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif


/* This will prevent compiler errors in some instances and is better explained in the
 * how-to documents on the wiki */
#ifndef G_GNUC_NULL_TERMINATED
# if __GNUC__ >= 4
#  define G_GNUC_NULL_TERMINATED __attribute__((__sentinel__))
# else
#  define G_GNUC_NULL_TERMINATED
# endif
#endif

#include <notify.h>
#include <plugin.h>
#include <version.h>
#include "prefs.h"
#include "debug.h"
#include "gtkutils.h"

#include <gtk/gtk.h>

#define PLUGIN_ID "locations"
#define PREF_PREFIX "/plugins/gtk"
#define PREF_LOCATIONS PREF_PREFIX "/locations"
#define PREF_LOCATION_ACCOUNT_MAP PREF_LOCATIONS "/map"
#define PREF_LAST_LOCATION PREF_LOCATIONS "/last"

PurplePlugin *locations_plugin = NULL;

typedef struct
{
	PurpleAccount *account;
	gboolean enabled;
} AccountStateInfo;

static GHashTable *locations_model = NULL;

/* Testing functions */
static void write_sample_data(void);
static void remove_sample_data(void);

static void print_locations_model(void);
/*********************/

/* Locations model functions */
static void locations_model_load(void);
static void locations_model_save(void);
static void locations_model_free(void);
static GList *locations_model_get_locations_names(void);
static GList *locations_model_lookup_accounts(gchar *location_name);
static gboolean locations_model_delete_location(gchar *location_name);
static void locations_model_free_value_cb(gpointer data);
/*****************************/

static void configure_locations(void);

/* Testing functions */
static void write_sample_data()
{
	GList *sample_data = NULL;

	sample_data = g_list_append(sample_data, "home:qcxhome@gmail.com:prpl-jabber:enabled");
	sample_data = g_list_append(sample_data, "work:qcxhome@hotmail.com:prpl-msn:enabled");
	sample_data = g_list_append(sample_data, "home:qcxhome@yahoo.com:prpl-yahoo:enabled");
	sample_data = g_list_append(sample_data, "work:qcxhome@gmail.com:prpl-jabber:disabled");
	sample_data = g_list_append(sample_data, "home:qcxhome@hotmail.com:prpl-msn:enabled");
	sample_data = g_list_append(sample_data, "home:qcxhome@yahoo.com:prpl-yahoo:disabled");
	sample_data = g_list_append(sample_data, "public:qcxhome@gmail.com:prpl-jabber:enabled");
	sample_data = g_list_append(sample_data, "home:qcxhome@hotmail.com:prpl-msn:enabled");
	sample_data = g_list_append(sample_data, "home:qcxhome@yahoo.com:prpl-yahoo:enabled");

	purple_prefs_add_string_list(PREF_LOCATION_ACCOUNT_MAP, sample_data);

	g_list_free(sample_data);
}

static void remove_sample_data()
{
	purple_prefs_remove(PREF_LOCATION_ACCOUNT_MAP);
	purple_prefs_remove(PREF_LAST_LOCATION);
}

static void print_locations_model()
{
	GList *keys = NULL,
		  *key_item = NULL,
		  *accounts = NULL,
		  *account_item = NULL;
	PurpleAccount *account = NULL;
	AccountStateInfo *asi = NULL;

	keys = locations_model_get_locations_names();
	for (key_item = g_list_first(keys); key_item != NULL; key_item = g_list_next(key_item))
	{
		printf("%s:\n", (gchar *)key_item->data);
		accounts = locations_model_lookup_accounts((gchar *)key_item->data);
		for (account_item = g_list_first(accounts); account_item != NULL; account_item = g_list_next(account_item))
		{
			asi = (AccountStateInfo *)account_item->data;
			printf("\t%s, %s: %s\n",
					purple_account_get_username(asi->account),
					purple_account_get_protocol_id(asi->account),
					asi->enabled ? "Enabled" : "Disabled" );
		}
	}
	g_list_free(keys);
}
/*** End of testing functions ***/

/* Locations model functions */
static void locations_model_free_value_cb(gpointer data)
{
	g_list_free((GList *)data);
}

static void locations_model_load()
{
	GList *map = NULL,
		  *item = NULL;
	gchar **fields = NULL;
	PurpleAccount *account = NULL;
	GList *account_list = NULL;
	AccountStateInfo *asi = NULL;

	locations_model = g_hash_table_new_full(g_str_hash, g_str_equal,
			g_free, /* free the Key */
			NULL
			); 

	map = purple_prefs_get_string_list(PREF_LOCATION_ACCOUNT_MAP);
	if (map == NULL)
		return;

	for (item = g_list_first(map); item != NULL; item = g_list_next(item))
	{
		fields = g_strsplit((gchar *)item->data, ":", -1);
		asi = g_new0(AccountStateInfo, 1);
		asi->account = purple_accounts_find(*(fields + 1), *(fields + 2));
		asi->enabled = g_strcmp0(*(fields + 3), "enabled") == 0 ? TRUE : FALSE;

		account_list = (GList *)g_hash_table_lookup(locations_model, *fields);
		account_list = g_list_append(account_list, asi);
		g_hash_table_insert(locations_model, g_strdup(*fields), account_list);

		g_strfreev(fields);
	}
}

static void locations_model_save()
{
}

static void
locations_model_free_account_info_cb(gpointer data, gpointer user_data)
{
	g_free((AccountStateInfo *)data);
}

static gboolean
locations_model_foreach_free_cb(gpointer key, gpointer value, gpointer data)
{
	g_list_foreach((GList *)value, locations_model_free_account_info_cb, NULL);
	g_list_free((GList *)value);

	return TRUE;
}

static void locations_model_free()
{
	g_hash_table_foreach_remove(locations_model, locations_model_foreach_free_cb, NULL);
	g_hash_table_destroy(locations_model);
	locations_model = NULL;
}

static GList *locations_model_get_locations_names()
{
	return g_hash_table_get_keys(locations_model);
}

static GList *
locations_model_lookup_accounts(gchar *location_name)
{
	return (GList *)g_hash_table_lookup(locations_model, location_name);
}

static gboolean
locations_model_delete_location(gchar *location_name)
{
	return g_hash_table_remove(locations_model, location_name);
}
/*** End of locations model functions ***/

static void
plugin_action_configure_cb (PurplePluginAction * action)
{
	configure_locations();
}

static void
plugin_action_configure_accounts_by_location_cb(PurplePluginAction *action)
{
	configure_locations();
}

static GList *
plugin_actions (PurplePlugin * plugin, gpointer context)
{
	GList *list = NULL,
		  *locations = NULL,
		  *item = NULL;
	gchar *action_name = NULL;
	PurplePluginAction *action = NULL;

	action = purple_plugin_action_new ("Configure", plugin_action_configure_cb);
	list = g_list_append (list, action);

	/* Add actions per location */
	locations = locations_model_get_locations_names();
	for (item = g_list_first(locations); item != NULL; item = g_list_next(item))
	{
		action_name = g_strdup_printf("Location: %s", (gchar *)item->data);
		action = purple_plugin_action_new (action_name, plugin_action_configure_accounts_by_location_cb);
		list = g_list_append (list, action);
		g_free(action_name);
	}
	g_list_free(locations);

	return list;
}

/******* signal handlers *******/

static void
account_status_toggled(GtkCellRenderer *renderer, gchar *path, gpointer data)
{
	GtkWidget *treeview = NULL;
	GtkTreeIter iter;
	GtkTreeModel *model = NULL;
	gboolean value = FALSE;

	treeview = (GtkWidget *)data;
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(data));
	if (gtk_tree_model_get_iter_from_string(model, &iter, path))
	{
		gtk_tree_model_get(model, &iter, 0, &value, -1);
		gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, !value, -1);
	}
}

static void
add_clicked_handler(GtkButton *button, gpointer data)
{
	purple_notify_message (locations_plugin, PURPLE_NOTIFY_MSG_INFO,
		"Locations", "Adding a new location to enable and disable some accounts.", NULL, NULL, NULL);
}

static void
save_clicked_handler(GtkButton *button, gpointer data)
{
	purple_notify_message (locations_plugin, PURPLE_NOTIFY_MSG_INFO,
		"Locations", "Saving your changes.", NULL, NULL, NULL);
}

static void
delete_clicked_handler(GtkButton *button, gpointer data)
{
	purple_notify_message (locations_plugin, PURPLE_NOTIFY_MSG_INFO,
		"Locations", "Delete current locations.", NULL, NULL, NULL);
}

static void
close_clicked_handler(GtkButton *button, gpointer data)
{
	purple_notify_message (locations_plugin, PURPLE_NOTIFY_MSG_INFO,
		"Locations", "Bye Bye!", NULL, NULL, NULL);
}

static void
show_configure_dialog()
{
	GtkWidget *dialog = NULL;
	GtkWidget *accounts_view = NULL;
	GtkWidget *content_area = NULL,
              *label = NULL,
			  *locations_list = NULL,
			  *hbox = NULL;
    GtkWidget *scrolled_win = NULL;
	GtkListStore *accounts_store = NULL;
	GtkTreeIter iter;
	GtkCellRenderer *renderer = NULL;
	GtkTreeViewColumn *column = NULL;
    int width, height;

    GList *accounts = NULL,
          *item = NULL;

	accounts_store = gtk_list_store_new(2, G_TYPE_BOOLEAN, G_TYPE_STRING);
    accounts = purple_accounts_get_all();
    item = g_list_first(accounts);
    for (; item != NULL; item = g_list_next(item))
    {
        gtk_list_store_append(accounts_store, &iter);
        gtk_list_store_set(accounts_store, &iter,
                0, FALSE,
                1, purple_account_get_username((PurpleAccount *)item->data),
                -1);
    }

	accounts_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(accounts_store));

	renderer = gtk_cell_renderer_toggle_new();
	g_object_set(renderer, "activatable", TRUE, NULL);
	g_signal_connect(G_OBJECT(renderer), "toggled", G_CALLBACK(account_status_toggled), accounts_view);
	column = gtk_tree_view_column_new_with_attributes("Status", renderer, "active", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(accounts_view), column);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Account", renderer, "text", 1, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(accounts_view), column);

	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(accounts_view));

    scrolled_win = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_win), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled_win), accounts_view);

	label = gtk_label_new("Location:");
	locations_list = gtk_combo_box_new_text();
	gtk_combo_box_append_text(GTK_COMBO_BOX(locations_list), "home");
	gtk_combo_box_append_text(GTK_COMBO_BOX(locations_list), "work");
	gtk_combo_box_append_text(GTK_COMBO_BOX(locations_list), "public");
	gtk_combo_box_append_text(GTK_COMBO_BOX(locations_list), "costa coffee");
	gtk_combo_box_append_text(GTK_COMBO_BOX(locations_list), "starbucks offee");
	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), locations_list, TRUE, TRUE, 0);

	width  = purple_prefs_get_int("/pidgin/accounts/dialog/width");
	height = purple_prefs_get_int("/pidgin/accounts/dialog/height");

    /* Create the main dialog UI */
	dialog = pidgin_create_dialog("Locations Configure", PIDGIN_HIG_BORDER, "Account", TRUE);
	gtk_window_set_default_size(GTK_WINDOW(dialog), width, height);

	content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	gtk_box_pack_start(GTK_BOX(content_area), hbox, FALSE, TRUE, 3);
	gtk_box_pack_start(GTK_BOX(content_area), scrolled_win, TRUE, TRUE, 3);

    pidgin_dialog_add_button(GTK_DIALOG(dialog), "gtk-new", G_CALLBACK(add_clicked_handler), NULL);
    pidgin_dialog_add_button(GTK_DIALOG(dialog), "gtk-save", G_CALLBACK(save_clicked_handler), NULL);
    pidgin_dialog_add_button(GTK_DIALOG(dialog), "gtk-delete", G_CALLBACK(delete_clicked_handler), NULL);
    pidgin_dialog_add_button(GTK_DIALOG(dialog), "gtk-close", G_CALLBACK(close_clicked_handler), NULL);

	gtk_widget_show_all(dialog);
}

static void
configure_locations()
{
	show_configure_dialog();
}

/******* end of signal handlers *******/

static gboolean
plugin_load (PurplePlugin * plugin)
{
	purple_prefs_add_none(PREF_LOCATIONS);

	write_sample_data();
	locations_model_load();

	print_locations_model();

	locations_plugin = plugin;

	return TRUE;
}

static gboolean
plugin_unload (PurplePlugin * plugin)
{
	if (locations_model != NULL)
	{
		locations_model_save();
		locations_model_free();
	}
	remove_sample_data();
	return TRUE;
}

static PurplePluginInfo info = {
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,
	NULL,
	0,
	NULL,
	PURPLE_PRIORITY_DEFAULT,

	"locations",
	"Locations",
	DISPLAY_VERSION,

	"Locations Plugin",
	"Locations Plugin",
	"Chenxiong Qi <qcxhome@gmail.com>",
	"",


	plugin_load,
	plugin_unload,
	NULL,

	NULL,
	NULL,
	NULL,
	plugin_actions,

	NULL,
	NULL,
	NULL,
	NULL
};

static void
init_plugin (PurplePlugin * plugin)
{
}

PURPLE_INIT_PLUGIN (hello_world, init_plugin, info)
