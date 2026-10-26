#include "launcher.h"
#include "maage/launcher_version.h"
#include "maage/log.h"
#include <gtk/gtk.h>
#include <string.h>
typedef struct maage_launcher_gtk {
    maage_launcher_ui *state;
    GtkWidget *window, *stack, *navigation, *account, *installation, *list, *status, *resource, *account_status, *verify, *repair, *error, *clients, *client_status, *play;
    GtkWidget *actions[6];
    GtkCssProvider *css;
    unsigned revision, client_revision;
    int result, updating;
} maage_launcher_gtk;
static GtkWidget *maage_ui_label(GtkWidget *box, const char *text, const char *style)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0); gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    if (style) gtk_style_context_add_class(gtk_widget_get_style_context(label), style);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0); return label;
}
static GtkWidget *maage_ui_button(GtkWidget *box, const char *text, GCallback callback, maage_launcher_gtk *ui)
{
    GtkWidget *button = gtk_button_new_with_label(text); gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    if (callback) g_signal_connect(button, "clicked", callback, ui); return button;
}
static void maage_ui_manage(GtkWidget *widget, gpointer context)
{ maage_launcher_gtk *ui = context; (void)widget; if (!ui->state->accounts.busy) { ui->result = 1; gtk_main_quit(); } }
static void maage_ui_repair(GtkWidget *widget, gpointer context)
{ maage_launcher_gtk *ui = context; (void)widget; if (!ui->state->accounts.busy) { ui->result = maage_access_allowed(ui->state->access) ? 2 : 1; gtk_main_quit(); } }
static void maage_ui_verify(GtkWidget *widget, gpointer context)
{ maage_launcher_gtk *ui = context; (void)widget; if (ui->state->account[0]) maage_access_verify_local(ui->state->access, ui->state->account); }
static void maage_ui_play(GtkWidget *widget, gpointer context)
{ maage_launcher_gtk *ui = context; (void)widget; maage_launcher_client_play(ui->state); }
static void maage_ui_project(GtkWidget *widget, gpointer context)
{
    maage_launcher_gtk *ui = context; GError *error = NULL; (void)widget;
    if (!gtk_show_uri_on_window(GTK_WINDOW(ui->window), MAAGE_PROJECT_URL, GDK_CURRENT_TIME, &error)) {
        g_strlcpy(ui->state->error, error->message, sizeof(ui->state->error)); g_error_free(error);
    }
}
static void maage_ui_page(GtkListBox *list, GtkListBoxRow *row, gpointer context)
{
    maage_launcher_gtk *ui = context; const char *pages[] = {"play", "news", "installations"}; int index;
    (void)list; if (!row) return; index = gtk_list_box_row_get_index(row);
    ui->state->page = (maage_launcher_page)index; gtk_stack_set_visible_child_name(GTK_STACK(ui->stack), pages[index]);
}
static void maage_ui_settings(GtkWidget *widget, gpointer context)
{
    maage_launcher_gtk *ui = context; (void)widget;
    ui->state->page = MAAGE_PAGE_SETTINGS;
    gtk_list_box_unselect_all(GTK_LIST_BOX(ui->navigation));
    gtk_stack_set_visible_child_name(GTK_STACK(ui->stack), "settings");
}
static void maage_ui_about(GtkWidget *widget, gpointer context)
{
    maage_launcher_gtk *ui = context; (void)widget;
    ui->state->page = MAAGE_PAGE_ABOUT;
    gtk_list_box_unselect_all(GTK_LIST_BOX(ui->navigation));
    gtk_stack_set_visible_child_name(GTK_STACK(ui->stack), "about");
}
static int maage_ui_row(maage_launcher_gtk *ui)
{
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(ui->list));
    return row ? gtk_list_box_row_get_index(row) : -1;
}
static void maage_ui_installation_open(GtkWidget *widget, gpointer context)
{
    maage_launcher_gtk *ui = context; int row = maage_ui_row(ui);
    char path[MAAGE_PATH_CAPACITY]; gchar *uri; GError *error = NULL; (void)widget;
    if (row < 0 || maage_status_failed(maage_launcher_installation_directory(ui->state, (unsigned)row, path))) return;
    uri = g_filename_to_uri(path, NULL, &error);
    if (!uri || !gtk_show_uri_on_window(GTK_WINDOW(ui->window), uri, GDK_CURRENT_TIME, &error))
        g_strlcpy(ui->state->error, error ? error->message : "Cannot open the installation folder.", sizeof(ui->state->error));
    if (error) g_error_free(error);
    g_free(uri);
}
static void maage_ui_installations_reload(maage_launcher_gtk *ui)
{
    GList *children, *p; unsigned i;
    ui->updating = 1;
    children = gtk_container_get_children(GTK_CONTAINER(ui->list));
    for (p = children; p; p = p->next) gtk_widget_destroy(GTK_WIDGET(p->data));
    g_list_free(children);
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(ui->installation));
    for (i = 0; i < ui->state->count; ++i) {
        maage_installation *item = &ui->state->installations[i];
        GtkWidget *line = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5); char title[160];
        g_snprintf(title, sizeof(title), "%s%s%s", item->name, item->id == ui->state->default_id ? " · Default" : "", item->id == ui->state->selected ? " · Selected" : "");
        gtk_container_set_border_width(GTK_CONTAINER(line), 12); maage_ui_label(line, title, NULL);
        maage_ui_label(line, maage_launcher_installation_client(ui->state, i), "muted"); gtk_list_box_insert(GTK_LIST_BOX(ui->list), line, -1);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui->installation), item->name);
        if (item->id == ui->state->selected) gtk_combo_box_set_active(GTK_COMBO_BOX(ui->installation), (int)i);
    }
    gtk_widget_show_all(ui->list); ui->updating = 0;
}
static void maage_ui_installation_changed(GtkComboBox *box, gpointer context)
{
    maage_launcher_gtk *ui = context;
    if (!ui->updating) { maage_launcher_installation_select(ui->state, (unsigned)gtk_combo_box_get_active(box)); maage_ui_installations_reload(ui); }
}
static void maage_ui_account_changed(GtkComboBox *box, gpointer context)
{
    maage_launcher_gtk *ui = context; const char *uuid;
    if (ui->updating) return; uuid = gtk_combo_box_get_active_id(box);
    if (uuid && !strcmp(uuid, "manage")) maage_ui_manage(NULL, ui);
    else if (uuid && !strcmp(uuid, "verify")) maage_ui_verify(NULL, ui);
    else if (uuid && !strcmp(uuid, "add")) { if ((!maage_status_failed(maage_access_sign_in(ui->state->access, 0)))) { ui->result = 1; gtk_main_quit(); } }
    else if (uuid && !strcmp(uuid, "signout")) maage_access_sign_out(ui->state->access, ui->state->account);
    else if (uuid && !strcmp(uuid, "remove")) maage_access_disconnect(ui->state->access, ui->state->account);
    else if (uuid) maage_launcher_account_select(ui->state, uuid);
    ui->revision = 0;
}
static void maage_ui_installation_action(GtkButton *button, gpointer context)
{
    maage_launcher_gtk *ui = context; int action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "action"));
    int row = maage_ui_row(ui); GtkWidget *dialog, *entry, *area, *client, *arguments, *editor_error; char name[128];
    if (action && row < 0) return;
    if (action == 4) { maage_launcher_installation_default(ui->state, (unsigned)row); maage_ui_installations_reload(ui); return; }
    if (action == 3) {
        dialog = gtk_message_dialog_new(GTK_WINDOW(ui->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL, "Remove this MaaGE instance?");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "Its gameplay folder will be archived, preserving worlds and private files. Shared resources and accounts are preserved.");
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) maage_launcher_installation_remove(ui->state, (unsigned)row);
    } else {
        const char *titles[] = {"New Installation", "Edit Installation", "Duplicate Installation"};
        dialog = gtk_dialog_new_with_buttons(titles[action], GTK_WINDOW(ui->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_OK, NULL);
        area = gtk_dialog_get_content_area(GTK_DIALOG(dialog)); gtk_container_set_border_width(GTK_CONTAINER(area), 20); gtk_box_set_spacing(GTK_BOX(area), 12);
        maage_ui_label(area, "MaaGE configuration name · Minecraft 1.7.10 baseline", NULL);
        entry = gtk_entry_new(); gtk_entry_set_max_length(GTK_ENTRY(entry), 95);
        g_snprintf(name, sizeof(name), "%s%s", action ? ui->state->installations[row].name : "New Installation", action == 2 ? " Copy" : "");
        gtk_entry_set_text(GTK_ENTRY(entry), name); gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 0);
        atk_object_set_name(gtk_widget_get_accessible(entry), "Installation name");
        client = gtk_combo_box_text_new(); gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(client), "", "Client selection required");
        gtk_combo_box_set_active(GTK_COMBO_BOX(client), 0);
        for (unsigned i = 0; i < ui->state->clients->count; ++i) {
            char label[384]; maage_client_build *build = &ui->state->clients->builds[i]; maage_client_build_label(build, label);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(client), build->id, label);
        }
        if (action && *ui->state->installations[row].client) {
            const char *id = ui->state->installations[row].client;
            if (maage_clients_find(ui->state->clients, id) < 0) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(client), id, "Referenced Client missing (keep reference)");
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(client), id);
        }
        atk_object_set_name(gtk_widget_get_accessible(client), "Client build"); gtk_box_pack_start(GTK_BOX(area), client, FALSE, FALSE, 0);
        maage_ui_label(area, "Client CLI arguments", NULL);
        arguments = gtk_entry_new(); gtk_entry_set_max_length(GTK_ENTRY(arguments), MAAGE_INSTANCE_ARGUMENTS_CAPACITY - 1);
        gtk_entry_set_text(GTK_ENTRY(arguments), action ? ui->state->installations[row].arguments : "");
        atk_object_set_name(gtk_widget_get_accessible(arguments), "Client CLI arguments");
        gtk_entry_set_placeholder_text(GTK_ENTRY(arguments), "Separate arguments with spaces; quote values containing spaces.");
        gtk_box_pack_start(GTK_BOX(area), arguments, FALSE, FALSE, 0);
        editor_error = gtk_label_new(""); gtk_label_set_line_wrap(GTK_LABEL(editor_error), TRUE);
        gtk_box_pack_start(GTK_BOX(area), editor_error, FALSE, FALSE, 0);
        gtk_entry_set_activates_default(GTK_ENTRY(arguments), TRUE);
        gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE); gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK); gtk_widget_show_all(area);
        while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
            if (!maage_status_failed(maage_launcher_installation_write(ui->state, action == 1 ? row : -1,
                    gtk_entry_get_text(GTK_ENTRY(entry)), gtk_combo_box_get_active_id(GTK_COMBO_BOX(client)),
                    gtk_entry_get_text(GTK_ENTRY(arguments))))) break;
            gtk_label_set_text(GTK_LABEL(editor_error), ui->state->error);
        }
    }
    gtk_widget_destroy(dialog); maage_ui_installations_reload(ui);
}
static void maage_ui_appearance_apply(maage_launcher_gtk *ui)
{
    if (ui->state->appearance) g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", ui->state->appearance == 2, NULL);
    else gtk_settings_reset_property(gtk_settings_get_default(), "gtk-application-prefer-dark-theme");
}
static void maage_ui_appearance(GtkComboBox *box, gpointer context)
{
    maage_launcher_gtk *ui = context; int previous = ui->state->appearance;
    if (ui->updating) return;
    ui->state->appearance = gtk_combo_box_get_active(box);
    if (maage_status_failed(maage_launcher_ui_save(ui->state))) { ui->state->appearance = previous; ui->updating = 1; gtk_combo_box_set_active(box, previous); ui->updating = 0; }
    maage_ui_appearance_apply(ui);
}
static gboolean maage_ui_tick(gpointer context)
{
    maage_launcher_gtk *ui = context; unsigned i; maage_launcher_ui *state = ui->state;
    maage_launcher_ui_refresh(state);
    if (ui->revision != state->accounts.revision || !gtk_combo_box_get_active_id(GTK_COMBO_BOX(ui->account))) {
        int connected = 0;
        ui->updating = 1; gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(ui->account));
        for (i = 0; i < state->accounts.count; ++i) {
            maage_access_profile *p = &state->accounts.profiles[i]; char title[160]; if (!p->connected) continue;
            g_snprintf(title, sizeof(title), "%s · %s", p->name, maage_access_state_name(p->state));
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->account), p->uuid, title);
            ++connected;
        }
        if (!connected) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->account), "none", "No connected Minecraft account");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->account), "add", "Add Microsoft Account…");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->account), "manage", "Manage Microsoft Accounts…");
        if (state->account[0]) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->account), "verify", "Refresh / Reverify Account");
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->account), "signout", "Sign Out");
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->account), "remove", "Remove Account");
        }
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(ui->account), state->account); ui->updating = 0; ui->revision = state->accounts.revision;
        if (!connected) { ui->updating = 1; gtk_combo_box_set_active(GTK_COMBO_BOX(ui->account), 0); ui->updating = 0; }
    }
    if (ui->client_revision != state->client_revision) {
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(ui->clients));
        for (i = 0; i < state->clients->count; ++i) {
            char label[384]; maage_client_build_label(&state->clients->builds[i], label);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->clients), state->clients->builds[i].id, label);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(ui->clients), 0);
        ui->client_revision = state->client_revision; maage_ui_installations_reload(ui);
    }
#if MAAGE_LAUNCHER_DEVELOPMENT
    if (state->client_confirmation) {
        GtkWidget *dialog; maage_client_build *build = &state->client_candidate.build;
        state->client_confirmation = 0;
        dialog = gtk_message_dialog_new(GTK_WINDOW(ui->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL, "%s", *state->replacing_client ? "Replace Client?" : "Import Local Client?");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s + %s\n%s · %s · %s\n%s\n\nOnly import code you trust. The whole package is copied and the original is preserved.%s", build->version, build->build, build->build_type, build->platform, build->architecture, state->client_candidate.source, *state->replacing_client ? "\n\nAll installations using the selected Client will switch to this build, then the old managed package will be removed." : "");
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) maage_launcher_client_import(state);
        else maage_launcher_client_cancel_import(state);
        gtk_widget_destroy(dialog);
    }
#endif
    gtk_label_set_text(GTK_LABEL(ui->client_status), state->client_status);
    gtk_widget_set_sensitive(ui->play, state->can_play);
    gtk_button_set_label(GTK_BUTTON(ui->play), state->client_process ? "Client Running" : "▶  Play");
    gtk_widget_set_sensitive(ui->account, !state->accounts.busy); gtk_widget_set_sensitive(ui->verify, !state->accounts.busy && state->account[0]);
    gtk_widget_set_sensitive(ui->repair, !state->accounts.busy);
    gtk_button_set_label(GTK_BUTTON(ui->status), state->readiness);
    gtk_button_set_label(GTK_BUTTON(ui->repair), state->accounts.allowed ? "Repair Assets…" : "Verify Minecraft Access…");
    gtk_label_set_text(GTK_LABEL(ui->resource), state->progress.stage == MAAGE_SETUP_FAILED ? state->progress.error : state->readiness);
    gtk_label_set_text(GTK_LABEL(ui->account_status), state->account_status); gtk_label_set_text(GTK_LABEL(ui->error), state->error);
    for (i = 0; i < 6; ++i) gtk_widget_set_sensitive(ui->actions[i], i == 0 || maage_ui_row(ui) >= 0);
    return G_SOURCE_CONTINUE;
}
static gboolean maage_ui_close(GtkWidget *widget, GdkEvent *event, gpointer context)
{ maage_launcher_gtk *ui = context; (void)widget; (void)event; if (!ui->state->client_job) gtk_main_quit(); return TRUE; }
#if MAAGE_LAUNCHER_DEVELOPMENT
static void maage_ui_client_import(GtkWidget *widget, gpointer context)
{
    maage_launcher_gtk *ui = context; GtkWidget *dialog;
    int replacing = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "replace")), index = -1;
    const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(ui->clients));
    if (id) index = maage_clients_find(ui->state->clients, id);
    if (ui->state->client_job || (replacing && index < 0)) return;
    dialog = gtk_file_chooser_dialog_new(replacing ? "Replace Client Executable" : "Import Local Client Executable", GTK_WINDOW(ui->window), GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL, "Inspect", GTK_RESPONSE_ACCEPT, NULL);
    { GtkFileFilter *filter = gtk_file_filter_new(); gtk_file_filter_set_name(filter, "MaaGE Client (maage-client)");
      gtk_file_filter_add_pattern(filter, "maage-client"); gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter); }
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (path) {
            if (replacing) maage_launcher_client_replace(ui->state, (unsigned)index, path);
            else maage_launcher_client_probe(ui->state, path);
            g_free(path);
        }
    }
    gtk_widget_destroy(dialog);
}
#endif
static void maage_ui_client_remove(GtkWidget *widget, gpointer context)
{
    maage_launcher_gtk *ui = context; const char *id; int index; GtkWidget *dialog; (void)widget;
    if (ui->state->client_job) return;
    id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(ui->clients));
    if (!id || (index = maage_clients_find(ui->state->clients, id)) < 0) return;
    dialog = gtk_message_dialog_new(GTK_WINDOW(ui->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL, "Remove managed Client?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "Only unused local Client packages are removed. Shared Minecraft resources and developer originals are preserved.");
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) maage_launcher_client_remove(ui->state, (unsigned)index);
    gtk_widget_destroy(dialog);
}
maage_status maage_launcher_native_run(maage_launcher_ui *state, int *action)
{
    maage_launcher_gtk ui; GtkWidget *split, *sidebar, *navigation, *right, *header, *page, *center, *button, *scroll, *footer, *appearance, *actions;
    const char *names[] = {"Play", "News", "Installations"}, *icons[] = {"media-playback-start-symbolic", "text-x-generic-symbolic", "view-list-symbolic"};
    guint timer; int i; const char *pages[] = {"play", "news", "installations", "settings", "about"};
    maage_status outcome = maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    if (!action) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    *action = 0;
    memset(&ui, 0, sizeof(ui)); ui.state = state;
    if (!gtk_init_check(NULL, NULL)) { maage_log_error("maage.launcher.ui", "GTK cannot connect to a desktop display."); return maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0); }
    /* Setup requests dark temporarily; main Launcher restores the desktop preference. */
    gtk_settings_reset_property(gtk_settings_get_default(), "gtk-application-prefer-dark-theme");
    maage_ui_appearance_apply(&ui);
    ui.css = gtk_css_provider_new(); gtk_css_provider_load_from_data(ui.css,
        ".maage-launcher .heading { font-size: 24px; font-weight: 600; } .maage-launcher .brand { font-size: 32px; font-weight: 600; }"
        ".maage-launcher .muted { opacity: .68; } .maage-launcher .primary { background: #de309e; color: white; min-height: 38px; font-size: 18px; }"
        ".maage-launcher .primary:disabled { background: #873663; color: #ddd; } .maage-launcher .sidebar row { border-radius: 5px; padding: 8px; }"
        ".maage-launcher .section { font-size: 16px; font-weight: 600; } .maage-launcher .about-panel { padding: 20px; border-radius: 8px; }", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(ui.css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    ui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL); gtk_window_set_title(GTK_WINDOW(ui.window), "MaaGE Launcher"); gtk_window_set_default_size(GTK_WINDOW(ui.window), 1040, 690);
    gtk_widget_set_size_request(ui.window, 960, 640); gtk_style_context_add_class(gtk_widget_get_style_context(ui.window), "maage-launcher");
    split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL); gtk_container_add(GTK_CONTAINER(ui.window), split);
    sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20); gtk_container_set_border_width(GTK_CONTAINER(sidebar), 14); gtk_widget_set_size_request(sidebar, 170, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(sidebar), "sidebar"); gtk_paned_pack1(GTK_PANED(split), sidebar, FALSE, FALSE);
    maage_ui_label(sidebar, "MAAGE", "muted"); ui.navigation = navigation = gtk_list_box_new(); gtk_list_box_set_selection_mode(GTK_LIST_BOX(navigation), GTK_SELECTION_SINGLE); gtk_box_pack_start(GTK_BOX(sidebar), navigation, FALSE, FALSE, 0);
    for (i = 0; i < 3; ++i) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10); gtk_box_pack_start(GTK_BOX(row), gtk_image_new_from_icon_name(icons[i], GTK_ICON_SIZE_MENU), FALSE, FALSE, 0); maage_ui_label(row, names[i], NULL); gtk_list_box_insert(GTK_LIST_BOX(navigation), row, -1);
    }
    actions = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6); gtk_box_pack_end(GTK_BOX(sidebar), actions, FALSE, FALSE, 0);
    button = gtk_button_new_from_icon_name("preferences-system-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_label(GTK_BUTTON(button), "Settings"); gtk_box_pack_start(GTK_BOX(actions), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", G_CALLBACK(maage_ui_settings), &ui);
    button = gtk_button_new_from_icon_name("dialog-information-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_label(GTK_BUTTON(button), "About MaaGE"); gtk_box_pack_start(GTK_BOX(actions), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", G_CALLBACK(maage_ui_about), &ui);
    right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14); gtk_container_set_border_width(GTK_CONTAINER(right), 26); gtk_paned_pack2(GTK_PANED(split), right, TRUE, FALSE);
    ui.stack = gtk_stack_new(); gtk_stack_set_transition_type(GTK_STACK(ui.stack), GTK_STACK_TRANSITION_TYPE_NONE); gtk_box_pack_start(GTK_BOX(right), ui.stack, TRUE, TRUE, 0);
    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16); gtk_stack_add_named(GTK_STACK(ui.stack), page, "play");
    header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10); gtk_box_pack_start(GTK_BOX(page), header, FALSE, FALSE, 0); maage_ui_label(header, "Play MaaGE", "heading");
    button = maage_ui_button(header, "Manage…", G_CALLBACK(maage_ui_manage), &ui); gtk_widget_set_halign(button, GTK_ALIGN_END);
    ui.account = gtk_combo_box_text_new(); gtk_widget_set_hexpand(ui.account, TRUE); gtk_widget_set_halign(ui.account, GTK_ALIGN_END); gtk_box_pack_end(GTK_BOX(header), ui.account, FALSE, FALSE, 0);
    atk_object_set_name(gtk_widget_get_accessible(ui.account), "Active Minecraft account");
    center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14); gtk_widget_set_valign(center, GTK_ALIGN_CENTER); gtk_widget_set_halign(center, GTK_ALIGN_CENTER); gtk_box_pack_start(GTK_BOX(page), center, TRUE, TRUE, 0);
    maage_ui_label(center, "MaaGE", "brand"); maage_ui_label(center, "Minecraft as a Game Engine", "muted");
    ui.installation = gtk_combo_box_text_new(); gtk_widget_set_size_request(ui.installation, 260, -1); gtk_box_pack_start(GTK_BOX(center), ui.installation, FALSE, FALSE, 0); atk_object_set_name(gtk_widget_get_accessible(ui.installation), "MaaGE installation");
    ui.play = button = maage_ui_button(center, "▶  Play", G_CALLBACK(maage_ui_play), &ui); gtk_style_context_add_class(gtk_widget_get_style_context(button), "primary"); gtk_widget_set_sensitive(button, FALSE); gtk_widget_set_tooltip_text(button, "Start this installation’s selected Client directly.");
    ui.client_status = maage_ui_label(center, "", "muted"); gtk_label_set_max_width_chars(GTK_LABEL(ui.client_status), 70);
    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14); gtk_stack_add_named(GTK_STACK(ui.stack), page, "news"); maage_ui_label(page, "News", "heading"); maage_ui_label(page, "No announcements yet", "section"); maage_ui_label(page, "News will appear here when a source is configured. For now, follow development on GitHub.", "muted"); maage_ui_button(page, "Open MaaGE Project ↗", G_CALLBACK(maage_ui_project), &ui);
    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14); gtk_stack_add_named(GTK_STACK(ui.stack), page, "installations"); maage_ui_label(page, "Installations", "heading"); maage_ui_label(page, "Installed Clients", NULL);
    ui.clients = gtk_combo_box_text_new(); gtk_box_pack_start(GTK_BOX(page), ui.clients, FALSE, FALSE, 0);
    atk_object_set_name(gtk_widget_get_accessible(ui.clients), "Installed Client builds");
    actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8); gtk_box_pack_start(GTK_BOX(page), actions, FALSE, FALSE, 0);
#if MAAGE_LAUNCHER_DEVELOPMENT
    maage_ui_button(actions, "Import Local Client…", G_CALLBACK(maage_ui_client_import), &ui);
    button = maage_ui_button(actions, "Replace Client…", G_CALLBACK(maage_ui_client_import), &ui);
    g_object_set_data(G_OBJECT(button), "replace", GINT_TO_POINTER(1));
#endif
    maage_ui_button(actions, "Remove Client…", G_CALLBACK(maage_ui_client_remove), &ui);
    maage_ui_label(page, "MaaGE Installations · shared Minecraft 1.7.10 resources", "muted");
    scroll = gtk_scrolled_window_new(NULL, NULL); gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0); ui.list = gtk_list_box_new(); gtk_container_add(GTK_CONTAINER(scroll), ui.list);
    actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8); gtk_box_pack_start(GTK_BOX(page), actions, FALSE, FALSE, 0);
    { const char *titles[] = {"New…", "Edit…", "Duplicate…", "Remove…", "Set as Default"}; for (i = 0; i < 5; ++i) { ui.actions[i] = maage_ui_button(actions, titles[i], G_CALLBACK(maage_ui_installation_action), &ui); g_object_set_data(G_OBJECT(ui.actions[i]), "action", GINT_TO_POINTER(i)); } }
    ui.actions[5] = maage_ui_button(page, "Open installation folder", G_CALLBACK(maage_ui_installation_open), &ui);
    gtk_widget_set_halign(ui.actions[5], GTK_ALIGN_START);
    scroll = gtk_scrolled_window_new(NULL, NULL); gtk_stack_add_named(GTK_STACK(ui.stack), scroll, "settings"); page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12); gtk_container_add(GTK_CONTAINER(scroll), page);
    maage_ui_label(page, "Settings", "heading"); maage_ui_label(page, "Launcher", "section"); maage_ui_label(page, "Startup opens Play after account and resource preparation.", "muted");
    ui.account_status = maage_ui_label(page, "", "muted"); gtk_label_set_max_width_chars(GTK_LABEL(ui.account_status), 70);
    actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8); gtk_box_pack_start(GTK_BOX(page), actions, FALSE, FALSE, 0); maage_ui_button(actions, "Manage Minecraft Accounts…", G_CALLBACK(maage_ui_manage), &ui); ui.verify = maage_ui_button(actions, "Refresh / Reverify Account", G_CALLBACK(maage_ui_verify), &ui);
    maage_ui_label(page, "Resources", "section"); button = maage_ui_label(page, state->resources, "muted"); gtk_label_set_selectable(GTK_LABEL(button), TRUE); gtk_label_set_max_width_chars(GTK_LABEL(button), 70);
    ui.resource = maage_ui_label(page, "", NULL); gtk_label_set_max_width_chars(GTK_LABEL(ui.resource), 70); ui.repair = maage_ui_button(page, "Repair Assets…", G_CALLBACK(maage_ui_repair), &ui);
    maage_ui_label(page, "Appearance", "section"); appearance = gtk_combo_box_text_new(); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(appearance), "System"); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(appearance), "Light"); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(appearance), "Dark"); gtk_combo_box_set_active(GTK_COMBO_BOX(appearance), state->appearance); gtk_box_pack_start(GTK_BOX(page), appearance, FALSE, FALSE, 0); g_signal_connect(appearance, "changed", G_CALLBACK(maage_ui_appearance), &ui);
    maage_ui_label(page, "About", "section");
    maage_ui_label(page, "Build information, project details, and open-source notices.", "muted");
    maage_ui_button(page, "About MaaGE…", G_CALLBACK(maage_ui_about), &ui);
    scroll = gtk_scrolled_window_new(NULL, NULL); gtk_stack_add_named(GTK_STACK(ui.stack), scroll, "about");
    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18); gtk_container_set_border_width(GTK_CONTAINER(page), 20);
    gtk_container_add(GTK_CONTAINER(scroll), page);
    maage_ui_label(page, "About MaaGE", "heading");
    center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6); gtk_style_context_add_class(gtk_widget_get_style_context(center), "about-panel");
    gtk_box_pack_start(GTK_BOX(page), center, FALSE, FALSE, 0);
    maage_ui_label(center, "MaaGE", "brand");
    maage_ui_label(center, "Minecraft as a Game Engine", "muted");
    gtk_box_pack_start(GTK_BOX(page), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    maage_ui_label(page, "Launcher build", "section");
    maage_ui_label(page, "Version " MAAGE_LAUNCHER_SEMANTIC_VERSION, NULL);
    button = maage_ui_label(page, "Git revision " MAAGE_LAUNCHER_GIT_HASH, "muted");
    gtk_label_set_selectable(GTK_LABEL(button), TRUE);
    gtk_box_pack_start(GTK_BOX(page), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    maage_ui_label(page, "Open source & attribution", "section");
    maage_ui_label(page, "MaaGE's original source code is licensed under the Apache License, Version 2.0. Minecraft game resources have separate ownership and licensing.", NULL);
    maage_ui_label(page, "MaaGE is an independent project and is not affiliated with or endorsed by Mojang Studios or Microsoft.", "muted");
    actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10); gtk_box_pack_start(GTK_BOX(page), actions, FALSE, FALSE, 0);
    maage_ui_button(actions, "View MaaGE on GitHub ↗", G_CALLBACK(maage_ui_project), &ui);
    maage_ui_button(actions, "Back to Settings", G_CALLBACK(maage_ui_settings), &ui);
    ui.error = maage_ui_label(right, "", NULL); gtk_box_pack_start(GTK_BOX(right), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12); gtk_box_pack_end(GTK_BOX(right), footer, FALSE, FALSE, 0); maage_ui_label(footer, "Development build · " MAAGE_LAUNCHER_VERSION, "muted"); ui.status = maage_ui_button(footer, "", G_CALLBACK(maage_ui_settings), &ui); gtk_widget_set_hexpand(ui.status, TRUE); gtk_widget_set_halign(ui.status, GTK_ALIGN_END); gtk_button_set_relief(GTK_BUTTON(ui.status), GTK_RELIEF_NONE);
    g_signal_connect(navigation, "row-selected", G_CALLBACK(maage_ui_page), &ui); g_signal_connect(ui.installation, "changed", G_CALLBACK(maage_ui_installation_changed), &ui); g_signal_connect(ui.account, "changed", G_CALLBACK(maage_ui_account_changed), &ui); g_signal_connect(ui.window, "delete-event", G_CALLBACK(maage_ui_close), &ui);
    maage_ui_installations_reload(&ui); maage_ui_tick(&ui); gtk_widget_show_all(ui.window);
    if (state->page < MAAGE_PAGE_SETTINGS) gtk_list_box_select_row(GTK_LIST_BOX(navigation), gtk_list_box_get_row_at_index(GTK_LIST_BOX(navigation), (int)state->page));
    gtk_stack_set_visible_child_name(GTK_STACK(ui.stack), pages[state->page]); timer = g_timeout_add(250, maage_ui_tick, &ui); gtk_main();
    g_source_remove(timer); gtk_widget_destroy(ui.window); gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(ui.css)); g_object_unref(ui.css); *action = ui.result; return outcome;
}
