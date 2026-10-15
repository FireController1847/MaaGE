#include "widgets.h"
#include "maage/launcher_version.h"
#include <stdio.h>

static const char *maage_profile_names[] = { "MaaGE Preview", "Creative Workshop", "Vanilla Foundations" };
static const char *maage_account_names[] = { "Avery", "Rowan", "Juniper", "Nova" };
static void maage_ui_heading(maage_launcher_ui *ui, maage_rect body, const char *title, const char *subtitle)
{
    maage_ui_label(ui, maage_ui_rect(body.x, body.y, body.width, 40), 30, ui->theme.text, title);
    maage_ui_label(ui, maage_ui_rect(body.x, body.y + 46, body.width, 25), 15, ui->theme.muted, subtitle);
}
static void maage_ui_play(maage_launcher_ui *ui, maage_rect body)
{
    maage_rect area, button;
    maage_ui_layout actions;
    float height = body.height > 440 ? body.height : 440;
    area = maage_ui_scroll(ui, body, &ui->scroll[0], height);
    maage_ui_hero(ui, area, ui->profile);
    maage_ui_label(ui, maage_ui_rect(area.x + 28, area.y + 32, area.width - 56, 25), 14, ui->theme.muted, "YOUR NEXT WORLD STARTS HERE");
    maage_ui_label(ui, maage_ui_rect(area.x + 28, area.y + 70, area.width - 56, 65), 48, ui->theme.text, "MaaGE");
    maage_ui_label(ui, maage_ui_rect(area.x + 28, area.y + 142, area.width - 56, 28), 18, ui->theme.text, maage_profile_names[ui->profile]);
    if (maage_ui_button(ui, 100, maage_ui_rect(area.x + 28, area.y + 185, 166, 36), "Choose profile", MAAGE_UI_NORMAL, 1)) ui->page = 2;
    actions.rect = maage_ui_rect(area.x + 24, area.y + area.height - 122, area.width - 48, 62);
    actions.cursor = 0; actions.gap = 12; actions.horizontal = 1;
    button = maage_ui_take(&actions, (actions.rect.width - 24) / 3);
    maage_ui_button(ui, 101, button, "Preview", MAAGE_UI_NORMAL, 0);
    button = maage_ui_take(&actions, (actions.rect.width - 24) / 3);
    maage_ui_button(ui, 102, button, "PLAY", MAAGE_UI_PRIMARY, 0);
    button = maage_ui_take(&actions, (actions.rect.width - 24) / 3);
    maage_ui_button(ui, 103, button, "Modded", MAAGE_UI_NORMAL, 0);
    maage_ui_label(ui, maage_ui_rect(area.x + 24, area.y + area.height - 44, area.width - 48, 25), 14, ui->theme.muted, "Game installation and launching are not available yet.");
    maage_ui_clip_pop(ui);
}
static void maage_ui_news(maage_launcher_ui *ui, maage_rect body)
{
    static const char *titles[] = { "A Place to Begin", "Built Your Way", "Room to Explore" };
    static const char *articles[] = {
        "Welcome to the MaaGE interface preview. These fictional announcements demonstrate the reading experience.\n\nA quiet horizon, a familiar shape, and room for something new. This sample article is stored locally and does not come from a news service.\n\nProfiles, accounts, and updates belong in one simple space. Their underlying services remain future work.\n\nTake a look around. Switch articles, browse the example profiles, or explore the temporary settings. Nothing is downloaded or saved.",
        "This is a fictional design journal, not a release announcement.\n\nDifferent worlds call for different ways to play. MaaGE is being developed as a modular native platform, with independently configurable experiences in mind.\n\nThe profiles in this preview are examples only. They do not install versions, manage modpacks, or launch a game.\n\nFor now, this page provides a place to explore the layout, typography, and reading experience.",
        "An imaginary postcard from a world that does not exist yet.\n\nA distant ridge catches the last light. Below it, geometric landscapes leave their details to the imagination. All imagery in this preview is generated for MaaGE.\n\nNo Minecraft artwork or proprietary assets are bundled here.\n\nThis is sample content for the launcher UI, with no network connection or external news feed." };
    maage_rect list, article, content, item;
    float width, y;
    int compact = body.width < 620;
    int index;
    width = body.width >= 680 ? 220 : body.width * 0.32f;
    list = maage_ui_rect(body.x, body.y, width, body.height);
    article = maage_ui_rect(body.x + width + 24, body.y, body.width - width - 24, body.height);
    if (compact) {
        list = maage_ui_rect(body.x, body.y, body.width, 118);
        article = maage_ui_rect(body.x, body.y + 138, body.width, body.height - 138);
    }
    content = maage_ui_scroll(ui, list, &ui->scroll[6], compact ? 118 : 3 * 132);
    for (index = 0; index < 3; ++index) {
        item = compact ? maage_ui_rect(content.x + index * content.width / 3, content.y, content.width / 3 - 8, 118) :
            maage_ui_rect(content.x, content.y + index * 132, content.width - 8, 118);
        if (maage_ui_button(ui, 120 + index, item, "", MAAGE_UI_CARD, 1)) { ui->article = index; ui->scroll[1] = 0; }
        maage_ui_hero(ui, maage_ui_rect(item.x + 4, item.y + 4, item.width - 8, 72), index);
        maage_ui_label(ui, maage_ui_rect(item.x + 10, item.y + 85, item.width - 20, 26), 15, ui->theme.text, titles[index]);
        if (ui->article == index) maage_ui_fill(ui, maage_ui_rect(item.x, item.y, 3, item.height), ui->theme.accent);
    }
    maage_ui_clip_pop(ui);
    y = article.width < 400 ? 1100 : 920;
    content = maage_ui_scroll(ui, article, &ui->scroll[1], y);
    maage_ui_heading(ui, content, titles[ui->article], "A fictional MaaGE announcement");
    maage_ui_hero(ui, maage_ui_rect(content.x, content.y + 90, content.width - 12, 180), ui->article);
    maage_ui_paragraph(ui, maage_ui_rect(content.x, content.y + 298, content.width - 20, y - 300), 17, articles[ui->article]);
    maage_ui_clip_pop(ui);
}
static void maage_ui_profiles(maage_launcher_ui *ui, maage_rect body)
{
    maage_rect area, card;
    int index;
    area = maage_ui_scroll(ui, body, &ui->scroll[2], 440);
    maage_ui_heading(ui, area, "Profiles", "Example installations. Select a profile to preview it on Play.");
    for (index = 0; index < 3; ++index) {
        card = maage_ui_rect(area.x, area.y + 98 + index * 96, area.width, 80);
        if (maage_ui_button(ui, 140 + index, maage_ui_rect(card.x, card.y, card.width - 60, card.height), "", MAAGE_UI_CARD, 1)) ui->profile = index;
        maage_ui_image(ui, maage_ui_rect(card.x + 10, card.y + 10, 60, 60), index);
        maage_ui_label(ui, maage_ui_rect(card.x + 86, card.y + 12, card.width - 160, 30), 22, ui->theme.text, maage_profile_names[index]);
        maage_ui_label(ui, maage_ui_rect(card.x + 86, card.y + 45, card.width - 160, 24), 14, ui->theme.muted, index == ui->profile ? "Selected / sample profile" : "Sample profile / not installed");
        if (maage_ui_button(ui, 150 + index, maage_ui_rect(card.x + card.width - 56, card.y, 56, card.height), "\xe2\x9c\x8e", MAAGE_UI_CARD, 1)) ui->dialog = 1;
    }
    if (maage_ui_button(ui, 160, maage_ui_rect(area.x, area.y + 396, 160, 40), "+ Add profile", MAAGE_UI_PRIMARY, 1)) ui->dialog = 1;
    maage_ui_clip_pop(ui);
}
static void maage_ui_accounts(maage_launcher_ui *ui, maage_rect body)
{
    maage_rect area, card;
    int index, columns = body.width >= 650 ? 2 : 1;
    float width = (body.width - (columns - 1) * 20) / columns;
    area = maage_ui_scroll(ui, body, &ui->scroll[3], ui->empty_accounts ? 380 : 220 + (4 / columns) * 118);
    maage_ui_heading(ui, area, "Accounts", "Fictional accounts / no sign-in or credentials.");
    if (ui->empty_accounts) {
        maage_ui_label(ui, maage_ui_rect(area.x + 20, area.y + 158, area.width - 40, 45), 28, ui->theme.muted, "No accounts yet");
        maage_ui_paragraph(ui, maage_ui_rect(area.x + 20, area.y + 210, area.width - 40, 76), 17, "Add an account when sign-in becomes available. For now, explore the sample accounts below.");
    } else for (index = 0; index < 4; ++index) {
        card = maage_ui_rect(area.x + (index % columns) * (width + 20), area.y + 100 + (index / columns) * 118, width, 98);
        maage_ui_fill(ui, card, ui->theme.panel);
        maage_ui_image(ui, maage_ui_rect(card.x + 14, card.y + 17, 56, 56), index);
        maage_ui_label(ui, maage_ui_rect(card.x + 86, card.y + 12, card.width - 100, 28), 21, ui->theme.text, maage_account_names[index]);
        if (maage_ui_button(ui, 180 + index, maage_ui_rect(card.x + 86, card.y + 53, 88, 30), ui->account == index ? "Selected" : "Select", MAAGE_UI_PRIMARY, 1)) ui->account = index;
        maage_ui_button(ui, 190 + index, maage_ui_rect(card.x + 184, card.y + 53, 76, 30), "Logout", MAAGE_UI_DANGER, 0);
    }
    card = maage_ui_rect(area.x, area.y + area.height - 62, (area.width - 12) / 2, 40);
    if (maage_ui_button(ui, 200, card, "+ Add account", MAAGE_UI_PRIMARY, 1)) ui->dialog = 2;
    if (maage_ui_button(ui, 201, maage_ui_rect(card.x + card.width + 12, card.y, card.width, 40), ui->empty_accounts ? "Show samples" : "Show empty state", MAAGE_UI_NORMAL, 1)) ui->empty_accounts = !ui->empty_accounts;
    maage_ui_clip_pop(ui);
}
static void maage_ui_settings(maage_launcher_ui *ui, maage_rect body)
{
    maage_rect area;
    char value[64];
    area = maage_ui_scroll(ui, body, &ui->scroll[4], 520);
    maage_ui_heading(ui, area, "Settings", "Temporary preview values. Changes are not saved or applied to a game.");
    maage_ui_label(ui, maage_ui_rect(area.x, area.y + 104, area.width, 32), 23, ui->theme.text, "Memory");
    maage_ui_fill(ui, maage_ui_rect(area.x, area.y + 141, area.width, 2), ui->theme.line);
    sprintf(value, "%.1f GB", (double)ui->memory);
    maage_ui_label(ui, maage_ui_rect(area.x, area.y + 163, 88, 28), 17, ui->theme.text, value);
    maage_ui_slider(ui, 220, maage_ui_rect(area.x + 104, area.y + 156, area.width - 116, 36), &ui->memory, 1, 16);
    maage_ui_label(ui, maage_ui_rect(area.x, area.y + 224, area.width, 32), 23, ui->theme.text, "Interface Scale Preview");
    maage_ui_fill(ui, maage_ui_rect(area.x, area.y + 261, area.width, 2), ui->theme.line);
    sprintf(value, "%.0f%%", (double)ui->ui_scale * 100);
    maage_ui_label(ui, maage_ui_rect(area.x, area.y + 283, 88, 28), 17, ui->theme.text, value);
    maage_ui_slider(ui, 221, maage_ui_rect(area.x + 104, area.y + 276, area.width - 116, 36), &ui->ui_scale, 0.85f, 1.25f);
    maage_ui_label(ui, maage_ui_rect(area.x, area.y + 346, area.width, 32), 23, ui->theme.text, "Example Profile Name");
    maage_ui_text_input(ui, 222, maage_ui_rect(area.x, area.y + 394, area.width, 46), ui->display_name, sizeof(ui->display_name));
    maage_ui_paragraph(ui, maage_ui_rect(area.x, area.y + 460, area.width, 60), 15, "These controls demonstrate the interface only. Profile management and launch configuration will arrive separately.");
    maage_ui_clip_pop(ui);
}
static void maage_ui_about(maage_launcher_ui *ui, maage_rect body)
{
    maage_rect area;
    area = maage_ui_scroll(ui, body, &ui->scroll[5], 520);
    maage_ui_heading(ui, area, "MaaGE", "Minecraft as a Game Engine");
    maage_ui_label(ui, maage_ui_rect(area.x, area.y + 104, area.width, 30), 18, ui->theme.text, "Launcher " MAAGE_LAUNCHER_VERSION);
    maage_ui_paragraph(ui, maage_ui_rect(area.x, area.y + 166, area.width, 110), 18, "A modular, cross-platform reimagining of Minecraft in C, built for performance, extensibility, and limitless modding.");
    maage_ui_paragraph(ui, maage_ui_rect(area.x, area.y + 292, area.width, 100), 16, "MaaGE is NOT an official Minecraft project and is NOT approved by or associated with Mojang.");
    maage_ui_paragraph(ui, maage_ui_rect(area.x, area.y + 404, area.width, 110), 15, "Original MaaGE code is licensed under Apache-2.0. Minecraft assets and third-party content are not covered. Developed collaboratively by humans and AI, with human oversight and responsibility.");
    maage_ui_clip_pop(ui);
}
void maage_ui_pages(maage_launcher_ui *ui, maage_rect body)
{
    switch (ui->page) {
    case 0: maage_ui_play(ui, body); break;
    case 1: maage_ui_news(ui, body); break;
    case 2: maage_ui_profiles(ui, body); break;
    case 3: maage_ui_accounts(ui, body); break;
    case 4: maage_ui_settings(ui, body); break;
    default: maage_ui_about(ui, body); break;
    }
}
void maage_ui_dialog(maage_launcher_ui *ui)
{
    maage_rect card;
    maage_color dim = { 0, 0, 0, 180 };
    float width = ui->width > 560 ? 500 : ui->width - 40;
    ui->blocked = 0;
    maage_ui_fill(ui, maage_ui_rect(0, 0, ui->width, ui->height), dim);
    card = maage_ui_rect((ui->width - width) / 2, (ui->height - 300) / 2, width, 300);
    maage_ui_fill(ui, card, ui->theme.panel);
    maage_ui_label(ui, maage_ui_rect(card.x + 24, card.y + 24, width - 48, 40), 26, ui->theme.text, ui->dialog == 1 ? "Profile Preview" : "Accounts Preview");
    maage_ui_paragraph(ui, maage_ui_rect(card.x + 24, card.y + 78, width - 48, 100), 16, ui->dialog == 1 ? "Profiles are examples only. Try a temporary name below; installing, saving, and editing real profiles are not available yet." : "Microsoft sign-in is not available yet. No account will be added, and no credentials are collected.");
    if (ui->dialog == 1) maage_ui_text_input(ui, 900, maage_ui_rect(card.x + 24, card.y + 178, width - 48, 42), ui->display_name, sizeof(ui->display_name));
    if (maage_ui_button(ui, 901, maage_ui_rect(card.x + width - 124, card.y + 238, 100, 38), "Close", MAAGE_UI_PRIMARY, 1)) { ui->dialog = 0; ui->focus = 0; }
}
