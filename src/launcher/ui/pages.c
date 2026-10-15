#include "widgets.h"

static const char *maage_ui_string(const char *text) { return text ? text : ""; }
static void maage_ui_launch_selection(maage_launcher_ui *ui, maage_rect card)
{
    const maage_launcher_installation *selected = maage_launcher_selected_installation(ui->app);
    const char *id = selected ? selected->id : NULL;
    const char *name = selected ? maage_ui_string(selected->name) : "Choose your installation";
    int ready = maage_launcher_can_request(ui->app, MAAGE_ACTION_PLAY, id);
    maage_rect inside = maage_ui_panel(ui, card), action;
    float height = maage_ui_paragraph_height(ui, inside.width, 26, name);
    maage_ui_label(ui, maage_ui_rect(inside.x, inside.y, inside.width, 25), ui->theme.small_size, ui->theme.muted, "SELECTED INSTALLATION");
    maage_ui_paragraph_color(ui, maage_ui_rect(inside.x, inside.y + 36, inside.width, height), 26, ui->theme.text, name);
    maage_ui_paragraph(ui, maage_ui_rect(inside.x, inside.y + 48 + height, inside.width, 68), ui->theme.body_size,
        selected ? maage_ui_string(selected->version) : "Select a game version or modpack in Installations. Your selection will appear here.");
    if (selected && selected->modpack) maage_ui_label(ui, maage_ui_rect(inside.x, inside.y + 84 + height, inside.width, 26), ui->theme.small_size, ui->theme.muted, selected->modpack);
    action = maage_ui_rect(inside.x, inside.y + inside.height - 144, inside.width, ui->theme.control_height);
    if (maage_ui_button(ui, 100, action, selected ? "Change installation" : "Browse installations", MAAGE_UI_NORMAL, 1)) ui->app->navigation.page = MAAGE_PAGE_INSTALLATIONS;
    action.y += ui->theme.control_height + 12; action.height = 48;
    if (maage_ui_button(ui, 101, action, "Play", MAAGE_UI_PRIMARY, ready)) maage_launcher_request(ui->app, MAAGE_ACTION_PLAY, id);
    maage_ui_label(ui, maage_ui_rect(inside.x, inside.y + inside.height - 24, inside.width, 24), ui->theme.small_size, ui->theme.muted,
        ready ? "Ready to launch" : "Game launching is not available yet.");
}
static void maage_ui_launch_readiness(maage_launcher_ui *ui, maage_rect card)
{
    const maage_launcher_installation *installation = maage_launcher_selected_installation(ui->app);
    const maage_launcher_account *account = maage_launcher_selected_account(ui->app);
    const char *account_label = "Signed out";
    maage_rect inside = maage_ui_panel(ui, card);
    int signed_in = account && account->authenticated;
    int ready = maage_launcher_can_request(ui->app, MAAGE_ACTION_PLAY, installation ? installation->id : NULL);
    if (signed_in) account_label = account->display_name ? account->display_name : "Signed in";
    if (ui->app->accounts.status == MAAGE_ACCOUNT_AUTHENTICATING) account_label = "Sign-in in progress";
    if (ui->app->accounts.status == MAAGE_ACCOUNT_ERROR) account_label = "Sign-in needs attention";
    maage_ui_label(ui, maage_ui_rect(inside.x, inside.y, inside.width, 32), ui->theme.section_size, ui->theme.text, "Launch readiness");
    maage_ui_status(ui, maage_ui_rect(inside.x, inside.y + 52, inside.width, 52), "Installation", installation ? "Selected" : "Not selected", installation != NULL);
    maage_ui_status(ui, maage_ui_rect(inside.x, inside.y + 124, inside.width, 52), "Minecraft account", account && signed_in && account->status_message ? account->status_message : account_label, account && account->minecraft_ready);
    maage_ui_status(ui, maage_ui_rect(inside.x, inside.y + 196, inside.width, 52), "Game launching", ready ? "Available" : "Not available yet", ready);
    if (maage_ui_button(ui, 102, maage_ui_rect(inside.x, inside.y + inside.height - ui->theme.control_height, inside.width, ui->theme.control_height),
        signed_in ? "Manage account" : "Account options", MAAGE_UI_NORMAL, 1)) ui->account_menu = 1;
}
static void maage_ui_play(maage_launcher_ui *ui, maage_rect body)
{
    maage_rect area, left, right, link;
    float heading, top, width, height;
    int split = body.width >= 800, stack_links = body.width < 560;
    const maage_launcher_installation *installation = maage_launcher_selected_installation(ui->app);
    width = split ? (body.width - ui->theme.gap) * 0.62f : body.width;
    height = 320 + maage_ui_paragraph_height(ui, width - ui->theme.padding * 2, 26,
        installation ? maage_ui_string(installation->name) : "Choose your installation");
    if (height < 356) height = 356;
    heading = maage_ui_heading(ui, body, "Play", "Your installation. Your worlds. Ready when you are.");
    top = heading + ui->theme.gap;
    area = maage_ui_scroll(ui, maage_ui_rect(body.x, body.y + top, body.width, body.height - top), &ui->scroll[0],
        (split ? height : height * 2 + ui->theme.gap) + ui->theme.gap + (stack_links ? 176 + ui->theme.gap : 88));
    width = split ? (area.width - ui->theme.gap) * 0.62f : area.width;
    left = maage_ui_rect(area.x, area.y, width, height);
    right = split ? maage_ui_rect(area.x + width + ui->theme.gap, area.y, area.width - width - ui->theme.gap, height) :
        maage_ui_rect(area.x, area.y + height + ui->theme.gap, area.width, height);
    maage_ui_launch_selection(ui, left);
    maage_ui_launch_readiness(ui, right);
    link = maage_ui_rect(area.x, right.y + right.height + ui->theme.gap, stack_links ? area.width : (area.width - ui->theme.gap) / 2, 88);
    if (maage_ui_summary(ui, 103, link, "Project news", "Updates and announcements", NULL, 0, 1)) ui->app->navigation.page = MAAGE_PAGE_NEWS;
    if (stack_links) link.y += link.height + ui->theme.gap;
    else link.x += link.width + ui->theme.gap;
    if (maage_ui_summary(ui, 104, link, "Launcher settings", "Appearance and preferences", NULL, 0, 1)) {
        ui->app->navigation.page = MAAGE_PAGE_SETTINGS; ui->app->navigation.settings = MAAGE_SETTINGS_GENERAL;
    }
    maage_ui_clip_pop(ui);
}
static void maage_ui_news_state(maage_launcher_ui *ui, maage_rect body)
{
    const char *title, *message;
    maage_rect area;
    switch (ui->app->news.status) {
    case MAAGE_CONTENT_LOADING: title = "Loading news"; message = "Getting the latest project updates..."; break;
    case MAAGE_CONTENT_ERROR:
        title = "News could not be loaded";
        message = ui->app->news.error ? ui->app->news.error : "Please try again later."; break;
    case MAAGE_CONTENT_UNAVAILABLE: title = "News will appear here"; message = "News is not available yet. Check back for project updates."; break;
    default: title = "No news yet"; message = "There are no announcements to show right now."; break;
    }
    area = maage_ui_scroll(ui, body, &ui->scroll[1], 216);
    maage_ui_empty(ui, area, title, message);
    maage_ui_clip_pop(ui);
}
static void maage_ui_news(maage_launcher_ui *ui, maage_rect body)
{
    const maage_launcher_article *article;
    maage_rect area, list, detail, content, card;
    size_t index, count = ui->app->news.items ? ui->app->news.count : 0;
    float article_height, image_height, subtitle_height, list_height, top;
    top = maage_ui_heading(ui, body, "News", "Updates from the MaaGE project.") + ui->theme.gap;
    if (maage_ui_button(ui, 120, maage_ui_rect(body.x + body.width - 112, body.y, 112, ui->theme.control_height), "Refresh", MAAGE_UI_NORMAL,
        maage_launcher_can_request(ui->app, MAAGE_ACTION_REFRESH_NEWS, NULL))) maage_launcher_request(ui->app, MAAGE_ACTION_REFRESH_NEWS, NULL);
    area = maage_ui_rect(body.x, body.y + top, body.width, body.height - top);
    if (ui->app->news.status != MAAGE_CONTENT_READY || !count) { maage_ui_news_state(ui, area); return; }
    if (ui->app->navigation.article >= count) ui->app->navigation.article = 0;
    list = maage_ui_rect(area.x, area.y, 250, area.height);
    detail = maage_ui_rect(area.x + 274, area.y, area.width - 274, area.height);
    if (area.width < 660) {
        list_height = area.height > 350 ? 160 : 84;
        list = maage_ui_rect(area.x, area.y, area.width, list_height);
        detail = maage_ui_rect(area.x, area.y + list_height + 20, area.width, area.height - list_height - 20);
    }
    content = maage_ui_scroll(ui, list, &ui->scroll[5], (float)count * 88);
    for (index = 0; index < count; ++index) {
        article = &ui->app->news.items[index];
        card = maage_ui_rect(content.x, content.y + (float)index * 88, content.width - 8, 76);
        if (maage_ui_summary(ui, 2000 + (int)index, card, maage_ui_string(article->title), maage_ui_string(article->subtitle), article->image,
            ui->app->navigation.article == index, 1)) { ui->app->navigation.article = index; ui->scroll[1] = 0; }
    }
    maage_ui_clip_pop(ui);
    article = &ui->app->news.items[ui->app->navigation.article];
    subtitle_height = maage_ui_paragraph_height(ui, detail.width - 24, ui->theme.body_size, article->subtitle);
    image_height = article->image ? 180 : 0;
    article_height = 62 + subtitle_height + image_height + maage_ui_paragraph_height(ui, detail.width - 24, ui->theme.body_size, article->body);
    content = maage_ui_scroll(ui, detail, &ui->scroll[1], article_height);
    maage_ui_label(ui, maage_ui_rect(content.x, content.y, content.width - 24, 38), 24, ui->theme.text, maage_ui_string(article->title));
    maage_ui_paragraph(ui, maage_ui_rect(content.x, content.y + 42, content.width - 24, subtitle_height), ui->theme.body_size, article->subtitle);
    if (article->image) maage_ui_image(ui, maage_ui_rect(content.x, content.y + 62 + subtitle_height, content.width - 24, 160), article->image);
    maage_ui_paragraph(ui, maage_ui_rect(content.x, content.y + 62 + subtitle_height + image_height, content.width - 24,
        article_height), ui->theme.body_size, article->body);
    maage_ui_clip_pop(ui);
}
static void maage_ui_installation_card(maage_launcher_ui *ui, maage_rect card, const maage_launcher_installation *item, int index)
{
    maage_rect actions;
    const maage_launcher_installation *selected = maage_launcher_selected_installation(ui->app);
    int is_selected = selected == item;
    if (maage_ui_summary(ui, 4000 + index * 4, maage_ui_rect(card.x, card.y, card.width, 76), maage_ui_string(item->name), maage_ui_string(item->version), NULL,
        is_selected, maage_launcher_can_request(ui->app, MAAGE_ACTION_SELECT_INSTALLATION, item->id))) maage_launcher_request(ui->app, MAAGE_ACTION_SELECT_INSTALLATION, item->id);
    maage_ui_fill(ui, maage_ui_rect(card.x, card.y + 76, card.width, 80), ui->theme.panel);
    if (item->modpack) maage_ui_label(ui, maage_ui_rect(card.x + 16, card.y + 76, card.width - 32, 22), ui->theme.small_size, ui->theme.muted, item->modpack);
    actions = maage_ui_rect(card.x + 16, card.y + 104, 88, ui->theme.control_height);
    if (maage_ui_button(ui, 4001 + index * 4, actions, "Edit", MAAGE_UI_NORMAL, maage_launcher_can_request(ui->app, MAAGE_ACTION_EDIT_INSTALLATION, item->id)))
        maage_launcher_request(ui->app, MAAGE_ACTION_EDIT_INSTALLATION, item->id);
    actions.x += 100;
    if (maage_ui_button(ui, 4002 + index * 4, actions, "Remove", MAAGE_UI_DANGER, maage_launcher_can_request(ui->app, MAAGE_ACTION_REMOVE_INSTALLATION, item->id)))
        maage_launcher_request(ui->app, MAAGE_ACTION_REMOVE_INSTALLATION, item->id);
    if (is_selected) maage_ui_label(ui, maage_ui_rect(actions.x + 100, actions.y + 10, card.width - 310, 26), ui->theme.small_size, ui->theme.muted, "Selected");
}
static void maage_ui_installations(maage_launcher_ui *ui, maage_rect body)
{
    maage_rect area;
    size_t index, count = ui->app->installations.items ? ui->app->installations.count : 0;
    float top;
    const char *title = "No installations yet", *message = "Your game versions and modpacks will appear here. Installation management is not available yet.";
    if (maage_launcher_can_request(ui->app, MAAGE_ACTION_CREATE_INSTALLATION, NULL))
        message = "Create an installation to choose a game version or modpack.";
    top = maage_ui_heading(ui, body, "Installations", "Game versions and modpacks, all in one place.") + ui->theme.gap;
    if (body.width < 640) top += ui->theme.control_height + ui->theme.gap;
    if (maage_ui_button(ui, 140, maage_ui_rect(body.width < 640 ? body.x : body.x + body.width - 192,
        body.width < 640 ? body.y + top - ui->theme.control_height - ui->theme.gap : body.y, 192, ui->theme.control_height), "Create installation", MAAGE_UI_PRIMARY,
        maage_launcher_can_request(ui->app, MAAGE_ACTION_CREATE_INSTALLATION, NULL))) maage_launcher_request(ui->app, MAAGE_ACTION_CREATE_INSTALLATION, NULL);
    area = maage_ui_rect(body.x, body.y + top, body.width, body.height - top);
    if (ui->app->installations.status == MAAGE_CONTENT_LOADING) { title = "Loading installations"; message = "Looking for your installed game versions..."; }
    if (ui->app->installations.status == MAAGE_CONTENT_ERROR) {
        title = "Installations could not be loaded"; message = ui->app->installations.error ? ui->app->installations.error : "Please try again later.";
    }
    if (ui->app->installations.status != MAAGE_CONTENT_READY || !count) {
        area = maage_ui_scroll(ui, area, &ui->scroll[2], 216);
        maage_ui_empty(ui, area, title, message);
    } else {
        area = maage_ui_scroll(ui, area, &ui->scroll[2], (float)count * 172);
        for (index = 0; index < count; ++index) maage_ui_installation_card(ui,
            maage_ui_rect(area.x, area.y + (float)index * 172, area.width - 8, 156), &ui->app->installations.items[index], (int)index);
    }
    maage_ui_clip_pop(ui);
}
void maage_ui_pages(maage_launcher_ui *ui, maage_rect body)
{
    switch (ui->app->navigation.page) {
    case MAAGE_PAGE_PLAY: maage_ui_play(ui, body); break;
    case MAAGE_PAGE_NEWS: maage_ui_news(ui, body); break;
    case MAAGE_PAGE_INSTALLATIONS: maage_ui_installations(ui, body); break;
    case MAAGE_PAGE_SETTINGS: maage_ui_settings_page(ui, body); break;
    }
}
