#include "../../access/access.h"
#include "maage/log.h"
#include "maage/time.h"
#include <gtk/gtk.h>
#include <string.h>
typedef struct maage_access_gtk {
    maage_access *access;
    maage_access_snapshot snapshot;
    GtkWidget *window, *profiles, *identity, *status, *spinner, *connect, *stored, *disconnect, *rescan, *next, *cancel, *panel, *device, *copy, *open, *stop, *signout;
    unsigned revision;
    int result, closing, updating, manage_accounts;
} maage_access_gtk;
static GtkWidget *maage_access_gtk_label(GtkWidget *box, const char *text, const char *style) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 76);
    if (style) gtk_style_context_add_class(gtk_widget_get_style_context(label), style);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    return label;
}
static gboolean maage_access_gtk_tick(gpointer context) {
    maage_access_gtk *ui = context;
    const char *selected = gtk_combo_box_get_active_id(GTK_COMBO_BOX(ui->profiles));
    char uuid[33] = "", text[1024];
    maage_access_profile *p = NULL;
    unsigned i;
    int idle;
    if (ui->updating) return G_SOURCE_CONTINUE;
    if (selected) g_strlcpy(uuid, selected, sizeof(uuid));
    maage_access_snapshot_get(ui->access, &ui->snapshot);
    if (ui->closing && !ui->snapshot.busy) {
        gtk_main_quit();
        return G_SOURCE_CONTINUE;
    }
    if (!ui->manage_accounts && ui->snapshot.auto_continue && ui->snapshot.allowed && !ui->snapshot.busy) {
        ui->result = 1;
        gtk_main_quit();
        return G_SOURCE_CONTINUE;
    }
    if (ui->revision != ui->snapshot.revision) {
        ui->updating = 1;
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(ui->profiles));
        for (i = 0; i < ui->snapshot.count; ++i) {
            p = &ui->snapshot.profiles[i];
            g_snprintf(text, sizeof(text), "%s — %s", p->name, maage_access_state_name(p->state));
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->profiles), p->uuid, text);
        }
        if (!ui->snapshot.count) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->profiles), "", "No connected accounts");
        if (!*uuid || !gtk_combo_box_set_active_id(GTK_COMBO_BOX(ui->profiles), uuid)) gtk_combo_box_set_active(GTK_COMBO_BOX(ui->profiles), 0);
        ui->updating = 0;
        ui->revision = ui->snapshot.revision;
    }
    selected = gtk_combo_box_get_active_id(GTK_COMBO_BOX(ui->profiles));
    p = NULL;
    for (i = 0; selected && i < ui->snapshot.count; ++i)
        if (!strcmp(selected, ui->snapshot.profiles[i].uuid)) {
            p = &ui->snapshot.profiles[i];
            break;
        }
    if (p)
        g_snprintf(text, sizeof(text), "%s\n%s · %s\n%s", p->uuid, *p->sources ? p->sources : "Source unavailable", maage_access_state_name(p->state), p->detail);
    else
        text[0] = 0;
    gtk_label_set_text(GTK_LABEL(ui->identity), text);
    gtk_label_set_text(GTK_LABEL(ui->status), ui->snapshot.status);
    idle = !ui->snapshot.busy && !ui->closing;
    gtk_widget_set_sensitive(ui->profiles, idle && ui->snapshot.count);
    gtk_widget_set_sensitive(ui->connect, idle);
    gtk_widget_set_sensitive(ui->stored, idle && p && p->session_saved);
    gtk_widget_set_sensitive(ui->disconnect, idle && p && p->connected);
    gtk_widget_set_sensitive(ui->rescan, idle);
    gtk_widget_set_sensitive(ui->signout, idle && p && p->session_saved);
    gtk_widget_set_sensitive(ui->stop, ui->snapshot.busy && !ui->closing);
    gtk_widget_set_visible(ui->panel, p != NULL);
    gtk_widget_set_visible(ui->copy, *ui->snapshot.device_code != 0);
    gtk_widget_set_visible(ui->open, *ui->snapshot.device_code != 0);
    if (*ui->snapshot.device_code) {
        double seconds = ui->snapshot.device_deadline - maage_time_monotonic_seconds();
        g_snprintf(text, sizeof(text), "%s\nCode: %s · %.0f seconds remaining", ui->snapshot.device_url, ui->snapshot.device_code, seconds > 0 ? seconds : 0);
    } else
        g_strlcpy(text, "Your browser will open to complete sign-in.", sizeof(text));
    gtk_label_set_text(GTK_LABEL(ui->device), text);
    gtk_widget_set_sensitive(ui->next, idle && ui->snapshot.allowed);
    gtk_widget_set_sensitive(ui->cancel, !ui->closing);
    if (ui->snapshot.busy)
        gtk_spinner_start(GTK_SPINNER(ui->spinner));
    else
        gtk_spinner_stop(GTK_SPINNER(ui->spinner));
    if (!gtk_widget_get_visible(ui->window)) gtk_widget_show(ui->window);
    return G_SOURCE_CONTINUE;
}
static void maage_access_gtk_select(GtkWidget *widget, gpointer context) {
    (void) widget;
    maage_access_gtk_tick(context);
}
static void maage_access_gtk_cancel(GtkWidget *widget, gpointer context) {
    maage_access_gtk *ui = context;
    (void) widget;
    ui->closing = 1;
    maage_access_cancel(ui->access);
    maage_access_gtk_tick(ui);
}
static gboolean maage_access_gtk_close(GtkWidget *widget, GdkEvent *event, gpointer context) {
    (void) event;
    maage_access_gtk_cancel(widget, context);
    return TRUE;
}
static void maage_access_gtk_next(GtkWidget *widget, gpointer context) {
    maage_access_gtk *ui = context;
    (void) widget;
    if (!ui->snapshot.busy && maage_access_allowed(ui->access)) {
        ui->result = 1;
        gtk_main_quit();
    }
}
static void maage_access_gtk_connect(GtkWidget *widget, gpointer context) {
    maage_access_gtk *ui = context;
    (void) widget;
    maage_access_sign_in(ui->access, 0);
    maage_access_gtk_tick(ui);
}
static void maage_access_gtk_fallback(GtkWidget *widget, gpointer context) {
    maage_access_gtk *ui = context;
    (void) widget;
    maage_access_sign_in(ui->access, 1);
    maage_access_gtk_tick(ui);
}
static void maage_access_gtk_stop(GtkWidget *widget, gpointer context) {
    maage_access_gtk *ui = context;
    (void) widget;
    maage_access_cancel_operation(ui->access);
}
static void maage_access_gtk_copy(GtkWidget *widget, gpointer context) {
    maage_access_gtk *ui = context;
    (void) widget;
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), ui->snapshot.device_code, -1);
}
static void maage_access_gtk_open(GtkWidget *widget, gpointer context) {
    maage_access_gtk *ui = context;
    (void) widget;
    if (*ui->snapshot.device_url) g_app_info_launch_default_for_uri(ui->snapshot.device_url, NULL, NULL);
}
static void maage_access_gtk_signout(GtkWidget *widget, gpointer context) {
    maage_access_gtk *ui = context;
    const char *uuid = gtk_combo_box_get_active_id(GTK_COMBO_BOX(ui->profiles));
    (void) widget;
    if (uuid && *uuid) maage_access_sign_out(ui->access, uuid);
    maage_access_gtk_tick(ui);
}
static void maage_access_gtk_saved(GtkWidget *widget, gpointer context) {
    maage_access_gtk *ui = context;
    const char *selected = gtk_combo_box_get_active_id(GTK_COMBO_BOX(ui->profiles));
    (void) widget;
    if (selected && *selected) maage_access_verify_local(ui->access, selected);
    maage_access_gtk_tick(ui);
}
static void maage_access_gtk_disconnect(GtkWidget *widget, gpointer context) {
    maage_access_gtk *ui = context;
    const char *selected = gtk_combo_box_get_active_id(GTK_COMBO_BOX(ui->profiles));
    (void) widget;
    if (selected && *selected) maage_access_disconnect(ui->access, selected);
    maage_access_gtk_tick(ui);
}
static GtkWidget *maage_access_gtk_button(GtkWidget *box, const char *text, GCallback callback, maage_access_gtk *ui) {
    GtkWidget *button = gtk_button_new_with_label(text);
    gtk_box_pack_end(GTK_BOX(box), button, FALSE, FALSE, 0);
    if (callback) g_signal_connect(button, "clicked", callback, ui);
    return button;
}
maage_status maage_access_native_run(maage_access *access, int manage_accounts, int *action) {
    maage_access_gtk ui;
    GtkWidget *outer, *header, *panel, *row, *label;
    GtkCssProvider *css;
    guint timer;
    maage_status outcome = maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    if (!action) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    *action = 0;
    memset(&ui, 0, sizeof(ui));
    ui.access = access;
    ui.manage_accounts = manage_accounts;
    ui.revision = (unsigned) -1;
    if (!gtk_init_check(NULL, NULL)) {
        maage_log_error("maage.launcher.ui", "GTK cannot connect to a desktop display.");
        return maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    }
    gtk_window_set_default_icon_name(MAAGE_LAUNCHER_ICON_NAME);
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);
    css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
                                    "window.maage-access { background: #1c1c1c; color: #e6e6e6; } .maage-access label { color: #e6e6e6; }"
                                    ".maage-access .muted { color: #ababab; font-size: 12px; } .maage-access .heading { font-size: 25px; font-weight: 600; color: #e84aac; }"
                                    ".maage-access .panel { background: #292929; border-radius: 8px; padding: 18px; } .maage-access .accent { background: #ad257b; }",
                                    -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    ui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.window), "maage-access");
    gtk_window_set_title(GTK_WINDOW(ui.window), "MaaGE Launcher — Initial setup");
    gtk_window_set_default_size(GTK_WINDOW(ui.window), 650, 610);
    gtk_window_set_resizable(GTK_WINDOW(ui.window), FALSE);
    gtk_window_set_position(GTK_WINDOW(ui.window), GTK_WIN_POS_CENTER);
    outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 28);
    gtk_container_add(GTK_CONTAINER(ui.window), outer);
    header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(outer), header, FALSE, FALSE, 0);
    maage_access_gtk_label(header, "MaaGE Launcher", NULL);
    label = gtk_label_new("Initial setup · 1 of 2");
    gtk_box_pack_end(GTK_BOX(header), label, FALSE, FALSE, 0);
    maage_access_gtk_label(outer, "✓ Verify Minecraft Access", "heading");
    maage_access_gtk_label(outer, "Sign in with your Microsoft account to verify access to Minecraft: Java Edition.", "muted");
    ui.panel = panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(panel), "panel");
    gtk_box_pack_start(GTK_BOX(outer), panel, FALSE, FALSE, 0);
    ui.profiles = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(panel), ui.profiles, FALSE, FALSE, 0);
    ui.identity = maage_access_gtk_label(panel, "", "muted");
    gtk_widget_set_size_request(ui.identity, -1, 90);
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(panel), row, FALSE, FALSE, 0);
    ui.disconnect = maage_access_gtk_button(row, "Remove", G_CALLBACK(maage_access_gtk_disconnect), &ui);
    ui.stored = maage_access_gtk_button(row, "Refresh / Reverify", G_CALLBACK(maage_access_gtk_saved), &ui);
    ui.signout = maage_access_gtk_button(row, "Sign Out", G_CALLBACK(maage_access_gtk_signout), &ui);
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(outer), row, FALSE, FALSE, 0);
    ui.connect = maage_access_gtk_button(row, "Sign in with Microsoft", G_CALLBACK(maage_access_gtk_connect), &ui);
    gtk_widget_set_hexpand(ui.connect, TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.connect), "accent");
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(outer), row, FALSE, FALSE, 0);
    ui.rescan = maage_access_gtk_button(row, "Use Device Code", G_CALLBACK(maage_access_gtk_fallback), &ui);
    ui.stop = maage_access_gtk_button(row, "Cancel Sign-in", G_CALLBACK(maage_access_gtk_stop), &ui);
    ui.spinner = gtk_spinner_new();
    gtk_box_pack_start(GTK_BOX(row), ui.spinner, FALSE, FALSE, 0);
    ui.device = maage_access_gtk_label(outer, "", "muted");
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(outer), row, FALSE, FALSE, 0);
    ui.copy = maage_access_gtk_button(row, "Copy Code", G_CALLBACK(maage_access_gtk_copy), &ui);
    ui.open = maage_access_gtk_button(row, "Open Browser", G_CALLBACK(maage_access_gtk_open), &ui);
    ui.status = maage_access_gtk_label(outer, "", "muted");
    gtk_widget_set_size_request(ui.status, -1, 56);
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_end(GTK_BOX(outer), row, FALSE, FALSE, 0);
    ui.next = maage_access_gtk_button(row, "Continue", G_CALLBACK(maage_access_gtk_next), &ui);
    ui.cancel = maage_access_gtk_button(row, "Cancel", G_CALLBACK(maage_access_gtk_cancel), &ui);
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.next), "accent");
    g_signal_connect(ui.profiles, "changed", G_CALLBACK(maage_access_gtk_select), &ui);
    g_signal_connect(ui.window, "delete-event", G_CALLBACK(maage_access_gtk_close), &ui);
    gtk_widget_set_sensitive(ui.next, FALSE);
    timer = g_timeout_add(100, maage_access_gtk_tick, &ui);
    gtk_widget_show_all(outer);
    maage_access_gtk_tick(&ui);
    gtk_main();
    g_source_remove(timer);
    gtk_widget_destroy(ui.window);
    maage_access_snapshot_release(&ui.snapshot);
    gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css));
    g_object_unref(css);
    *action = ui.result;
    return outcome;
}
