#include "widgets.h"
#include "maage/launcher_version.h"
#include <stdio.h>
#include <string.h>

static void maage_ui_setting_number(maage_launcher_ui *ui, maage_rect row, maage_setting_key key, int percent, int enabled)
{
    const maage_setting_definition *definition = maage_setting_describe(key);
    maage_setting_value value = ui->app->settings.values[key];
    char label[32];
    maage_ui_label(ui, maage_ui_rect(row.x, row.y, row.width, 24), 15, ui->theme.text, definition->label);
    sprintf(label, percent ? "%.0f%%" : "%.0f MiB", (double)value.data.number * (percent ? 100 : 1));
    if (!percent && value.data.number == 0) sprintf(label, "Automatic");
    maage_ui_label(ui, maage_ui_rect(row.x, row.y + 38, 94, 24), 14, ui->theme.muted, label);
    maage_ui_slider(ui, 600 + key, maage_ui_rect(row.x + 104, row.y + 30, row.width - 116, 32),
        &value.data.number, definition->minimum, definition->maximum, enabled);
    if (enabled) maage_launcher_settings_set(&ui->app->settings, key, &value);
}
static void maage_ui_general(maage_launcher_ui *ui, maage_rect body)
{
    maage_rect area;
    maage_setting_value value = ui->app->settings.values[MAAGE_SETTING_KEEP_OPEN];
    area = maage_ui_scroll(ui, body, &ui->scroll[3], 354);
    maage_ui_label(ui, maage_ui_rect(area.x, area.y, area.width, 28), 20, ui->theme.text, "Appearance");
    maage_ui_label(ui, maage_ui_rect(area.x, area.y + 40, area.width, 24), 14, ui->theme.muted, "Charcoal / the MaaGE dark theme");
    maage_ui_setting_number(ui, maage_ui_rect(area.x, area.y + 90, area.width, 72), MAAGE_SETTING_UI_SCALE, 1, 1);
    maage_ui_paragraph(ui, maage_ui_rect(area.x, area.y + 176, area.width, 58), 13, "Changes apply to this session and are not saved.");
    maage_ui_fill(ui, maage_ui_rect(area.x, area.y + 242, area.width, 1), ui->theme.line);
    maage_ui_label(ui, maage_ui_rect(area.x, area.y + 262, area.width - 76, 26), 15, ui->theme.text, maage_setting_describe(MAAGE_SETTING_KEEP_OPEN)->label);
    maage_ui_button(ui, 620, maage_ui_rect(area.x + area.width - 60, area.y + 256, 60, ui->theme.control_height), value.data.boolean ? "On" : "Off", MAAGE_UI_NORMAL, 0);
    maage_ui_paragraph(ui, maage_ui_rect(area.x, area.y + 308, area.width, 46), 13, "Launcher behavior settings become available with game launching.");
    maage_ui_clip_pop(ui);
}
static void maage_ui_game_settings(maage_launcher_ui *ui, maage_rect body)
{
    maage_rect area;
    size_t index;
    const char *default_id = ui->app->settings.values[MAAGE_SETTING_DEFAULT_INSTALLATION].data.text;
    const char *default_name = *default_id ? "Unavailable" : "Not configured";
    maage_setting_value directory = ui->app->settings.values[MAAGE_SETTING_GAME_DIRECTORY];
    if (ui->app->installations.items) for (index = 0; index < ui->app->installations.count; ++index)
        if (ui->app->installations.items[index].id && ui->app->installations.items[index].name &&
            strcmp(default_id, ui->app->installations.items[index].id) == 0) default_name = ui->app->installations.items[index].name;
    area = maage_ui_scroll(ui, body, &ui->scroll[3], 480);
    maage_ui_paragraph(ui, maage_ui_rect(area.x, area.y, area.width, 56), 14, "Game configuration is not available yet. These preferences will be used when installation and launch support is added.");
    maage_ui_label(ui, maage_ui_rect(area.x, area.y + 76, area.width, 26), 15, ui->theme.text, maage_setting_describe(MAAGE_SETTING_DEFAULT_INSTALLATION)->label);
    maage_ui_button(ui, 630, maage_ui_rect(area.x, area.y + 112, area.width, ui->theme.control_height), default_name, MAAGE_UI_NORMAL, 0);
    maage_ui_label(ui, maage_ui_rect(area.x, area.y + 170, area.width, 26), 15, ui->theme.text, maage_setting_describe(MAAGE_SETTING_GAME_DIRECTORY)->label);
    maage_ui_text_input(ui, 631, maage_ui_rect(area.x, area.y + 206, area.width, ui->theme.control_height), directory.data.text, sizeof(directory.data.text), 0);
    if (!directory.data.text[0]) maage_ui_label(ui, maage_ui_rect(area.x + 12, area.y + 213, area.width - 24, 24), 14, ui->theme.muted, "Not configured");
    maage_ui_label(ui, maage_ui_rect(area.x, area.y + 264, area.width, 26), 15, ui->theme.text, "Runtime");
    maage_ui_label(ui, maage_ui_rect(area.x, area.y + 296, area.width, 24), 14, ui->theme.muted, "Native MaaGE Client");
    maage_ui_setting_number(ui, maage_ui_rect(area.x, area.y + 354, area.width, 72), MAAGE_SETTING_MEMORY_MIB, 0, 0);
    maage_ui_clip_pop(ui);
}
static void maage_ui_account_settings(maage_launcher_ui *ui, maage_rect body)
{
    maage_rect area;
    const maage_launcher_account *account = maage_launcher_selected_account(ui->app);
    area = maage_ui_scroll(ui, body, &ui->scroll[3], 210);
    maage_ui_heading(ui, area, "Microsoft Accounts", "Manage sign-in and account selection from the account menu.");
    maage_ui_label(ui, maage_ui_rect(area.x, area.y + 92, area.width, 26), 15, ui->theme.muted,
        ui->app->accounts.status == MAAGE_ACCOUNT_SIGNED_IN && account && account->display_name ? account->display_name : "Not signed in");
    if (maage_ui_button(ui, 640, maage_ui_rect(area.x, area.y + 142, 166, ui->theme.control_height), "Manage accounts", MAAGE_UI_NORMAL, 1)) ui->account_menu = 1;
    maage_ui_clip_pop(ui);
}
static void maage_ui_about(maage_launcher_ui *ui, maage_rect body)
{
    maage_rect area;
    float y, height;
    const char *description = "A modular, cross-platform reimagining of Minecraft in C, built for performance, extensibility, and limitless modding.";
    const char *disclaimer = "MaaGE is NOT an official Minecraft project and is NOT approved by or associated with Mojang.";
    const char *license = "Original MaaGE code is licensed under Apache-2.0. Minecraft assets and third-party content are not covered.";
    const char *collaboration = "Developed collaboratively by humans and AI, with human oversight and responsibility.";
    height = 196 + maage_ui_paragraph_height(ui, body.width, 15, description) + maage_ui_paragraph_height(ui, body.width, 14, disclaimer) +
        maage_ui_paragraph_height(ui, body.width, 14, license) + maage_ui_paragraph_height(ui, body.width, 14, collaboration);
    area = maage_ui_scroll(ui, body, &ui->scroll[3], height);
    maage_ui_heading(ui, area, "MaaGE", "Minecraft as a Game Engine");
    maage_ui_label(ui, maage_ui_rect(area.x, area.y + 86, area.width, 26), 14, ui->theme.text, "Launcher " MAAGE_LAUNCHER_VERSION);
    y = area.y + 132;
    maage_ui_paragraph(ui, maage_ui_rect(area.x, y, area.width, height), 15, description);
    y += maage_ui_paragraph_height(ui, area.width, 15, description) + 24;
    maage_ui_paragraph(ui, maage_ui_rect(area.x, y, area.width, height), 14, disclaimer);
    y += maage_ui_paragraph_height(ui, area.width, 14, disclaimer) + 20;
    maage_ui_paragraph(ui, maage_ui_rect(area.x, y, area.width, height), 14, license);
    y += maage_ui_paragraph_height(ui, area.width, 14, license) + 20;
    maage_ui_paragraph(ui, maage_ui_rect(area.x, y, area.width, height), 14, collaboration);
    maage_ui_clip_pop(ui);
}
void maage_ui_settings_page(maage_launcher_ui *ui, maage_rect body)
{
    static const char *sections[] = { "General", "Game", "Accounts", "About" };
    maage_rect item, content;
    int index;
    maage_ui_heading(ui, body, "Settings", "Preferences for your launcher and game.");
    for (index = 0; index < 4; ++index) {
        item = maage_ui_rect(body.x + index * body.width / 4, body.y + 78, body.width / 4 - 4, ui->theme.control_height);
        if (maage_ui_button(ui, 650 + index, item, sections[index], MAAGE_UI_TAB, 1)) ui->app->navigation.settings = (maage_settings_section)index;
        if ((int)ui->app->navigation.settings == index) maage_ui_fill(ui, maage_ui_rect(item.x, item.y + item.height - 2, item.width, 2), ui->theme.accent);
    }
    if (ui->last_section != (int)ui->app->navigation.settings) { ui->scroll[3] = 0; ui->last_section = (int)ui->app->navigation.settings; ui->focus = 0; }
    content = maage_ui_rect(body.x, body.y + 136, body.width, body.height - 136);
    switch (ui->app->navigation.settings) {
    case MAAGE_SETTINGS_GENERAL: maage_ui_general(ui, content); break;
    case MAAGE_SETTINGS_GAME: maage_ui_game_settings(ui, content); break;
    case MAAGE_SETTINGS_ACCOUNTS: maage_ui_account_settings(ui, content); break;
    case MAAGE_SETTINGS_ABOUT: maage_ui_about(ui, content); break;
    }
}
