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

#include "internal.h"

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

#define LOCATION_NAME_MAX_LENGTH 30

#define LOCATION_NAME_TIP "Location name only contains letters (either upper or lower case), digits, space, dash and underscore."

PurplePlugin *locations_plugin = NULL;

typedef struct
{
	PurpleAccount *account;
	gboolean enabled;
} AccountStateInfo;

static GHashTable *locations_model = NULL;

static AccountStateInfo *account_state_info_new(PurpleAccount *account, gboolean enabled);
static void account_state_info_free(AccountStateInfo *asi);

/* Locations model functions */
static void locations_model_load(void);
static void locations_model_save(void);
static void locations_model_free(void);
static GList *locations_model_get_locations_names(void);
static GList *locations_model_lookup_accounts(gchar *location_name);
static gboolean locations_model_location_exists(gchar *name);
static void locations_model_add_location(gchar *name, GList *asis);
static gboolean locations_model_delete_location(gchar *location_name);
/*****************************/

/* UI-specific functions */
typedef struct
{
	GtkWidget *dialog;
	GtkWidget *cboLocations; /* A GtkCombox */
	GtkWidget *tvAccounts; /* A GtkTreeView */
	GtkWidget *btnAdd;
	GtkWidget *btnSave;
	GtkWidget *btnDelete;
	GtkWidget *btnClose;
}
LocationConfigurationDialog;

typedef struct
{
	GtkWidget *dialog;
	GtkWidget *prompt;		/* A label */
	GtkWidget *name_entry;	/* A entry */
	GtkWidget *tip;			/* A label */
}
NewLocationNameInputDialog;

static LocationConfigurationDialog *configure_dialog = NULL;
static void location_configure_dialog_create(void);
static void location_configure_dialog_destroy(void);
static void location_configure_dialog_show(void);
static gchar *location_configure_dialog_get_new_location_name(GtkWidget *parent);
static void add_clicked_handler(GtkButton *button, gpointer data);
static void save_clicked_handler(GtkButton *button, gpointer data);
static void delete_clicked_handler(GtkButton *button, gpointer data);
static void cboLocations_changed_handler(GtkComboBox *sender, gpointer data);

static GtkWidget *create_gtk_combo_box(GList *initial_strings);
static void gtk_combo_box_locate_iter(GtkTreeModel *model, const gchar *string, GtkTreeIter *iter);
static void gtk_combo_box_select_string(GtkWidget *combo_box, const gchar *s);
static void gtk_combo_box_add_string(GtkWidget *combo_box, gchar *string);
static void gtk_combo_box_remove_string(GtkWidget *combo_box, const gchar *string);
/****************/

static AccountStateInfo *
account_state_info_new(PurpleAccount *account, gboolean enabled)
{
	AccountStateInfo *asi = NULL;

	asi = g_new0(AccountStateInfo, 1);
	asi->account = account;
	asi->enabled = enabled;

	return asi;
}

static void
account_state_info_free(AccountStateInfo *asi)
{
	if (asi == NULL) return;
	g_free(asi);
}

/* Locations model functions */

static void locations_model_load()
{
	GList *map = NULL,
		  *item = NULL;
	gchar **fields = NULL;
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

		g_free((gchar *)item->data);
		g_strfreev(fields);
	}

	g_list_free(map);
}

static void locations_model_save()
{
	GList *keys = NULL,
		  *key_item = NULL,
		  *values = NULL,
		  *value_item = NULL,
		  *mapping = NULL,
		  *item = NULL;
	AccountStateInfo *asi = NULL;

	keys = locations_model_get_locations_names();
	key_item = g_list_first(keys);
	for (; key_item != NULL; key_item = g_list_next(key_item))
	{
		values = locations_model_lookup_accounts((gchar *)key_item->data);
		value_item = g_list_first(values);
		for (; value_item != NULL; value_item = g_list_next(value_item))
		{
			asi = (AccountStateInfo *)value_item->data;
			mapping = g_list_append(mapping,
					g_strdup_printf("%s:%s:%s:%s",
						(gchar *)key_item->data,
						purple_account_get_username(asi->account),
						purple_account_get_protocol_id(asi->account),
						asi->enabled ? "enabled" : "disabled"));
		}
	}

	purple_prefs_set_string_list(PREF_LOCATION_ACCOUNT_MAP, mapping);

	/* TODO: refactoring the GList free procedure */
	for (item = g_list_first(mapping); item != NULL; item = g_list_next(item))
		g_free((gchar *)item->data);

	g_list_free(mapping);
}

static gboolean
locations_model_location_exists(gchar *name)
{
	return NULL != g_hash_table_lookup(locations_model, name);
}

static void
locations_model_add_location(gchar *name, GList *asis)
{
	g_hash_table_insert(locations_model, g_strdup(name), asis);
}

static void
locations_model_free_account_info_cb(gpointer data, gpointer user_data)
{
	account_state_info_free((AccountStateInfo *)data);
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

/* UI-specific functions */

static GtkWidget *
create_gtk_combo_box(GList *initial_strings)
{
	GtkWidget *combo_box = NULL;
	GtkListStore *store = NULL;
	GtkTreeIter iter;
	GtkCellRenderer *renderer = NULL;
	GList *item = NULL;

	store = gtk_list_store_new(1, G_TYPE_STRING);
	for (item = g_list_first(initial_strings); item != NULL; item = g_list_next(item))
	{
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, (gchar *)item->data, -1);
	}

	combo_box = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
	renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo_box), renderer, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo_box), renderer, "text", 0, NULL);
	return combo_box;
}

static void
gtk_combo_box_locate_iter(GtkTreeModel *model, const gchar *string, GtkTreeIter *iter)
{
	gchar *text = NULL;
	gboolean end_search = FALSE;

	if (gtk_tree_model_get_iter_first(model, iter))
	{
		do
		{
			gtk_tree_model_get(model, iter, 0, &text, -1);
			end_search = g_strcmp0(text, string) == 0;
			g_free(text);
		}
		while (!end_search && gtk_tree_model_iter_next(model, iter));
	}
}

static void
gtk_combo_box_select_string(GtkWidget *combo_box, const gchar *s)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo_box));
	gtk_combo_box_locate_iter(model, s, &iter);
	gtk_combo_box_set_active_iter(GTK_COMBO_BOX(combo_box), &iter);
}

static void
gtk_combo_box_add_string(GtkWidget *combo_box, gchar *string)
{
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;

	model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo_box));
	if (model != NULL)
	{
		gtk_list_store_append(GTK_LIST_STORE(model), &iter);
		gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, string, -1);
	}
}

static void
gtk_combo_box_remove_string(GtkWidget *combo_box, const gchar *string)
{
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;

	model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo_box));
	gtk_combo_box_locate_iter(model, string, &iter);
	gtk_list_store_remove(GTK_LIST_STORE(combo_box), &iter);
}

/*** End of UI-specific functions ***/

static void
plugin_action_configure_cb (PurplePluginAction * action)
{
  location_configure_dialog_create();
  location_configure_dialog_show();
  location_configure_dialog_destroy();
}

static void
plugin_action_configure_accounts_by_location_cb(PurplePluginAction *action)
{
	gchar **fields = NULL;
	GList *asis = NULL,
		  *item = NULL;
	AccountStateInfo *asi = NULL;

	fields = g_strsplit(action->label, ": ", -1);
	asis = locations_model_lookup_accounts(*(fields+1));

	for (item = g_list_first(asis); item != NULL; item = g_list_next(item))
	{
		asi = (AccountStateInfo *)item->data;
		purple_account_set_enabled(asi->account, PIDGIN_UI, asi->enabled);
	}

	purple_prefs_set_string(PREF_LAST_LOCATION, *(fields+1));

	g_strfreev(fields);
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
	GtkTreeIter iter;
	GtkTreeModel *model = NULL;
	gboolean value = FALSE;

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
	GList *asis = NULL,
		  *cur_accounts = NULL,
		  *account_item = NULL;
	gchar *name = NULL;

	LocationConfigurationDialog *configure_dialog = NULL;

	configure_dialog = (LocationConfigurationDialog *)data;

	name = location_configure_dialog_get_new_location_name(configure_dialog->dialog);
	if (name == NULL || strlen(name) == 0)
		return;

	/* Add new location to locations model */
	cur_accounts = purple_accounts_get_all();
	account_item = g_list_first(cur_accounts);
	for (; account_item != NULL; account_item = g_list_next(account_item))
	{
		asis = g_list_append(asis,
			   account_state_info_new(
				   (PurpleAccount *)account_item->data,
				   purple_account_get_enabled(
					   (PurpleAccount *)account_item->data,
					   PIDGIN_UI)));
	}
	locations_model_add_location(name, asis);

	/* Add new location name to locations list */
	gtk_combo_box_add_string(configure_dialog->cboLocations, name);

	/* Select the new location, and the account list will auto-refresh after selecting. */
	gtk_combo_box_select_string(configure_dialog->cboLocations, name);
}

/*
 * Update locations model using the model of accounts tree view.
 * If there is any new purple account created after saving the
 * location, add it to the locations model.
 */
static void
save_clicked_handler(GtkButton *button, gpointer data)
{
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	gchar *loc_name = NULL,
		  *username = NULL,
		  *protocol_id = NULL;
	LocationConfigurationDialog *configure_dialog = NULL;
	gboolean enabled = FALSE,
			 update_item_found = FALSE;
	GList *update_asis = NULL,
		  *item = NULL;
	AccountStateInfo *asi = NULL;

	configure_dialog = (LocationConfigurationDialog *)data;
	model = gtk_combo_box_get_model(GTK_COMBO_BOX(configure_dialog->cboLocations));
	gtk_combo_box_get_active_iter(GTK_COMBO_BOX(configure_dialog->cboLocations), &iter);
	gtk_tree_model_get(model, &iter, 0, &loc_name, -1);

	update_asis = locations_model_lookup_accounts(loc_name);
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(configure_dialog->tvAccounts));

	if (gtk_tree_model_get_iter_first(model, &iter))
	{
		do
		{
			gtk_tree_model_get(model, &iter, 0, &enabled, 1, &username, 2, &protocol_id, -1);

			for (item = g_list_first(update_asis); item != NULL; item = g_list_next(item))
			{
				asi = (AccountStateInfo *)item->data;
				update_item_found =
					g_strcmp0(username, purple_account_get_username(asi->account)) == 0 &&
					g_strcmp0(protocol_id, purple_account_get_protocol_id(asi->account)) == 0;
				if (update_item_found)
					break;
			}

			if (update_item_found)
			{
				asi->enabled = enabled;
			}
			else
			{
				asi = g_new0(AccountStateInfo, 1);
				asi->enabled = enabled;
				asi->account = purple_accounts_find(username, protocol_id);
				/* Put the AccountStateInfo list back to the locations model with new Purple account. */
				locations_model_add_location(loc_name, g_list_append(update_asis, asi));
			}

			g_free(username);
			g_free(protocol_id);
		}
		while (gtk_tree_model_iter_next(model, &iter));
	}

	g_free(loc_name);
}

static void
delete_clicked_handler(GtkButton *button, gpointer data)
{
	LocationConfigurationDialog *configure_dialog = NULL;
	GtkWidget *msg_dialog = NULL;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	gchar *name = NULL;

	configure_dialog = (LocationConfigurationDialog *)data;
	model = gtk_combo_box_get_model(GTK_COMBO_BOX(configure_dialog->cboLocations));
	gtk_combo_box_get_active_iter(GTK_COMBO_BOX(configure_dialog->cboLocations), &iter);
	gtk_tree_model_get(model, &iter, 0, &name, -1);

	msg_dialog = gtk_message_dialog_new(
			GTK_WINDOW(configure_dialog->dialog),
			GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_QUESTION,
			GTK_BUTTONS_YES_NO,
			"Are you sure to delete location %s", name);

	if (gtk_dialog_run(GTK_DIALOG(msg_dialog)) == GTK_RESPONSE_YES)
	{

		locations_model_delete_location(name);
		gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
		gtk_tree_view_set_model(GTK_TREE_VIEW(configure_dialog->tvAccounts), NULL);
	}

	g_free(name);
	name = NULL;
	gtk_widget_destroy(msg_dialog);
}

static void
cboLocations_changed_handler(GtkComboBox *sender, gpointer data)
{
	LocationConfigurationDialog *configure_dialog = NULL;
	gboolean selected = FALSE;
	GtkListStore *store = NULL;
	GtkTreeIter iter;
	GtkTreeModel *model = NULL;
	gchar *location_name = NULL;
	GList *ls = NULL,
		  *item = NULL;
	AccountStateInfo *asi = NULL;

	configure_dialog = (LocationConfigurationDialog *)data;
	selected = gtk_combo_box_get_active(sender) > -1;

	gtk_widget_set_sensitive(configure_dialog->btnSave, selected);
	gtk_widget_set_sensitive(configure_dialog->btnDelete, selected);

	if (!selected) return;

	model = gtk_combo_box_get_model(sender);
	gtk_combo_box_get_active_iter(sender, &iter);
	gtk_tree_model_get(model, &iter, 0, &location_name, -1);

	store = gtk_list_store_new(3, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING);
	ls = locations_model_lookup_accounts(location_name);
	for (item = g_list_first(ls); item != NULL; item = g_list_next(item))
	{
		asi = (AccountStateInfo *)item->data;
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter,
				0, asi->enabled,
				1, purple_account_get_username(asi->account),
				2, purple_account_get_protocol_id(asi->account),
				-1);
	}
	gtk_tree_view_set_model(
			GTK_TREE_VIEW(configure_dialog->tvAccounts),
		   	GTK_TREE_MODEL(store));

	g_free(location_name);
}

/******* end of signal handlers *******/

static void
location_configure_dialog_create()
{
	GtkWidget *content_area = NULL,
			  *label = NULL,
			  *hbox = NULL;
	GtkWidget *scrolled_win = NULL;
	GtkCellRenderer *renderer = NULL;
	GtkTreeViewColumn *column = NULL;
	int width, height;

	configure_dialog = g_new0(LocationConfigurationDialog, 1);

	configure_dialog->tvAccounts = gtk_tree_view_new();

	renderer = gtk_cell_renderer_toggle_new();
	g_object_set(renderer, "activatable", TRUE, NULL);
	g_signal_connect(G_OBJECT(renderer), "toggled", G_CALLBACK(account_status_toggled), configure_dialog->tvAccounts);
	column = gtk_tree_view_column_new_with_attributes("Enabled", renderer, "active", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(configure_dialog->tvAccounts), column);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Account", renderer, "text", 1, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(configure_dialog->tvAccounts), column);

	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(configure_dialog->tvAccounts));

	scrolled_win = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_win), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scrolled_win), configure_dialog->tvAccounts);

	label = gtk_label_new("Location:");
	configure_dialog->cboLocations = create_gtk_combo_box(
			locations_model_get_locations_names());
	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), configure_dialog->cboLocations, TRUE, TRUE, 0);
	g_signal_connect(
			G_OBJECT(configure_dialog->cboLocations), "changed",
			G_CALLBACK(cboLocations_changed_handler), configure_dialog);

	width  = purple_prefs_get_int("/pidgin/accounts/dialog/width");
	height = purple_prefs_get_int("/pidgin/accounts/dialog/height");

	/* Create the main dialog UI */
	configure_dialog->dialog = gtk_dialog_new();
	gtk_window_set_title(GTK_WINDOW(configure_dialog->dialog), "Location Configuration");
	gtk_window_set_modal(GTK_WINDOW(configure_dialog->dialog), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(configure_dialog->dialog), width, height);

	content_area = gtk_dialog_get_content_area(GTK_DIALOG(configure_dialog->dialog));
	gtk_box_pack_start(GTK_BOX(content_area), hbox, FALSE, TRUE, 3);
	gtk_box_pack_start(GTK_BOX(content_area), scrolled_win, TRUE, TRUE, 3);

	hbox = gtk_dialog_get_action_area(GTK_DIALOG(configure_dialog->dialog));
	configure_dialog->btnAdd = gtk_button_new_from_stock(GTK_STOCK_NEW);
	g_signal_connect(
			G_OBJECT(configure_dialog->btnAdd), "clicked",
		   	G_CALLBACK(add_clicked_handler), configure_dialog);
	gtk_box_pack_start(GTK_BOX(hbox), configure_dialog->btnAdd, TRUE, TRUE, 0);

	configure_dialog->btnSave = gtk_button_new_from_stock(GTK_STOCK_SAVE);
	gtk_widget_set_sensitive(configure_dialog->btnSave, FALSE);
	g_signal_connect(
			G_OBJECT(configure_dialog->btnSave), "clicked",
		   	G_CALLBACK(save_clicked_handler), configure_dialog);
	gtk_box_pack_start(GTK_BOX(hbox), configure_dialog->btnSave, TRUE, TRUE, 0);

	configure_dialog->btnDelete = gtk_button_new_from_stock(GTK_STOCK_DELETE);
	gtk_widget_set_sensitive(configure_dialog->btnDelete, FALSE);
	g_signal_connect(
			G_OBJECT(configure_dialog->btnDelete), "clicked",
		   	G_CALLBACK(delete_clicked_handler), configure_dialog);
	gtk_box_pack_start(GTK_BOX(hbox), configure_dialog->btnDelete, TRUE, TRUE, 0);

	gtk_dialog_add_button(
			GTK_DIALOG(configure_dialog->dialog),
		   	GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);

	gtk_widget_show_all(configure_dialog->dialog);
	gtk_dialog_run(GTK_DIALOG(configure_dialog->dialog));
	gtk_widget_destroy(configure_dialog->dialog);
}

static void
location_configure_dialog_destroy()
{
	if (configure_dialog != NULL)
	{
		gtk_widget_destroy(configure_dialog->dialog);
		g_free(configure_dialog);
		configure_dialog = NULL;
	}
}

static void
location_configure_dialog_show()
{
	gtk_dialog_run(GTK_DIALOG(configure_dialog->dialog));
}

static void
get_new_location_name_dialog_ok_clicked(GtkWidget *sender, gpointer data)
{
	NewLocationNameInputDialog *input_dialog = NULL;
	gchar *name = NULL,
		  *sp = NULL;

	input_dialog = (NewLocationNameInputDialog *)data;
	name = g_strdup(gtk_entry_get_text(GTK_ENTRY(input_dialog->name_entry)));
	for (sp = name; *sp; ++sp)
	{
		if (!(*sp >= 'a' && *sp <= 'z' ||
			*sp >= 'A' && *sp <= 'Z' ||
			*sp >= '0' && *sp <= '9' ||
			*sp == '-' || *sp == '_' || *sp == ' '))
		{
			g_free(name);

			sp = g_strdup_printf(
					"<span foreground=\"red\" font-weight=\"bold\">%s</span>",
					LOCATION_NAME_TIP);
			gtk_label_set_markup(GTK_LABEL(input_dialog->tip), sp);
			g_free(sp);
			return;
		}
	}

	gtk_dialog_response(GTK_DIALOG(input_dialog->dialog), GTK_RESPONSE_OK);
}

static gchar *
location_configure_dialog_get_new_location_name(GtkWidget *parent)
{
	NewLocationNameInputDialog *input_dialog = NULL;
	GtkWidget *dialog = NULL,
			  *label = NULL,
			  *name_entry = NULL,
			  *box = NULL,
			  *button = NULL;
	gchar *name = NULL,
		  *sp = NULL;
	GtkResponseType dialog_result = 0;

	input_dialog = g_new0(NewLocationNameInputDialog, 1);
	input_dialog->dialog = gtk_dialog_new_with_buttons(
			"Location Name",
			GTK_WINDOW(parent),
			GTK_DIALOG_MODAL,
			NULL);

	box = gtk_dialog_get_content_area(GTK_DIALOG(input_dialog->dialog));
	input_dialog->prompt = gtk_label_new("Please enter a new location name here.");
	gtk_box_pack_start(GTK_BOX(box), input_dialog->prompt, TRUE, TRUE, 0);

	input_dialog->name_entry = gtk_entry_new_with_max_length(LOCATION_NAME_MAX_LENGTH);
	gtk_box_pack_start(GTK_BOX(box), input_dialog->name_entry, TRUE, TRUE, 0);

	input_dialog->tip = gtk_label_new(LOCATION_NAME_TIP);
	gtk_label_set_line_wrap(GTK_LABEL(input_dialog->tip), TRUE);
	gtk_box_pack_start(GTK_BOX(box), input_dialog->tip, TRUE, TRUE, 0);

	box = gtk_dialog_get_action_area(GTK_DIALOG(input_dialog->dialog));
	button = gtk_button_new_from_stock(GTK_STOCK_OK);
	gtk_box_pack_start(GTK_BOX(box), button, TRUE, TRUE, 0);
	/* Validating the new location name */
	g_signal_connect(G_OBJECT(button), "clicked",
			G_CALLBACK(get_new_location_name_dialog_ok_clicked), input_dialog);

	gtk_dialog_add_button(GTK_DIALOG(input_dialog->dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	gtk_widget_show_all(input_dialog->dialog);

	dialog_result = gtk_dialog_run(GTK_DIALOG(input_dialog->dialog));
	if (dialog_result == GTK_RESPONSE_OK)
	{
		name = g_strdup(gtk_entry_get_text(GTK_ENTRY(input_dialog->name_entry)));
	}

	gtk_widget_destroy(input_dialog->dialog);
	g_free(input_dialog);
	return name;
}

static gboolean
plugin_load (PurplePlugin * plugin)
{
	purple_prefs_add_none(PREF_LOCATIONS);

	locations_model_load();

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
	return TRUE;
}

static PurplePluginInfo info =
{
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,
	NULL,
	0,
	NULL,
	PURPLE_PRIORITY_DEFAULT,

	"gtk-tkdchen-locations",
	"Locations",
	DISPLAY_VERSION,

	"Allows you to enable and disable accounts just by selecting a predefined location.",
	"When you have several accounts, and only part of them can be used in some place. "
	"It might be more convenience to predefine the accounts' usability by location, "
	"and then just select the specific location from Locations' menu.",
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

PURPLE_INIT_PLUGIN (locations, init_plugin, info)
