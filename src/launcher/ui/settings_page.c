#include "widgets.h"
#include "maage/launcher_version.h"
#include <stdio.h>
#include <string.h>

static void maage_ui_setting_number(maage_launcher_ui *ui, maage_rect row, maage_setting_key key, int percent, int enabled)
{
    const maage_setting_definition *definition = maage_setting_describe(key);
    maage_setting_value value = ui->app->settings.values[key];
    char label[32];
    maage_ui_label(ui, maage_ui_rect(row.x, row.y, row.width - 104, 28), ui->theme.body_size, ui->theme.text, definition->label);
    sprintf(label, percent ? "%.0f%%" : "%.0f MiB", (double)value.data.number * (percent ? 100 : 1));
    if (!percent && value.data.number == 0) strcpy(label, "Automatic");
    maage_ui_label(ui, maage_ui_rect(row.x + row.width - 96, row.y, 96, 28), ui->theme.body_size, ui->theme.muted, label);
    maage_ui_slider(ui, 600 + key, maage_ui_rect(row.x, row.y + 32, row.width, ui->theme.control_height),
        &value.data.number, definition->minimum, definition->maximum, enabled);
    if (enabled) maage_launcher_settings_set(&ui->app->settings, key, &value);
}
static void maage_ui_general(maage_launcher_ui *ui, maage_rect body)
{
    maage_rect area, inside;
    maage_setting_value value = ui->app->settings.values[MAAGE_SETTING_KEEP_OPEN];
    area = maage_ui_scroll(ui, body, &ui->scroll[3], 440);
    inside = maage_ui_panel(ui, maage_ui_rect(area.x, area.y, area.width - 8, 264));
    maage_ui_label(ui, maage_ui_rect(inside.x, inside.y, inside.width, 32), ui->theme.section_size, ui->theme.text, "Appearance");
    maage_ui_label(ui, maage_ui_rect(inside.x, inside.y + 48, inside.width / 2, 28), ui->theme.body_size, ui->theme.muted, "Theme");
    maage_ui_label(ui, maage_ui_rect(inside.x + inside.width / 2, inside.y + 48, inside.width / 2, 28), ui->theme.body_size, ui->theme.text, "MaaGE Charcoal");
    maage_ui_fill(ui, maage_ui_rect(inside.x, inside.y + 92, inside.width, 1), ui->theme.line);
    maage_ui_setting_number(ui, maage_ui_rect(inside.x, inside.y + 112, inside.width, 72), MAAGE_SETTING_UI_SCALE, 1, 1);
    maage_ui_paragraph(ui, maage_ui_rect(inside.x, inside.y + 192, inside.width, 38), ui->theme.small_size, "Changes apply to this session and are not saved.");
    inside = maage_ui_panel(ui, maage_ui_rect(area.x, area.y + 280, area.width - 8, 160));
    maage_ui_label(ui, maage_ui_rect(inside.x, inside.y, inside.width, 32), ui->theme.section_size, ui->theme.text, "Launcher behavior");
    maage_ui_paragraph_color(ui, maage_ui_rect(inside.x, inside.y + 48, inside.width - 88, 54), ui->theme.body_size, ui->theme.text,
        maage_setting_describe(MAAGE_SETTING_KEEP_OPEN)->label);
    maage_ui_button(ui, 620, maage_ui_rect(inside.x + inside.width - 64, inside.y + 40, 64, ui->theme.control_height), value.data.boolean ? "On" : "Off", MAAGE_UI_NORMAL, 0);
    maage_ui_paragraph(ui, maage_ui_rect(inside.x, inside.y + 92, inside.width, 34), ui->theme.small_size, "Available when game launching is supported.");
    maage_ui_clip_pop(ui);
}
static void maage_ui_game_settings(maage_launcher_ui *ui, maage_rect body)
{
    maage_rect area, inside;
    size_t index;
    const char *default_id = ui->app->settings.values[MAAGE_SETTING_DEFAULT_INSTALLATION].data.text;
    const char *default_name = *default_id ? "Unavailable" : "Not configured";
    maage_setting_value directory = ui->app->settings.values[MAAGE_SETTING_GAME_DIRECTORY];
    if (ui->app->installations.items) for (index = 0; index < ui->app->installations.count; ++index)
        if (ui->app->installations.items[index].id && ui->app->installations.items[index].name &&
            strcmp(default_id, ui->app->installations.items[index].id) == 0) default_name = ui->app->installations.items[index].name;
    area = maage_ui_scroll(ui, body, &ui->scroll[3], 550);
    maage_ui_paragraph(ui, maage_ui_rect(area.x, area.y, area.width, 52), ui->theme.small_size, "Game configuration becomes available with installation and launch support.");
    inside = maage_ui_panel(ui, maage_ui_rect(area.x, area.y + 64, area.width - 8, 248));
    maage_ui_label(ui, maage_ui_rect(inside.x, inside.y, inside.width, 32), ui->theme.section_size, ui->theme.text, "Installation defaults");
    maage_ui_label(ui, maage_ui_rect(inside.x, inside.y + 48, inside.width, 28), ui->theme.body_size, ui->theme.text, maage_setting_describe(MAAGE_SETTING_DEFAULT_INSTALLATION)->label);
    maage_ui_button(ui, 630, maage_ui_rect(inside.x, inside.y + 80, inside.width, ui->theme.control_height), default_name, MAAGE_UI_NORMAL, 0);
    maage_ui_label(ui, maage_ui_rect(inside.x, inside.y + 136, inside.width, 28), ui->theme.body_size, ui->theme.text, maage_setting_describe(MAAGE_SETTING_GAME_DIRECTORY)->label);
    maage_ui_text_input(ui, 631, maage_ui_rect(inside.x, inside.y + 168, inside.width, ui->theme.control_height), directory.data.text, sizeof(directory.data.text), 0);
    if (!directory.data.text[0]) maage_ui_label(ui, maage_ui_rect(inside.x + 12, inside.y + 179, inside.width - 24, 28), ui->theme.body_size, ui->theme.muted, "Not configured");
    inside = maage_ui_panel(ui, maage_ui_rect(area.x, area.y + 328, area.width - 8, 222));
    maage_ui_label(ui, maage_ui_rect(inside.x, inside.y, inside.width, 32), ui->theme.section_size, ui->theme.text, "Runtime and memory");
    maage_ui_label(ui, maage_ui_rect(inside.x, inside.y + 48, inside.width, 28), ui->theme.body_size, ui->theme.muted, "Native MaaGE Client");
    maage_ui_setting_number(ui, maage_ui_rect(inside.x, inside.y + 98, inside.width, 72), MAAGE_SETTING_MEMORY_MIB, 0, 0);
    maage_ui_clip_pop(ui);
}
static void maage_ui_account_settings(maage_launcher_ui *ui, maage_rect body)
{
    maage_rect area, inside;
    const maage_launcher_account *account = maage_launcher_selected_account(ui->app);
    int signed_in = ui->app->accounts.status == MAAGE_ACCOUNT_SIGNED_IN && account && account->authenticated;
    area = maage_ui_scroll(ui, body, &ui->scroll[3], 260);
    inside = maage_ui_panel(ui, maage_ui_rect(area.x, area.y, area.width - 8, 260));
    maage_ui_label(ui, maage_ui_rect(inside.x, inside.y, inside.width, 32), ui->theme.section_size, ui->theme.text, "Microsoft Accounts");
    maage_ui_paragraph(ui, maage_ui_rect(inside.x, inside.y + 44, inside.width, 60), ui->theme.body_size, "Sign-in and account selection live in the account menu at the top right.");
    maage_ui_status(ui, maage_ui_rect(inside.x, inside.y + 112, inside.width, 52), "Account", signed_in && account->display_name ? account->display_name : "Not signed in", signed_in);
    if (maage_ui_button(ui, 640, maage_ui_rect(inside.x, inside.y + 180, 184, ui->theme.control_height), "Manage accounts", MAAGE_UI_NORMAL, 1)) ui->account_menu = 1;
    maage_ui_clip_pop(ui);
}
static void maage_ui_about(maage_launcher_ui *ui, maage_rect body)
{
    maage_rect area, inside;
    float y, height, width = body.width - 8 - ui->theme.padding * 2;
    const char *paragraphs[] = {
        "A modular, cross-platform reimagining of Minecraft in C, built for performance, extensibility, and limitless modding.",
        "MaaGE is NOT an official Minecraft project and is NOT approved by or associated with Mojang.",
        "Original MaaGE code is licensed under Apache-2.0. Minecraft assets and third-party content are not covered.",
        "Developed collaboratively by humans and AI, with human oversight and responsibility."
    };
    int index;
    height = 160 + ui->theme.padding * 2;
    for (index = 0; index < 4; ++index) height += maage_ui_paragraph_height(ui, width, ui->theme.body_size, paragraphs[index]) + ui->theme.gap;
    area = maage_ui_scroll(ui, body, &ui->scroll[3], height);
    inside = maage_ui_panel(ui, maage_ui_rect(area.x, area.y, area.width - 8, height));
    y = inside.y + maage_ui_heading(ui, inside, "MaaGE", "Minecraft as a Game Engine") + ui->theme.gap;
    maage_ui_label(ui, maage_ui_rect(inside.x, y, inside.width, 28), ui->theme.body_size, ui->theme.text, "Launcher " MAAGE_LAUNCHER_VERSION);
    y += 48;
    for (index = 0; index < 4; ++index) {
        height = maage_ui_paragraph_height(ui, inside.width, ui->theme.body_size, paragraphs[index]);
        maage_ui_paragraph(ui, maage_ui_rect(inside.x, y, inside.width, height), ui->theme.body_size, paragraphs[index]);
        y += height + ui->theme.gap;
    }
    maage_ui_clip_pop(ui);
}
void maage_ui_settings_page(maage_launcher_ui *ui, maage_rect body)
{
    static const char *sections[] = { "General", "Game", "Accounts", "About" };
    maage_rect item, content;
    int index, sidebar = body.width >= 760;
    float top = maage_ui_heading(ui, body, "Settings", "Make MaaGE feel at home.") + ui->theme.gap;
    for (index = 0; index < 4; ++index) {
        item = sidebar ? maage_ui_rect(body.x, body.y + top + index * 52, 176, 44) :
            maage_ui_rect(body.x + index * body.width / 4, body.y + top, body.width / 4 - 4, 44);
        if (maage_ui_tab(ui, 650 + index, item, sections[index], (int)ui->app->navigation.settings == index)) ui->app->navigation.settings = (maage_settings_section)index;
    }
    if (ui->last_section != (int)ui->app->navigation.settings) {
        ui->scroll[3] = 0; ui->last_section = (int)ui->app->navigation.settings;
        if (ui->focus < 650 || ui->focus > 653) ui->focus = 0;
    }
    content = sidebar ? maage_ui_rect(body.x + 200, body.y + top, body.width - 200, body.height - top) :
        maage_ui_rect(body.x, body.y + top + 60, body.width, body.height - top - 60);
    switch (ui->app->navigation.settings) {
    case MAAGE_SETTINGS_GENERAL: maage_ui_general(ui, content); break;
    case MAAGE_SETTINGS_GAME: maage_ui_game_settings(ui, content); break;
    case MAAGE_SETTINGS_ACCOUNTS: maage_ui_account_settings(ui, content); break;
    case MAAGE_SETTINGS_ABOUT: maage_ui_about(ui, content); break;
    }
}
