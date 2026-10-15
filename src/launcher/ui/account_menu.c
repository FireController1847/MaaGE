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
    float width = ui->safe.width > 416 ? 368 : ui->safe.width - 48;
    float height, message_height, help_height, y;
    int authenticating = ui->app->accounts.status == MAAGE_ACCOUNT_AUTHENTICATING;
    int signed_in = ui->app->accounts.status == MAAGE_ACCOUNT_SIGNED_IN && account;
    const char *message, *help = !ui->app->accounts.service.request ? "Microsoft sign-in is not available yet." : "";
    switch (ui->app->accounts.status) {
    case MAAGE_ACCOUNT_AUTHENTICATING: message = "Waiting for sign-in to complete..."; break;
    case MAAGE_ACCOUNT_ERROR: message = ui->app->accounts.error ? ui->app->accounts.error : "Sign-in could not be completed."; break;
    case MAAGE_ACCOUNT_SIGNED_IN: message = account && account->display_name ? account->display_name : "Choose an account"; break;
    default: message = "You are not signed in."; break;
    }
    message_height = maage_ui_paragraph_height(ui, width - ui->theme.padding * 2, ui->theme.body_size, message);
    help_height = signed_in ? ui->theme.control_height : maage_ui_paragraph_height(ui, width - ui->theme.padding * 2, ui->theme.small_size, help);
    height = 32 + ui->theme.gap + message_height + ui->theme.gap + (float)count * 88 + ui->theme.control_height + ui->theme.gap + help_height;
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
    if (maage_ui_button(ui, 700, maage_ui_rect(content.x, content.y, content.width, ui->theme.control_height), authenticating ? "Cancel sign-in" : "Sign in with Microsoft",
        authenticating ? MAAGE_UI_NORMAL : MAAGE_UI_PRIMARY, maage_launcher_can_request(ui->app, authenticating ? MAAGE_ACTION_CANCEL_SIGN_IN : MAAGE_ACTION_SIGN_IN, NULL)))
        maage_launcher_request(ui->app, authenticating ? MAAGE_ACTION_CANCEL_SIGN_IN : MAAGE_ACTION_SIGN_IN, NULL);
    content.y += ui->theme.control_height + ui->theme.gap;
    if (signed_in) {
        if (maage_ui_button(ui, 701, maage_ui_rect(content.x, content.y, content.width, ui->theme.control_height), "Sign out", MAAGE_UI_DANGER,
            maage_launcher_can_request(ui->app, MAAGE_ACTION_SIGN_OUT, account->id))) maage_launcher_request(ui->app, MAAGE_ACTION_SIGN_OUT, account->id);
    } else maage_ui_paragraph(ui, maage_ui_rect(content.x, content.y, content.width, help_height), ui->theme.small_size, help);
    maage_ui_clip_pop(ui);
}
