#include "widgets.h"

static void maage_ui_account_item(maage_launcher_ui *ui, maage_rect item, const maage_launcher_account *account, int index)
{
    int selected = account == maage_launcher_selected_account(ui->app);
    if (maage_ui_summary(ui, 8000 + index, item, account->display_name ? account->display_name : "Microsoft account",
        account->status_message ? account->status_message : "Sign-in required", account->avatar, selected,
        maage_launcher_can_request(ui->app, MAAGE_ACTION_SELECT_ACCOUNT, account->id)))
        maage_launcher_request(ui->app, MAAGE_ACTION_SELECT_ACCOUNT, account->id);
}
static void maage_ui_account_action(maage_launcher_ui *ui, maage_rect *content, int id, const char *label,
    maage_launcher_action action, const char *account, int style)
{
    if (maage_ui_button(ui, id, maage_ui_rect(content->x, content->y, content->width, ui->theme.control_height), label, style,
        maage_launcher_can_request(ui->app, action, account))) maage_launcher_request(ui->app, action, account);
    content->y += ui->theme.control_height + ui->theme.gap;
}
void maage_ui_account_menu(maage_launcher_ui *ui)
{
    const maage_launcher_account *account = maage_launcher_selected_account(ui->app);
    size_t index, count = ui->app->accounts.items ? ui->app->accounts.count : 0;
    maage_rect menu, content;
    float width = ui->safe.width > 416 ? 368 : ui->safe.width - 48;
    float height, message_height, uuid_height = 0, y;
    int authenticating = ui->app->accounts.status == MAAGE_ACCOUNT_AUTHENTICATING;
    int storage = ui->app->accounts.storage_decision_required;
    const char *message = "Sign in with Microsoft to verify Minecraft Java ownership.";
    if (authenticating) message = ui->app->accounts.progress;
    else if (ui->app->accounts.status == MAAGE_ACCOUNT_ERROR) message = ui->app->accounts.error;
    else if (account) message = account->status_message;
    if (!message || !*message) message = "Choose or add a Microsoft account.";
    message_height = maage_ui_paragraph_height(ui, width - ui->theme.padding * 2, ui->theme.body_size, message);
    if (account && account->uuid && *account->uuid)
        uuid_height = maage_ui_paragraph_height(ui, width - ui->theme.padding * 2, ui->theme.small_size, account->uuid) + ui->theme.gap;
    height = 32 + ui->theme.gap + message_height + ui->theme.gap + (float)count * 88 + uuid_height +
        (float)(storage ? 2 : authenticating ? 1 : account ? 5 : 1) * (ui->theme.control_height + ui->theme.gap);
    y = ui->account_anchor.y + ui->account_anchor.height + 8;
    menu = maage_ui_rect(ui->safe.x + ui->safe.width - width - 24, y, width, height + ui->theme.padding * 2);
    if (menu.height > ui->safe.y + ui->safe.height - y - 16) menu.height = ui->safe.y + ui->safe.height - y - 16;
    if (menu.height < 0) menu.height = 0;
    ui->blocked = 0;
    if (ui->input.pressed && !maage_ui_contains(menu, ui->input.x, ui->input.y) && !maage_ui_contains(ui->account_anchor, ui->input.x, ui->input.y)) {
        ui->account_menu = 0; ui->active = 0; ui->focus = 7;
        return;
    }
    content = maage_ui_panel(ui, menu);
    content = maage_ui_scroll(ui, content, &ui->scroll[6], height);
    maage_ui_label(ui, maage_ui_rect(content.x, content.y, content.width, 32), ui->theme.section_size, ui->theme.text, "Microsoft Accounts");
    content.y += 32 + ui->theme.gap;
    maage_ui_paragraph(ui, maage_ui_rect(content.x, content.y, content.width, message_height), ui->theme.body_size, message);
    content.y += message_height + ui->theme.gap;
    for (index = 0; index < count; ++index) maage_ui_account_item(ui, maage_ui_rect(content.x, content.y + (float)index * 88, content.width - 4, 76), &ui->app->accounts.items[index], (int)index);
    content.y += (float)count * 88;
    if (uuid_height) {
        maage_ui_paragraph(ui, maage_ui_rect(content.x, content.y, content.width, uuid_height), ui->theme.small_size, account->uuid);
        content.y += uuid_height;
    }
    if (storage) maage_ui_account_action(ui, &content, 702, "Continue for this session", MAAGE_ACTION_SESSION_ONLY, NULL, MAAGE_UI_PRIMARY);
    if (authenticating || storage) maage_ui_account_action(ui, &content, 700, "Cancel", MAAGE_ACTION_CANCEL_SIGN_IN, NULL, MAAGE_UI_NORMAL);
    else {
        maage_ui_account_action(ui, &content, 700, count ? "Add Microsoft account" : "Sign in with Microsoft", MAAGE_ACTION_SIGN_IN, NULL, MAAGE_UI_PRIMARY);
        if (account) {
            maage_ui_account_action(ui, &content, 703, "Refresh session", MAAGE_ACTION_REFRESH_SESSION, account->id, MAAGE_UI_NORMAL);
            maage_ui_account_action(ui, &content, 704, "Sign in again", MAAGE_ACTION_REAUTH_ACCOUNT, account->id, MAAGE_UI_NORMAL);
            maage_ui_account_action(ui, &content, 701, "Sign out", MAAGE_ACTION_SIGN_OUT, account->id, MAAGE_UI_NORMAL);
            maage_ui_account_action(ui, &content, 705, "Remove account", MAAGE_ACTION_REMOVE_ACCOUNT, account->id, MAAGE_UI_DANGER);
        }
    }
    maage_ui_clip_pop(ui);
}
