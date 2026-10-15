#include "widgets.h"

static const char *maage_ui_string(const char *text) { return text ? text : ""; }
static void maage_ui_play(maage_launcher_ui *ui, maage_rect body)
{
    const maage_launcher_installation *selected = maage_launcher_selected_installation(ui->app);
    const char *id = selected ? selected->id : NULL;
    maage_rect area, card, action;
    float text_width, card_height = body.width < 560 ? 234 : 202;
    area = maage_ui_scroll(ui, body, &ui->scroll[0], 160 + card_height);
    maage_ui_fill(ui, maage_ui_rect(area.x, area.y, area.width, 128), ui->theme.panel);
    maage_ui_fill(ui, maage_ui_rect(area.x, area.y, 3, 128), ui->theme.accent);
    maage_ui_label(ui, maage_ui_rect(area.x + 24, area.y + 20, area.width - 48, 38), 28, ui->theme.text, "MaaGE");
    maage_ui_paragraph(ui, maage_ui_rect(area.x + 24, area.y + 68, area.width - 48, 52), 15, "Your worlds, your way. Choose an installation to get started.");
    card = maage_ui_rect(area.x, area.y + 152, area.width, card_height);
    maage_ui_fill(ui, card, ui->theme.panel);
    text_width = card.width - 40;
    if (card.width >= 560) text_width -= 184;
    maage_ui_label(ui, maage_ui_rect(card.x + 20, card.y + 18, text_width, 24), 13, ui->theme.muted, "INSTALLATION");
    maage_ui_label(ui, maage_ui_rect(card.x + 20, card.y + 50, text_width, 32), 22, ui->theme.text,
        selected ? maage_ui_string(selected->name) : "No installation selected");
    maage_ui_label(ui, maage_ui_rect(card.x + 20, card.y + 90, text_width, 25), 14, ui->theme.muted,
        selected ? maage_ui_string(selected->version) : "Manage your game versions and modpacks in Installations.");
    if (selected && selected->modpack) maage_ui_label(ui, maage_ui_rect(card.x + 20, card.y + 118, text_width, 24), 14, ui->theme.muted, selected->modpack);
    action = card.width >= 560 ? maage_ui_rect(card.x + card.width - 184, card.y + 20, 164, ui->theme.control_height) :
        maage_ui_rect(card.x + 20, card.y + card.height - 58, (card.width - 52) / 2, ui->theme.control_height);
    if (maage_ui_button(ui, 100, action, "Choose installation", MAAGE_UI_NORMAL, 1)) ui->app->navigation.page = MAAGE_PAGE_INSTALLATIONS;
    action = maage_ui_rect(card.x + card.width - (card.width >= 560 ? 184 : (card.width - 52) / 2 + 20),
        card.y + card.height - 58, card.width >= 560 ? 164 : (card.width - 52) / 2, 36);
    if (maage_ui_button(ui, 101, action, "Play", MAAGE_UI_PRIMARY, maage_launcher_can_request(ui->app, MAAGE_ACTION_PLAY, id)))
        maage_launcher_request(ui->app, MAAGE_ACTION_PLAY, id);
    if (!maage_launcher_can_request(ui->app, MAAGE_ACTION_PLAY, id) && card.width >= 560)
        maage_ui_label(ui, maage_ui_rect(card.x + 20, card.y + card.height - 48, text_width, 24), 13, ui->theme.muted, "Game launching is not available yet.");
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
    area = maage_ui_scroll(ui, body, &ui->scroll[1], 180);
    maage_ui_empty(ui, area, title, message);
    maage_ui_clip_pop(ui);
}
static void maage_ui_news(maage_launcher_ui *ui, maage_rect body)
{
    const maage_launcher_article *article;
    maage_rect area, list, detail, content, card;
    size_t index, count = ui->app->news.items ? ui->app->news.count : 0;
    float article_height, image_height, subtitle_height, list_height;
    maage_ui_heading(ui, body, "News", "Updates from the MaaGE project.");
    if (maage_ui_button(ui, 120, maage_ui_rect(body.x + body.width - 94, body.y, 94, ui->theme.control_height), "Refresh", MAAGE_UI_NORMAL,
        maage_launcher_can_request(ui->app, MAAGE_ACTION_REFRESH_NEWS, NULL))) maage_launcher_request(ui->app, MAAGE_ACTION_REFRESH_NEWS, NULL);
    area = maage_ui_rect(body.x, body.y + 86, body.width, body.height - 86);
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
    subtitle_height = maage_ui_paragraph_height(ui, detail.width - 24, 14, article->subtitle);
    image_height = article->image ? 180 : 0;
    article_height = 62 + subtitle_height + image_height + maage_ui_paragraph_height(ui, detail.width - 24, 15, article->body);
    content = maage_ui_scroll(ui, detail, &ui->scroll[1], article_height);
    maage_ui_label(ui, maage_ui_rect(content.x, content.y, content.width - 24, 38), 24, ui->theme.text, maage_ui_string(article->title));
    maage_ui_paragraph(ui, maage_ui_rect(content.x, content.y + 42, content.width - 24, subtitle_height), 14, article->subtitle);
    if (article->image) maage_ui_image(ui, maage_ui_rect(content.x, content.y + 62 + subtitle_height, content.width - 24, 160), article->image);
    maage_ui_paragraph(ui, maage_ui_rect(content.x, content.y + 62 + subtitle_height + image_height, content.width - 24,
        article_height), 15, article->body);
    maage_ui_clip_pop(ui);
}
static void maage_ui_installation_card(maage_launcher_ui *ui, maage_rect card, const maage_launcher_installation *item, int index)
{
    maage_rect actions;
    const maage_launcher_installation *selected = maage_launcher_selected_installation(ui->app);
    int is_selected = selected == item;
    if (maage_ui_summary(ui, 4000 + index * 4, maage_ui_rect(card.x, card.y, card.width, 76), maage_ui_string(item->name), maage_ui_string(item->version), NULL,
        is_selected, maage_launcher_can_request(ui->app, MAAGE_ACTION_SELECT_INSTALLATION, item->id))) maage_launcher_request(ui->app, MAAGE_ACTION_SELECT_INSTALLATION, item->id);
    maage_ui_fill(ui, maage_ui_rect(card.x, card.y + 76, card.width, 66), ui->theme.panel);
    if (item->modpack) maage_ui_label(ui, maage_ui_rect(card.x + 16, card.y + 76, card.width - 32, 22), 13, ui->theme.muted, item->modpack);
    actions = maage_ui_rect(card.x + 16, card.y + 102, 76, ui->theme.control_height);
    if (maage_ui_button(ui, 4001 + index * 4, actions, "Edit", MAAGE_UI_NORMAL, maage_launcher_can_request(ui->app, MAAGE_ACTION_EDIT_INSTALLATION, item->id)))
        maage_launcher_request(ui->app, MAAGE_ACTION_EDIT_INSTALLATION, item->id);
    actions.x += 88;
    if (maage_ui_button(ui, 4002 + index * 4, actions, "Remove", MAAGE_UI_DANGER, maage_launcher_can_request(ui->app, MAAGE_ACTION_REMOVE_INSTALLATION, item->id)))
        maage_launcher_request(ui->app, MAAGE_ACTION_REMOVE_INSTALLATION, item->id);
    if (is_selected) maage_ui_label(ui, maage_ui_rect(actions.x + 92, actions.y + 7, card.width - 290, 22), 13, ui->theme.muted, "Selected");
}
static void maage_ui_installations(maage_launcher_ui *ui, maage_rect body)
{
    maage_rect area;
    size_t index, count = ui->app->installations.items ? ui->app->installations.count : 0;
    float top = body.width < 540 ? 124 : 86;
    const char *title = "No installations yet", *message = "Your game versions and modpacks will appear here. Installation management is not available yet.";
    if (maage_launcher_can_request(ui->app, MAAGE_ACTION_CREATE_INSTALLATION, NULL))
        message = "Create an installation to choose a game version or modpack.";
    maage_ui_heading(ui, body, "Installations", "Game versions and modpacks, all in one place.");
    if (maage_ui_button(ui, 140, maage_ui_rect(body.width < 540 ? body.x : body.x + body.width - 164,
        body.width < 540 ? body.y + 78 : body.y, 164, ui->theme.control_height), "Create installation", MAAGE_UI_PRIMARY,
        maage_launcher_can_request(ui->app, MAAGE_ACTION_CREATE_INSTALLATION, NULL))) maage_launcher_request(ui->app, MAAGE_ACTION_CREATE_INSTALLATION, NULL);
    area = maage_ui_rect(body.x, body.y + top, body.width, body.height - top);
    if (ui->app->installations.status == MAAGE_CONTENT_LOADING) { title = "Loading installations"; message = "Looking for your installed game versions..."; }
    if (ui->app->installations.status == MAAGE_CONTENT_ERROR) {
        title = "Installations could not be loaded"; message = ui->app->installations.error ? ui->app->installations.error : "Please try again later.";
    }
    if (ui->app->installations.status != MAAGE_CONTENT_READY || !count) {
        area = maage_ui_scroll(ui, area, &ui->scroll[2], 180);
        maage_ui_empty(ui, area, title, message);
    } else {
        area = maage_ui_scroll(ui, area, &ui->scroll[2], (float)count * 158);
        for (index = 0; index < count; ++index) maage_ui_installation_card(ui,
            maage_ui_rect(area.x, area.y + (float)index * 158, area.width - 8, 142), &ui->app->installations.items[index], (int)index);
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
