#include "../setup.h"
#include "maage/log.h"
#include <gtk/gtk.h>
#include <string.h>
typedef struct maage_setup_gtk {
    maage_assets *assets;
    GtkWidget *window, *heading, *subtitle, *directories, *metadata, *resources, *client, *reconstruction, *completion, *percent, *progress, *count, *detail, *next, *cancel;
    int result, closing;
} maage_setup_gtk;
static GtkWidget *maage_setup_gtk_label(GtkWidget *box, const char *text, const char *style) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    if (style) gtk_style_context_add_class(gtk_widget_get_style_context(label), style);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    return label;
}
static void maage_setup_gtk_cancel(GtkWidget *widget, gpointer context) {
    maage_setup_gtk *ui = (maage_setup_gtk *) context;
    (void) widget;
    ui->closing = 1;
    maage_assets_cancel(ui->assets);
    gtk_widget_set_sensitive(ui->cancel, FALSE);
    gtk_widget_set_sensitive(ui->next, FALSE);
}
static gboolean maage_setup_gtk_close(GtkWidget *widget, GdkEvent *event, gpointer context) {
    (void) event;
    maage_setup_gtk_cancel(widget, context);
    return TRUE;
}
static void maage_setup_gtk_next(GtkWidget *widget, gpointer context) {
    maage_setup_gtk *ui = (maage_setup_gtk *) context;
    maage_setup_progress p;
    (void) widget;
    maage_assets_snapshot(ui->assets, &p);
    if (!maage_assets_access_allowed(ui->assets) && (p.stage == MAAGE_SETUP_FAILED || p.stage == MAAGE_SETUP_COMPLETE)) {
        ui->result = 2;
        gtk_main_quit();
    } else if (p.stage == MAAGE_SETUP_COMPLETE && maage_assets_access_allowed(ui->assets)) {
        ui->result = 1;
        gtk_main_quit();
    } else if (p.stage == MAAGE_SETUP_FAILED)
        maage_assets_retry(ui->assets);
}
static gboolean maage_setup_gtk_tick(gpointer context) {
    maage_setup_gtk *ui = (maage_setup_gtk *) context;
    maage_setup_progress p;
    maage_setup_text t;
    char percent[16];
    maage_assets_snapshot(ui->assets, &p);
    maage_setup_describe(&p, &t);
    if (p.stage == MAAGE_SETUP_COMPLETE && !maage_assets_access_allowed(ui->assets)) {
        p.stage = MAAGE_SETUP_FAILED;
        p.needs_setup = 1;
        strcpy(p.error, "Minecraft access expired. Choose Verify Access and verify again. Existing resources are preserved.");
        maage_setup_describe(&p, &t);
    }
    if (ui->closing && (p.stage == MAAGE_SETUP_CANCELLED || p.stage == MAAGE_SETUP_FAILED || p.stage == MAAGE_SETUP_COMPLETE)) {
        gtk_main_quit();
        return G_SOURCE_CONTINUE;
    }
    if (t.complete && !p.needs_setup && maage_assets_access_allowed(ui->assets)) {
        ui->result = 1;
        gtk_main_quit();
        return G_SOURCE_CONTINUE;
    }
    gtk_label_set_text(GTK_LABEL(ui->heading), t.heading);
    gtk_label_set_text(GTK_LABEL(ui->subtitle), t.subtitle);
    gtk_label_set_text(GTK_LABEL(ui->directories), t.directories);
    gtk_label_set_text(GTK_LABEL(ui->metadata), t.metadata);
    gtk_label_set_text(GTK_LABEL(ui->resources), t.resources);
    gtk_label_set_text(GTK_LABEL(ui->client), t.client);
    gtk_label_set_text(GTK_LABEL(ui->reconstruction), t.reconstruction);
    gtk_label_set_text(GTK_LABEL(ui->completion), t.completion);
    gtk_label_set_text(GTK_LABEL(ui->count), t.count);
    gtk_label_set_text(GTK_LABEL(ui->detail), t.detail);
    if (p.percent < 0) {
        percent[0] = 0;
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(ui->progress));
    } else {
        g_snprintf(percent, sizeof(percent), "%d%%", p.percent);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->progress), p.percent / 100.0);
    }
    gtk_label_set_text(GTK_LABEL(ui->percent), percent);
    if (t.failed && !maage_assets_access_allowed(ui->assets)) t.button = "Verify Access";
    gtk_button_set_label(GTK_BUTTON(ui->next), t.button);
    gtk_widget_set_sensitive(ui->next, t.actionable && !ui->closing && (!t.complete || maage_assets_access_allowed(ui->assets)));
    if (!gtk_widget_get_visible(ui->window) && (p.needs_setup || t.failed)) gtk_widget_show(ui->window);
    return G_SOURCE_CONTINUE;
}
maage_status maage_setup_native_run(maage_assets *assets, int *action) {
    maage_setup_gtk ui;
    GtkWidget *outer, *header, *intro, *titles, *panel, *row, *buttons, *label;
    GtkCssProvider *css;
    guint timer;
    maage_status outcome = maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    if (!action) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    *action = 0;
    memset(&ui, 0, sizeof(ui));
    ui.assets = assets;
    if (!gtk_init_check(NULL, NULL)) {
        maage_log_error("maage.launcher.ui", "GTK cannot connect to a desktop display.");
        return maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_LAUNCHER, MAAGE_OPERATION_OPEN, 0);
    }
    gtk_window_set_default_icon_name(MAAGE_LAUNCHER_ICON_NAME);
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);
    css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
                                    "window.maage-setup { background: #1c1c1c; color: #e6e6e6; }"
                                    ".maage-setup label { color: #e6e6e6; } .maage-setup .muted { color: #ababab; font-size: 12px; }"
                                    ".maage-setup .heading { font-size: 25px; font-weight: 600; }"
                                    ".maage-setup .panel { background: #292929; border-radius: 8px; padding: 20px; }"
                                    ".maage-setup progressbar trough { background: #3d3d3d; min-height: 8px; border: 0; }"
                                    ".maage-setup progressbar progress { background: #de309e; min-height: 8px; border: 0; }"
                                    ".maage-setup .download-icon { font-size: 32px; color: #e84aac; }",
                                    -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    ui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.window), "maage-setup");
    gtk_window_set_title(GTK_WINDOW(ui.window), "MaaGE Launcher — Initial setup");
    gtk_window_set_default_size(GTK_WINDOW(ui.window), 620, 561);
    gtk_window_set_resizable(GTK_WINDOW(ui.window), FALSE);
    gtk_window_set_position(GTK_WINDOW(ui.window), GTK_WIN_POS_CENTER);
    outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 28);
    gtk_container_add(GTK_CONTAINER(ui.window), outer);
    header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(outer), header, FALSE, FALSE, 0);
    maage_setup_gtk_label(header, "MaaGE Launcher", NULL);
    label = gtk_label_new("Initial setup");
    gtk_style_context_add_class(gtk_widget_get_style_context(label), "muted");
    gtk_box_pack_end(GTK_BOX(header), label, FALSE, FALSE, 0);
    intro = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_pack_start(GTK_BOX(outer), intro, FALSE, FALSE, 0);
    maage_setup_gtk_label(intro, "↓", "download-icon");
    titles = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_pack_start(GTK_BOX(intro), titles, TRUE, TRUE, 0);
    ui.heading = maage_setup_gtk_label(titles, "Preparing MaaGE", "heading");
    ui.subtitle = maage_setup_gtk_label(titles, "Setting up the resources needed for your first launch.", "muted");
    panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_style_context_add_class(gtk_widget_get_style_context(panel), "panel");
    gtk_box_pack_start(GTK_BOX(outer), panel, FALSE, FALSE, 0);
    ui.directories = maage_setup_gtk_label(panel, "", NULL);
    ui.metadata = maage_setup_gtk_label(panel, "", NULL);
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(row, 10);
    gtk_box_pack_start(GTK_BOX(panel), row, FALSE, FALSE, 0);
    ui.resources = maage_setup_gtk_label(row, "", NULL);
    ui.client = maage_setup_gtk_label(panel, "", NULL);
    ui.reconstruction = maage_setup_gtk_label(panel, "", NULL);
    ui.completion = maage_setup_gtk_label(panel, "", NULL);
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(panel), row, FALSE, FALSE, 0);
    ui.progress = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(row), ui.progress, TRUE, TRUE, 0);
    ui.percent = gtk_label_new("");
    gtk_box_pack_end(GTK_BOX(row), ui.percent, FALSE, FALSE, 0);
    atk_object_set_name(gtk_widget_get_accessible(ui.progress), "Resource setup progress");
    ui.count = maage_setup_gtk_label(panel, "", "muted");
    ui.detail = maage_setup_gtk_label(outer, "", "muted");
    gtk_label_set_max_width_chars(GTK_LABEL(ui.detail), 70);
    gtk_widget_set_size_request(ui.detail, -1, 54);
    buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(buttons), 10);
    gtk_box_pack_end(GTK_BOX(outer), buttons, FALSE, FALSE, 0);
    ui.cancel = gtk_button_new_with_label("Cancel");
    ui.next = gtk_button_new_with_label("Continue");
    gtk_container_add(GTK_CONTAINER(buttons), ui.cancel);
    gtk_container_add(GTK_CONTAINER(buttons), ui.next);
    gtk_widget_set_sensitive(ui.next, FALSE);
    gtk_widget_set_can_default(ui.next, TRUE);
    gtk_widget_grab_default(ui.next);
    g_signal_connect(ui.cancel, "clicked", G_CALLBACK(maage_setup_gtk_cancel), &ui);
    g_signal_connect(ui.next, "clicked", G_CALLBACK(maage_setup_gtk_next), &ui);
    g_signal_connect(ui.window, "delete-event", G_CALLBACK(maage_setup_gtk_close), &ui);
    timer = g_timeout_add(100, maage_setup_gtk_tick, &ui);
    gtk_widget_show_all(outer);
    gtk_main();
    g_source_remove(timer);
    gtk_widget_destroy(ui.window);
    gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css));
    g_object_unref(css);
    *action = ui.result;
    return outcome;
}
