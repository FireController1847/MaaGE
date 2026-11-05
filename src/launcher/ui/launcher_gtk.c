#include "launcher.h"
#include "maage/launcher_version.h"
#include "maage/log.h"
#include "maage/time.h"
#include <gtk/gtk.h>
#include <string.h>
typedef struct maage_launcher_gtk {
    maage_launcher_ui *state;
    GtkWidget *window, *stack, *navigation, *account, *installation, *list, *status, *resource, *account_status, *verify, *repair, *error, *clients, *client_status, *play;
    GtkWidget *actions[7], *auth_actions[7], *verify_resources, *retry_resources, *cancel_resources, *profiles, *authentication, *launch_status, *active_summary, *progress, *appearance;
    GtkCssProvider *css;
    unsigned revision, client_revision;
    int updating, displayed_appearance;
} maage_launcher_gtk;
static GtkWidget *maage_ui_label(GtkWidget *box, const char *text, const char *style) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    if (style) gtk_style_context_add_class(gtk_widget_get_style_context(label), style);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    return label;
}
static GtkWidget *maage_ui_button(GtkWidget *box, const char *text, GCallback callback, maage_launcher_gtk *ui) {
    GtkWidget *button = gtk_button_new_with_label(text);
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    if (callback) g_signal_connect(button, "clicked", callback, ui);
    return button;
}
static void maage_ui_manage(GtkWidget *widget, gpointer context) {
    maage_launcher_gtk *ui = context;
    (void) widget;
    gtk_list_box_select_row(GTK_LIST_BOX(ui->navigation), gtk_list_box_get_row_at_index(GTK_LIST_BOX(ui->navigation), MAAGE_PAGE_ACCOUNTS));
}
static void maage_ui_repair(GtkWidget *widget, gpointer context) {
    maage_launcher_gtk *ui = context;
    (void) widget;
    maage_launcher_resources(ui->state, MAAGE_ASSETS_REPAIR);
}
static void maage_ui_resource_action(GtkWidget *widget, gpointer context) {
    maage_launcher_gtk *ui = context;
    int action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "resource-action"));
    if (action == 2) maage_launcher_resources_cancel(ui->state);
    else maage_launcher_resources(ui->state, action == 1 ? ui->state->resource_mode : MAAGE_ASSETS_VERIFY);
}
static void maage_ui_auth_action(GtkWidget *widget, gpointer context) {
    maage_launcher_gtk *ui = context;
    int action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "auth-action"));
    GError *error = NULL;
    if (action < 2) maage_access_sign_in(ui->state->access, action);
    else if (action == 2) maage_access_sign_out(ui->state->access, ui->state->account);
    else if (action == 3) maage_access_disconnect(ui->state->access, ui->state->account);
    else if (action == 4) maage_access_cancel_operation(ui->state->access);
    else if (action == 5 && *ui->state->accounts.device_code) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), ui->state->accounts.device_code, -1);
    else if (action == 6 && *ui->state->accounts.device_url) {
        gtk_show_uri_on_window(GTK_WINDOW(ui->window), ui->state->accounts.device_url, GDK_CURRENT_TIME, &error);
        if (error) g_error_free(error);
    }
}
static void maage_ui_profile_selected(GtkListBox *list, GtkListBoxRow *row, gpointer context) {
    maage_launcher_gtk *ui = context;
    const char *uuid;
    (void) list;
    if (ui->updating || !row) return;
    uuid = g_object_get_data(G_OBJECT(row), "account-uuid");
    if (uuid) maage_launcher_account_select(ui->state, uuid);
}
static void maage_ui_verify(GtkWidget *widget, gpointer context) {
    maage_launcher_gtk *ui = context;
    (void) widget;
    if (!ui->state->accounts.restored) maage_access_restore(ui->state->access);
    else if (ui->state->account[0]) maage_access_verify_local(ui->state->access, ui->state->account);
}
static void maage_ui_play(GtkWidget *widget, gpointer context) {
    maage_launcher_gtk *ui = context;
    (void) widget;
    maage_launcher_client_play(ui->state);
}
static void maage_ui_project(GtkWidget *widget, gpointer context) {
    maage_launcher_gtk *ui = context;
    GError *error = NULL;
    (void) widget;
    if (!gtk_show_uri_on_window(GTK_WINDOW(ui->window), MAAGE_PROJECT_URL, GDK_CURRENT_TIME, &error)) {
        g_strlcpy(ui->state->error, error ? error->message : "Cannot open the MaaGE project page.", sizeof(ui->state->error));
        if (error) g_error_free(error);
    }
}
static void maage_ui_page(GtkListBox *list, GtkListBoxRow *row, gpointer context) {
    maage_launcher_gtk *ui = context;
    const char *pages[] = {"play", "installations", "accounts"};
    int index;
    (void) list;
    if (!row) return;
    index = gtk_list_box_row_get_index(row);
    ui->state->page = (maage_launcher_page) index;
    gtk_stack_set_visible_child_name(GTK_STACK(ui->stack), pages[index]);
}
static void maage_ui_settings(GtkWidget *widget, gpointer context) {
    maage_launcher_gtk *ui = context;
    (void) widget;
    ui->state->page = MAAGE_PAGE_SETTINGS;
    gtk_list_box_unselect_all(GTK_LIST_BOX(ui->navigation));
    gtk_stack_set_visible_child_name(GTK_STACK(ui->stack), "settings");
}
static void maage_ui_about(GtkWidget *widget, gpointer context) {
    maage_launcher_gtk *ui = context;
    (void) widget;
    ui->state->page = MAAGE_PAGE_ABOUT;
    gtk_list_box_unselect_all(GTK_LIST_BOX(ui->navigation));
    gtk_stack_set_visible_child_name(GTK_STACK(ui->stack), "about");
}
static void maage_ui_attention(GtkWidget *widget, gpointer context) {
    maage_launcher_gtk *ui = context;
    maage_launcher_page page = maage_launcher_attention_page(ui->state);
    (void) widget;
    if (page < MAAGE_PAGE_SETTINGS) gtk_list_box_select_row(GTK_LIST_BOX(ui->navigation), gtk_list_box_get_row_at_index(GTK_LIST_BOX(ui->navigation), page));
    else maage_ui_settings(NULL, ui);
}
static int maage_ui_row(maage_launcher_gtk *ui) {
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(ui->list));
    return row ? gtk_list_box_row_get_index(row) : -1;
}
static void maage_ui_installation_open(GtkWidget *widget, gpointer context) {
    maage_launcher_gtk *ui = context;
    int row = maage_ui_row(ui);
    char path[MAAGE_PATH_CAPACITY];
    gchar *uri;
    GError *error = NULL;
    (void) widget;
    if (row < 0 || maage_status_failed(maage_launcher_installation_directory(ui->state, (unsigned) row, path))) return;
    uri = g_filename_to_uri(path, NULL, &error);
    if (!uri || !gtk_show_uri_on_window(GTK_WINDOW(ui->window), uri, GDK_CURRENT_TIME, &error)) g_strlcpy(ui->state->error, error ? error->message : "Cannot open the installation folder.", sizeof(ui->state->error));
    if (error) g_error_free(error);
    g_free(uri);
}
static void maage_ui_installations_reload(maage_launcher_gtk *ui) {
    GList *children, *p;
    unsigned i;
    ui->updating = 1;
    children = gtk_container_get_children(GTK_CONTAINER(ui->list));
    for (p = children; p; p = p->next)
        gtk_widget_destroy(GTK_WIDGET(p->data));
    g_list_free(children);
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(ui->installation));
    for (i = 0; i < ui->state->count; ++i) {
        maage_installation *item = &ui->state->installations[i];
        GtkWidget *line = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        char title[160];
        g_snprintf(title, sizeof(title), "%s%s%s", item->name, item->id == ui->state->default_id ? " · Default" : "", item->id == ui->state->selected ? " · Selected" : "");
        gtk_container_set_border_width(GTK_CONTAINER(line), 12);
        maage_ui_label(line, title, NULL);
        maage_ui_label(line, maage_launcher_installation_client(ui->state, i), "muted");
        gtk_list_box_insert(GTK_LIST_BOX(ui->list), line, -1);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui->installation), item->name);
        if (item->id == ui->state->selected) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(ui->installation), (int) i);
            gtk_list_box_select_row(GTK_LIST_BOX(ui->list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(ui->list), (int) i));
        }
    }
    gtk_widget_show_all(ui->list);
    ui->updating = 0;
}
static void maage_ui_installation_changed(GtkComboBox *box, gpointer context) {
    maage_launcher_gtk *ui = context;
    if (!ui->updating) {
        maage_launcher_installation_select(ui->state, (unsigned) gtk_combo_box_get_active(box));
        maage_ui_installations_reload(ui);
    }
}
static void maage_ui_account_changed(GtkComboBox *box, gpointer context) {
    maage_launcher_gtk *ui = context;
    const char *uuid;
    if (ui->updating) return;
    uuid = gtk_combo_box_get_active_id(box);
    if (uuid && !strcmp(uuid, "manage")) maage_ui_manage(NULL, ui);
    else if (uuid && strcmp(uuid, "none")) maage_launcher_account_select(ui->state, uuid);
    ui->revision = 0;
}
static void maage_ui_installation_action(GtkButton *button, gpointer context) {
    maage_launcher_gtk *ui = context;
    int action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "action"));
    int row = maage_ui_row(ui);
    GtkWidget *dialog, *entry, *area, *client, *arguments, *editor_error;
    char name[128];
    if (!ui->state->catalog_ready || ui->state->client_job || (action && row < 0)) return;
    if (action == 5) {
        maage_launcher_installation_select(ui->state, (unsigned) row);
        maage_ui_installations_reload(ui);
        return;
    }
    if (action == 4) {
        maage_launcher_installation_default(ui->state, (unsigned) row);
        maage_ui_installations_reload(ui);
        return;
    }
    if (action == 3) {
        dialog = gtk_message_dialog_new(GTK_WINDOW(ui->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL, "Remove this MaaGE instance?");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "Its gameplay folder will be archived, preserving worlds and private files. Shared resources and accounts are preserved.");
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) maage_launcher_installation_remove(ui->state, (unsigned) row);
    } else {
        const char *titles[] = {"New Installation", "Edit Installation", "Duplicate Installation"};
        dialog = gtk_dialog_new_with_buttons(titles[action], GTK_WINDOW(ui->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_OK, NULL);
        area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        gtk_container_set_border_width(GTK_CONTAINER(area), 20);
        gtk_box_set_spacing(GTK_BOX(area), 12);
        maage_ui_label(area, "MaaGE configuration name · Minecraft 1.7.10 baseline", NULL);
        entry = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(entry), 95);
        g_snprintf(name, sizeof(name), "%s%s", action ? ui->state->installations[row].name : "New Installation", action == 2 ? " Copy" : "");
        gtk_entry_set_text(GTK_ENTRY(entry), name);
        gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 0);
        atk_object_set_name(gtk_widget_get_accessible(entry), "Installation name");
        client = gtk_combo_box_text_new();
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(client), "", "Client selection required");
        gtk_combo_box_set_active(GTK_COMBO_BOX(client), 0);
        for (unsigned i = 0; i < ui->state->clients->count; ++i) {
            char label[384];
            maage_client_build *build = &ui->state->clients->builds[i];
            maage_client_build_label(build, label);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(client), build->id, label);
        }
        if (action && *ui->state->installations[row].client) {
            const char *id = ui->state->installations[row].client;
            if (maage_clients_find(ui->state->clients, id) < 0) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(client), id, "Referenced Client missing (keep reference)");
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(client), id);
        }
        atk_object_set_name(gtk_widget_get_accessible(client), "Client build");
        gtk_box_pack_start(GTK_BOX(area), client, FALSE, FALSE, 0);
        maage_ui_label(area, "Client CLI arguments", NULL);
        arguments = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(arguments), MAAGE_INSTANCE_ARGUMENTS_CAPACITY - 1);
        gtk_entry_set_text(GTK_ENTRY(arguments), action ? ui->state->installations[row].arguments : "");
        atk_object_set_name(gtk_widget_get_accessible(arguments), "Client CLI arguments");
        gtk_entry_set_placeholder_text(GTK_ENTRY(arguments), "Separate arguments with spaces; quote values containing spaces.");
        gtk_box_pack_start(GTK_BOX(area), arguments, FALSE, FALSE, 0);
        editor_error = gtk_label_new("");
        gtk_label_set_line_wrap(GTK_LABEL(editor_error), TRUE);
        gtk_box_pack_start(GTK_BOX(area), editor_error, FALSE, FALSE, 0);
        gtk_entry_set_activates_default(GTK_ENTRY(arguments), TRUE);
        gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
        gtk_widget_show_all(area);
        while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
            if (!maage_status_failed(maage_launcher_installation_write(ui->state, action == 1 ? row : -1, gtk_entry_get_text(GTK_ENTRY(entry)), gtk_combo_box_get_active_id(GTK_COMBO_BOX(client)), gtk_entry_get_text(GTK_ENTRY(arguments))))) break;
            gtk_label_set_text(GTK_LABEL(editor_error), ui->state->error);
        }
    }
    gtk_widget_destroy(dialog);
    maage_ui_installations_reload(ui);
}
static void maage_ui_appearance_apply(maage_launcher_gtk *ui) {
    if (ui->state->appearance) g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", ui->state->appearance == 2, NULL);
    else
        gtk_settings_reset_property(gtk_settings_get_default(), "gtk-application-prefer-dark-theme");
}
static void maage_ui_appearance(GtkComboBox *box, gpointer context) {
    maage_launcher_gtk *ui = context;
    int previous = ui->state->appearance;
    if (ui->updating) return;
    ui->state->appearance = gtk_combo_box_get_active(box);
    if (maage_status_failed(maage_launcher_ui_save(ui->state))) {
        ui->state->appearance = previous;
        ui->updating = 1;
        gtk_combo_box_set_active(box, previous);
        ui->updating = 0;
    }
    maage_ui_appearance_apply(ui);
}
static gboolean maage_ui_tick(gpointer context) {
    maage_launcher_gtk *ui = context;
    unsigned i;
    maage_launcher_ui *state = ui->state;
    maage_launcher_ui_refresh(state);
    if (ui->displayed_appearance != state->appearance) {
        ui->displayed_appearance = state->appearance;
        ui->updating = 1;
        gtk_combo_box_set_active(GTK_COMBO_BOX(ui->appearance), state->appearance);
        ui->updating = 0;
        maage_ui_appearance_apply(ui);
    }
    gtk_widget_set_sensitive(ui->appearance, state->catalog_ready);
    if (ui->revision != state->accounts.revision || !gtk_combo_box_get_active_id(GTK_COMBO_BOX(ui->account))) {
        int connected = 0;
        ui->updating = 1;
        GList *children = gtk_container_get_children(GTK_CONTAINER(ui->profiles)), *child;
        for (child = children; child; child = child->next)
            gtk_widget_destroy(GTK_WIDGET(child->data));
        g_list_free(children);
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(ui->account));
        for (i = 0; i < state->accounts.count; ++i) {
            maage_access_profile *p = &state->accounts.profiles[i];
            char title[160];
            if (!p->connected) continue;
            g_snprintf(title, sizeof(title), "%s · %s", p->name, maage_access_state_name(p->state));
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->account), p->uuid, title);
            {
                GtkWidget *row = gtk_list_box_row_new(), *line = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
                gtk_container_set_border_width(GTK_CONTAINER(line), 10);
                maage_ui_label(line, p->name, "section");
                maage_ui_label(line, maage_access_state_name(p->state), "muted");
                gtk_container_add(GTK_CONTAINER(row), line);
                g_object_set_data_full(G_OBJECT(row), "account-uuid", g_strdup(p->uuid), g_free);
                gtk_list_box_insert(GTK_LIST_BOX(ui->profiles), row, -1);
                if (!strcmp(state->account, p->uuid)) gtk_list_box_select_row(GTK_LIST_BOX(ui->profiles), GTK_LIST_BOX_ROW(row));
            }
            ++connected;
        }
        if (!connected) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->account), "none", "No connected Minecraft account");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->account), "manage", "Manage Accounts…");
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(ui->account), state->account);
        gtk_widget_show_all(ui->profiles);
        ui->updating = 0;
        ui->revision = state->accounts.revision;
        if (!connected) {
            ui->updating = 1;
            gtk_combo_box_set_active(GTK_COMBO_BOX(ui->account), 0);
            ui->updating = 0;
        }
    }
    if (ui->client_revision != state->client_revision) {
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(ui->clients));
        for (i = 0; i < state->clients->count; ++i) {
            char label[384];
            maage_client_build_label(&state->clients->builds[i], label);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->clients), state->clients->builds[i].id, label);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(ui->clients), 0);
        ui->client_revision = state->client_revision;
        maage_ui_installations_reload(ui);
    }
#if MAAGE_LAUNCHER_DEVELOPMENT
    if (state->client_confirmation) {
        GtkWidget *dialog;
        maage_client_build *build = &state->client_candidate.build;
        state->client_confirmation = 0;
        dialog = gtk_message_dialog_new(GTK_WINDOW(ui->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL, "%s", *state->replacing_client ? "Replace Client?" : "Import Local Client?");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s + %s\n%s · %s · %s\n%s\n\nOnly import code you trust. The whole package is copied and the original is preserved.%s", build->version, build->build, build->build_type, build->platform, build->architecture, state->client_candidate.source, *state->replacing_client ? "\n\nAll installations using the selected Client will switch to this build, then the old managed package will be removed." : "");
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) maage_launcher_client_import(state);
        else
            maage_launcher_client_cancel_import(state);
        gtk_widget_destroy(dialog);
    }
#endif
    gtk_label_set_text(GTK_LABEL(ui->client_status), state->client_status);
    gtk_label_set_text(GTK_LABEL(ui->launch_status), state->launch_status);
    gtk_label_set_text(GTK_LABEL(ui->active_summary), state->active_summary);
    gtk_label_set_text(GTK_LABEL(ui->authentication), state->authentication_status);
    gtk_widget_set_sensitive(ui->play, state->can_play);
    gtk_button_set_label(GTK_BUTTON(ui->play), state->client_process ? "Client Running" : state->launch_pending      ? "Checking Resources…"
                                                                                      : state->client_operation == 4 ? "Starting Client…"
                                                                                                                     : "Play");
    gtk_widget_set_sensitive(ui->account, state->accounts.metadata_ready);
    gtk_widget_set_sensitive(ui->verify, !state->accounts.busy && (*state->account || !state->accounts.restored));
    for (i = 0; i < 7; ++i) {
        int enabled = i < 2 ? state->accounts.restored && !state->accounts.busy : i < 4 ? state->accounts.restored && !state->accounts.busy && *state->account
                                                                                                       : i == 4  ? state->accounts.busy
                                                                                                                 : *state->accounts.device_code && state->accounts.device_deadline > maage_time_monotonic_seconds();
        gtk_widget_set_sensitive(ui->auth_actions[i], enabled);
    }
    gtk_widget_set_sensitive(ui->repair, !state->progress.busy && !state->client_process && state->client_operation != 4 && !state->launch_pending);
    gtk_widget_set_sensitive(ui->verify_resources, gtk_widget_get_sensitive(ui->repair));
    gtk_widget_set_sensitive(ui->retry_resources, gtk_widget_get_sensitive(ui->repair));
    gtk_widget_set_sensitive(ui->cancel_resources, state->progress.busy || state->resources_pending);
    gtk_button_set_label(GTK_BUTTON(ui->status), state->readiness);
    gtk_label_set_text(GTK_LABEL(ui->resource), state->resource_status);
    gtk_label_set_text(GTK_LABEL(ui->account_status), state->account_status);
    gtk_label_set_text(GTK_LABEL(ui->error), state->error);
    if (state->progress.busy || state->accounts.busy) {
        gtk_widget_show(ui->progress);
        if (state->progress.busy && state->progress.percent >= 0) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->progress), state->progress.percent / 100.0);
        else gtk_progress_bar_pulse(GTK_PROGRESS_BAR(ui->progress));
    } else gtk_widget_hide(ui->progress);
    for (i = 0; i < 7; ++i)
        gtk_widget_set_sensitive(ui->actions[i], state->catalog_ready && !state->client_job && (i == 0 || maage_ui_row(ui) >= 0));
    return G_SOURCE_CONTINUE;
}
static gboolean maage_ui_close(GtkWidget *widget, GdkEvent *event, gpointer context) {
    maage_launcher_gtk *ui = context;
    (void) widget;
    (void) event;
    gtk_main_quit();
    return TRUE;
}
#if MAAGE_LAUNCHER_DEVELOPMENT
static void maage_ui_client_import(GtkWidget *widget, gpointer context) {
    maage_launcher_gtk *ui = context;
    GtkWidget *dialog;
    int replacing = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "replace")), index = -1;
    const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(ui->clients));
    if (id) index = maage_clients_find(ui->state->clients, id);
    if (!ui->state->catalog_ready || ui->state->client_job || (replacing && index < 0)) return;
    dialog = gtk_file_chooser_dialog_new(replacing ? "Replace Client Executable" : "Import Local Client Executable", GTK_WINDOW(ui->window), GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL, "Inspect", GTK_RESPONSE_ACCEPT, NULL);
    {
        GtkFileFilter *filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "MaaGE Client (maage-client)");
        gtk_file_filter_add_pattern(filter, "maage-client");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    }
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (path) {
            if (replacing) maage_launcher_client_replace(ui->state, (unsigned) index, path);
            else
                maage_launcher_client_probe(ui->state, path);
            g_free(path);
        }
    }
    gtk_widget_destroy(dialog);
}
#endif
static void maage_ui_client_remove(GtkWidget *widget, gpointer context) {
    maage_launcher_gtk *ui = context;
    const char *id;
    int index;
    GtkWidget *dialog;
    (void) widget;
    if (ui->state->client_job) return;
    id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(ui->clients));
    if (!id || (index = maage_clients_find(ui->state->clients, id)) < 0) return;
    dialog = gtk_message_dialog_new(GTK_WINDOW(ui->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL, "Remove managed Client?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "Only unused local Client packages are removed. Shared Minecraft resources and developer originals are preserved.");
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) maage_launcher_client_remove(ui->state, (unsigned) index);
    gtk_widget_destroy(dialog);
}
static gboolean maage_ui_presented(GtkWidget *widget, GdkEvent *event, gpointer context) {
    maage_launcher_gtk *ui = context;
    (void) widget;
    (void) event;
    maage_launcher_ui_presented(ui->state);
    return FALSE;
}
maage_status maage_launcher_native_run(maage_launcher_ui *state) {
    maage_launcher_gtk ui;
    GtkWidget *split, *sidebar, *right, *header, *page, *form, *button, *scroll, *footer, *actions;
    const char *names[] = {"Play", "Installations", "Accounts"}, *icons[] = {"media-playback-start-symbolic", "view-list-symbolic", "avatar-default-symbolic"};
    const char *pages[] = {"play", "installations", "accounts", "settings", "about"};
    guint timer;
    int i;
    double started = maage_time_monotonic_seconds();
    if (!state) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    memset(&ui, 0, sizeof(ui));
    ui.state = state;
    if (!gtk_init_check(NULL, NULL)) return maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    gtk_window_set_default_icon_name(MAAGE_LAUNCHER_ICON_NAME);
    maage_ui_appearance_apply(&ui);
    ui.css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(ui.css,
                                    ".maage-launcher .heading { font-size: 26px; font-weight: 600; }"
                                    ".maage-launcher .muted { opacity: .72; } .maage-launcher .section { font-size: 15px; font-weight: 600; }"
                                    ".maage-launcher .primary { min-height: 44px; font-size: 20px; font-weight: 600; }"
                                    ".maage-launcher .sidebar row { padding: 10px 8px; border-radius: 6px; }"
                                    ".maage-launcher .sidebar { background-color: @theme_unfocused_bg_color; }",
                                    -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(ui.css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    ui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ui.window), "MaaGE Launcher");
    gtk_window_set_default_size(GTK_WINDOW(ui.window), 1060, 740);
    gtk_widget_set_size_request(ui.window, 900, 660);
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.window), "maage-launcher");
    split = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(ui.window), split);
    sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 22);
    gtk_container_set_border_width(GTK_CONTAINER(sidebar), 16);
    gtk_widget_set_size_request(sidebar, 160, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(sidebar), "sidebar");
    gtk_box_pack_start(GTK_BOX(split), sidebar, FALSE, FALSE, 0);
    maage_ui_label(sidebar, "MaaGE", "heading");
    ui.navigation = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui.navigation), GTK_SELECTION_SINGLE);
    gtk_box_pack_start(GTK_BOX(sidebar), ui.navigation, FALSE, FALSE, 0);
    for (i = 0; i < 3; ++i) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(row), gtk_image_new_from_icon_name(icons[i], GTK_ICON_SIZE_MENU), FALSE, FALSE, 0);
        maage_ui_label(row, names[i], NULL);
        gtk_list_box_insert(GTK_LIST_BOX(ui.navigation), row, -1);
    }
    actions = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_pack_end(GTK_BOX(sidebar), actions, FALSE, FALSE, 0);
    maage_ui_button(actions, "Settings", G_CALLBACK(maage_ui_settings), &ui);
    maage_ui_button(actions, "About", G_CALLBACK(maage_ui_about), &ui);
    right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_container_set_border_width(GTK_CONTAINER(right), 26);
    gtk_box_pack_start(GTK_BOX(split), right, TRUE, TRUE, 0);
    header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_pack_start(GTK_BOX(right), header, FALSE, FALSE, 0);
    ui.active_summary = maage_ui_label(header, "Starting Launcher…", "muted");
    gtk_label_set_ellipsize(GTK_LABEL(ui.active_summary), PANGO_ELLIPSIZE_END);
    gtk_label_set_line_wrap(GTK_LABEL(ui.active_summary), FALSE);
    gtk_widget_set_hexpand(ui.active_summary, TRUE);
    ui.account = gtk_combo_box_text_new();
    gtk_box_pack_end(GTK_BOX(header), ui.account, FALSE, FALSE, 0);
    atk_object_set_name(gtk_widget_get_accessible(ui.account), "Active Minecraft account");
    ui.stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(ui.stack), GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_box_pack_start(GTK_BOX(right), ui.stack, TRUE, TRUE, 0);
    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_stack_add_named(GTK_STACK(ui.stack), page, "play");
    form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_valign(form, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(page), form, TRUE, TRUE, 0);
    maage_ui_label(form, "Play", "heading");
    maage_ui_label(form, "Choose your installation and jump into MaaGE.", "muted");
    maage_ui_label(form, "Installation", "section");
    ui.installation = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(form), ui.installation, FALSE, FALSE, 0);
    atk_object_set_name(gtk_widget_get_accessible(ui.installation), "Installation for Play");
    ui.client_status = maage_ui_label(form, "Loading Client builds…", "muted");
    ui.launch_status = maage_ui_label(form, "Starting Launcher services…", NULL);
    gtk_label_set_max_width_chars(GTK_LABEL(ui.launch_status), 62);
    ui.play = maage_ui_button(form, "Play", G_CALLBACK(maage_ui_play), &ui);
    gtk_widget_set_halign(ui.play, GTK_ALIGN_START);
    gtk_widget_set_size_request(ui.play, 260, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.play), "primary");
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.play), "suggested-action");
    actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(form), actions, FALSE, FALSE, 0);
    maage_ui_button(actions, "Accounts", G_CALLBACK(maage_ui_manage), &ui);
    maage_ui_button(actions, "Resources", G_CALLBACK(maage_ui_settings), &ui);
    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_stack_add_named(GTK_STACK(ui.stack), page, "installations");
    maage_ui_label(page, "Installations", "heading");
    maage_ui_label(page, "Select an installation to manage its name, Client build and launch options.", "muted");
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    ui.list = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scroll), ui.list);
    for (i = 0; i < 6; ++i) {
        const char *titles[] = {"New…", "Edit…", "Duplicate…", "Remove…", "Set as Default", "Use for Play"};
        if (i == 0 || i == 4) {
            actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_box_pack_start(GTK_BOX(page), actions, FALSE, FALSE, 0);
        }
        ui.actions[i] = maage_ui_button(actions, titles[i], G_CALLBACK(maage_ui_installation_action), &ui);
        g_object_set_data(G_OBJECT(ui.actions[i]), "action", GINT_TO_POINTER(i));
    }
    ui.actions[6] = maage_ui_button(actions, "Open Folder", G_CALLBACK(maage_ui_installation_open), &ui);
    maage_ui_label(page, "Client builds", "section");
    ui.clients = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(page), ui.clients, FALSE, FALSE, 0);
    atk_object_set_name(gtk_widget_get_accessible(ui.clients), "Installed Client builds");
    actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(page), actions, FALSE, FALSE, 0);
#if MAAGE_LAUNCHER_DEVELOPMENT
    maage_ui_button(actions, "Import Local Client…", G_CALLBACK(maage_ui_client_import), &ui);
    button = maage_ui_button(actions, "Replace Client…", G_CALLBACK(maage_ui_client_import), &ui);
    g_object_set_data(G_OBJECT(button), "replace", GINT_TO_POINTER(1));
#endif
    maage_ui_button(actions, "Remove Client…", G_CALLBACK(maage_ui_client_remove), &ui);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_stack_add_named(GTK_STACK(ui.stack), scroll, "accounts");
    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_add(GTK_CONTAINER(scroll), page);
    maage_ui_label(page, "Accounts", "heading");
    maage_ui_label(page, "Only verified Minecraft Java Edition access enables Play.", "muted");
    ui.profiles = gtk_list_box_new();
    gtk_box_pack_start(GTK_BOX(page), ui.profiles, FALSE, FALSE, 0);
    ui.account_status = maage_ui_label(page, "", NULL);
    gtk_label_set_max_width_chars(GTK_LABEL(ui.account_status), 65);
    for (i = 0; i < 7; ++i) {
        const char *titles[] = {"Sign In with Microsoft", "Use Device Code", "Sign Out", "Remove Account", "Cancel Sign In", "Copy Code", "Open Microsoft Sign In"};
        if (i == 0 || i == 2 || i == 5) {
            actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_box_pack_start(GTK_BOX(page), actions, FALSE, FALSE, 0);
        }
        ui.auth_actions[i] = maage_ui_button(actions, titles[i], G_CALLBACK(maage_ui_auth_action), &ui);
        g_object_set_data(G_OBJECT(ui.auth_actions[i]), "auth-action", GINT_TO_POINTER(i));
        if (i == 1) ui.verify = maage_ui_button(actions, "Refresh / Retry", G_CALLBACK(maage_ui_verify), &ui);
    }
    ui.authentication = maage_ui_label(page, "", NULL);
    gtk_label_set_selectable(GTK_LABEL(ui.authentication), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(ui.authentication), 65);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_stack_add_named(GTK_STACK(ui.stack), scroll, "settings");
    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_add(GTK_CONTAINER(scroll), page);
    maage_ui_label(page, "Settings", "heading");
    maage_ui_label(page, "Accounts and resources prepare in the background while you use the Launcher.", "muted");
    maage_ui_label(page, "Resources", "section");
    button = maage_ui_label(page, state->resources, "muted");
    gtk_label_set_selectable(GTK_LABEL(button), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(button), 65);
    ui.resource = maage_ui_label(page, "", NULL);
    gtk_label_set_max_width_chars(GTK_LABEL(ui.resource), 65);
    actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(page), actions, FALSE, FALSE, 0);
    ui.verify_resources = maage_ui_button(actions, "Verify Resources", G_CALLBACK(maage_ui_resource_action), &ui);
    ui.repair = maage_ui_button(actions, "Repair Resources", G_CALLBACK(maage_ui_repair), &ui);
    actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(page), actions, FALSE, FALSE, 0);
    ui.retry_resources = maage_ui_button(actions, "Retry", G_CALLBACK(maage_ui_resource_action), &ui);
    g_object_set_data(G_OBJECT(ui.retry_resources), "resource-action", GINT_TO_POINTER(1));
    ui.cancel_resources = maage_ui_button(actions, "Cancel", G_CALLBACK(maage_ui_resource_action), &ui);
    g_object_set_data(G_OBJECT(ui.cancel_resources), "resource-action", GINT_TO_POINTER(2));
    maage_ui_label(page, "Verify checks file contents. Repair also restores missing or damaged resources.", "muted");
    maage_ui_label(page, "Appearance", "section");
    ui.appearance = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.appearance), "System");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.appearance), "Light");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.appearance), "Dark");
    gtk_combo_box_set_active(GTK_COMBO_BOX(ui.appearance), state->appearance);
    gtk_box_pack_start(GTK_BOX(page), ui.appearance, FALSE, FALSE, 0);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_stack_add_named(GTK_STACK(ui.stack), scroll, "about");
    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_container_add(GTK_CONTAINER(scroll), page);
    maage_ui_label(page, "About MaaGE", "heading");
    maage_ui_label(page, "Minecraft as a Game Engine", "section");
    maage_ui_label(page, "Launcher " MAAGE_LAUNCHER_SEMANTIC_VERSION, NULL);
    maage_ui_label(page, "Git revision " MAAGE_LAUNCHER_GIT_HASH, "muted");
    maage_ui_label(page, "MaaGE's original source code is licensed under the Apache License, Version 2.0. Minecraft game resources have separate ownership and licensing.", NULL);
    maage_ui_label(page, "MaaGE is independent and is not affiliated with or endorsed by Mojang Studios or Microsoft.", "muted");
    maage_ui_button(page, "View MaaGE on GitHub ↗", G_CALLBACK(maage_ui_project), &ui);
    ui.progress = gtk_progress_bar_new();
    gtk_widget_set_no_show_all(ui.progress, TRUE);
    atk_object_set_name(gtk_widget_get_accessible(ui.progress), "Background preparation progress");
    gtk_box_pack_start(GTK_BOX(right), ui.progress, FALSE, FALSE, 0);
    ui.error = maage_ui_label(right, "", NULL);
    gtk_label_set_max_width_chars(GTK_LABEL(ui.error), 70);
    footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_end(GTK_BOX(right), footer, FALSE, FALSE, 0);
    maage_ui_label(footer, "Launcher " MAAGE_LAUNCHER_VERSION, "muted");
    ui.status = maage_ui_button(footer, "", G_CALLBACK(maage_ui_attention), &ui);
    gtk_widget_set_hexpand(ui.status, TRUE);
    gtk_widget_set_halign(ui.status, GTK_ALIGN_END);
    gtk_button_set_relief(GTK_BUTTON(ui.status), GTK_RELIEF_NONE);
    g_signal_connect(ui.navigation, "row-selected", G_CALLBACK(maage_ui_page), &ui);
    g_signal_connect(ui.installation, "changed", G_CALLBACK(maage_ui_installation_changed), &ui);
    g_signal_connect(ui.account, "changed", G_CALLBACK(maage_ui_account_changed), &ui);
    g_signal_connect(ui.profiles, "row-selected", G_CALLBACK(maage_ui_profile_selected), &ui);
    g_signal_connect(ui.appearance, "changed", G_CALLBACK(maage_ui_appearance), &ui);
    g_signal_connect(ui.window, "delete-event", G_CALLBACK(maage_ui_close), &ui);
    g_signal_connect(ui.window, "map-event", G_CALLBACK(maage_ui_presented), &ui);
    maage_ui_installations_reload(&ui);
    maage_ui_tick(&ui);
    gtk_widget_show_all(ui.window);
    if (state->page < MAAGE_PAGE_SETTINGS) gtk_list_box_select_row(GTK_LIST_BOX(ui.navigation), gtk_list_box_get_row_at_index(GTK_LIST_BOX(ui.navigation), (int) state->page));
    gtk_stack_set_visible_child_name(GTK_STACK(ui.stack), pages[state->page]);
    MAAGE_LOG_DEBUG_EVENT("maage.launcher.startup", maage_log_debug("maage.launcher.startup", "GTK main window creation/show request: %.3f s.", maage_time_monotonic_seconds() - started));
    timer = g_timeout_add(250, maage_ui_tick, &ui);
    gtk_main();
    g_source_remove(timer);
    gtk_widget_destroy(ui.window);
    gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(ui.css));
    g_object_unref(ui.css);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_OPEN, 0);
}
