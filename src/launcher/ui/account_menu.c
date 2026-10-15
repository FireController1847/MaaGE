#include "widgets.h"

static void maage_ui_account_item(maage_launcher_ui *ui, maage_rect item, const maage_launcher_account *account, int index)
{
    int selected = account == maage_launcher_selected_account(ui->app);
    const char *subtitle = !account->authenticated ? "Sign-in required" : selected ? "Selected" : account->provider ? account->provider : "";
    if (maage_ui_summary(ui, 8000 + index, item, account->display_name ? account->display_name : "Account", subtitle, account->avatar, selected,
        maage_launcher_can_request(ui->app, MAAGE_ACTION_SELECT_ACCOUNT, account->id))) maage_launcher_request(ui->app, MAAGE_ACTION_SELECT_ACCOUNT, account->id);
}
void maage_ui_account_menu(maage_launcher_ui *ui)
{
    const maage_launcher_account *account = maage_launcher_selected_account(ui->app);
    size_t index, count = ui->app->accounts.items ? ui->app->accounts.count : 0;
    maage_rect menu, content;
    float width = ui->safe.width > 352 ? 328 : ui->safe.width - 24;
    float height = 210 + (float)count * 84;
    const char *message;
    if (ui->app->accounts.status == MAAGE_ACCOUNT_AUTHENTICATING || ui->app->accounts.status == MAAGE_ACCOUNT_ERROR) height += 52;
    if (height > ui->safe.height - 70) height = ui->safe.height - 70;
    menu = maage_ui_rect(ui->safe.x + ui->safe.width - width - 12, ui->account_anchor.y + ui->account_anchor.height + 8, width, height);
    if (menu.y + menu.height > ui->safe.y + ui->safe.height - 12) menu.y = ui->safe.y + ui->safe.height - menu.height - 12;
    ui->blocked = 0;
    if (ui->input.pressed && !maage_ui_contains(menu, ui->input.x, ui->input.y) && !maage_ui_contains(ui->account_anchor, ui->input.x, ui->input.y)) {
        ui->account_menu = 0; ui->active = 0; ui->focus = 7;
        return;
    }
    maage_ui_fill(ui, maage_ui_rect(menu.x - 1, menu.y - 1, menu.width + 2, menu.height + 2), ui->theme.line);
    maage_ui_fill(ui, menu, ui->theme.panel);
    content = maage_ui_scroll(ui, maage_ui_rect(menu.x + 16, menu.y + 16, menu.width - 32, menu.height - 32), &ui->scroll[6], 178 + (float)count * 84 +
        (ui->app->accounts.status == MAAGE_ACCOUNT_AUTHENTICATING || ui->app->accounts.status == MAAGE_ACCOUNT_ERROR ? 52 : 0));
    maage_ui_label(ui, maage_ui_rect(content.x, content.y, content.width, 28), 19, ui->theme.text, "Microsoft Accounts");
    switch (ui->app->accounts.status) {
    case MAAGE_ACCOUNT_AUTHENTICATING: message = "Waiting for sign-in to complete..."; break;
    case MAAGE_ACCOUNT_ERROR: message = ui->app->accounts.error ? ui->app->accounts.error : "Sign-in could not be completed."; break;
    case MAAGE_ACCOUNT_SIGNED_IN: message = account && account->display_name ? account->display_name : "Choose an account"; break;
    default: message = "You are not signed in."; break;
    }
    maage_ui_paragraph(ui, maage_ui_rect(content.x, content.y + 38, content.width, 68), 14, message);
    content.y += ui->app->accounts.status == MAAGE_ACCOUNT_AUTHENTICATING || ui->app->accounts.status == MAAGE_ACCOUNT_ERROR ? 116 : 74;
    for (index = 0; index < count; ++index) maage_ui_account_item(ui, maage_ui_rect(content.x, content.y + (float)index * 84, content.width - 4, 76), &ui->app->accounts.items[index], (int)index);
    content.y += (float)count * 84 + 8;
    if (ui->app->accounts.status == MAAGE_ACCOUNT_AUTHENTICATING) {
        if (maage_ui_button(ui, 700, maage_ui_rect(content.x, content.y, content.width, ui->theme.control_height), "Cancel sign-in", MAAGE_UI_NORMAL,
            maage_launcher_can_request(ui->app, MAAGE_ACTION_CANCEL_SIGN_IN, NULL))) maage_launcher_request(ui->app, MAAGE_ACTION_CANCEL_SIGN_IN, NULL);
    } else if (maage_ui_button(ui, 700, maage_ui_rect(content.x, content.y, content.width, ui->theme.control_height), "Sign in with Microsoft", MAAGE_UI_PRIMARY,
        maage_launcher_can_request(ui->app, MAAGE_ACTION_SIGN_IN, NULL))) maage_launcher_request(ui->app, MAAGE_ACTION_SIGN_IN, NULL);
    if (ui->app->accounts.status == MAAGE_ACCOUNT_SIGNED_IN && account) {
        if (maage_ui_button(ui, 701, maage_ui_rect(content.x, content.y + 44, content.width, ui->theme.control_height), "Sign out", MAAGE_UI_DANGER,
            maage_launcher_can_request(ui->app, MAAGE_ACTION_SIGN_OUT, account->id))) maage_launcher_request(ui->app, MAAGE_ACTION_SIGN_OUT, account->id);
    } else if (!ui->app->accounts.service.request) maage_ui_paragraph(ui, maage_ui_rect(content.x, content.y + 44, content.width, 52), 13, "Microsoft sign-in is not available yet.");
    maage_ui_clip_pop(ui);
}
