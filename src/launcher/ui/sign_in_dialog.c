#include "widgets.h"
#include "maage/time.h"
#include <stdio.h>

static void maage_ui_sign_in_center(maage_launcher_ui *ui, maage_rect rect, float size, const char *text)
{
    float width = maage_canvas_text_width(ui->canvas, size, text);
    if (width < rect.width) rect.x += (rect.width - width) / 2;
    maage_ui_label(ui, rect, size, ui->theme.text, text);
}
void maage_ui_sign_in_dialog(maage_launcher_ui *ui)
{
    const maage_launcher_sign_in *sign_in = &ui->app->accounts.sign_in;
    const char *instructions = sign_in->can_link ?
        "Open the website below on this or another device. Enter this code, sign in with Microsoft, and approve linking your account to MaaGE." :
        "Keep this window open while MaaGE securely links and verifies your Microsoft account.";
    const char *progress = ui->app->accounts.progress ? ui->app->accounts.progress : "Preparing sign-in...";
    maage_rect panel, content, code, button, track;
    maage_color shade = {0, 0, 0, 170};
    float width = ui->safe.width - 48, height, instruction_height, url_height = 0, progress_height, hint_height = 0, half;
    double now = maage_time_monotonic_seconds(), phase;
    unsigned long remaining = (unsigned long)(sign_in->seconds_remaining > 0 ? sign_in->seconds_remaining : 0);
    char expiry[64];
    int stacked, storage = ui->app->accounts.storage_decision_required;
    if (width > 520) width = 520;
    if (width < 1) return;
    stacked = width < 400;
    instruction_height = maage_ui_paragraph_height(ui, width - 48, ui->theme.body_size, instructions);
    progress_height = maage_ui_paragraph_height(ui, width - 48, ui->theme.body_size, progress);
    if (sign_in->can_link) url_height = maage_ui_paragraph_height(ui, width - 48, ui->theme.body_size, sign_in->verification_uri);
    if (sign_in->can_link && sign_in->hint && *sign_in->hint)
        hint_height = maage_ui_paragraph_height(ui, width - 48, ui->theme.small_size, sign_in->hint) + 12;
    height = 44 + instruction_height + 16 + progress_height + 32 + ui->theme.control_height +
        (sign_in->can_link ? url_height + 120 + (stacked ? 2 : 1) * (ui->theme.control_height + 12) + 32 : 0) + hint_height +
        (storage ? ui->theme.control_height + 12 : 0);
    panel = maage_ui_rect(ui->safe.x + (ui->safe.width - width) / 2, ui->safe.y + 24, width, height + 48);
    if (panel.height > ui->safe.height - 48) panel.height = ui->safe.height - 48;
    if (panel.height < 0) panel.height = 0;
    panel.y = ui->safe.y + (ui->safe.height - panel.height) / 2;
    maage_ui_fill(ui, ui->safe, shade);
    ui->blocked = 0;
    content = maage_ui_panel(ui, panel);
    maage_ui_fill(ui, maage_ui_rect(panel.x, panel.y, panel.width, 3), ui->theme.accent);
    content = maage_ui_scroll(ui, content, &ui->scroll[7], height);
    maage_ui_label(ui, maage_ui_rect(content.x, content.y, content.width, 32), ui->theme.section_size, ui->theme.text, "Sign in with Microsoft");
    content.y += 44;
    maage_ui_paragraph(ui, maage_ui_rect(content.x, content.y, content.width, instruction_height), ui->theme.body_size, instructions);
    content.y += instruction_height + 16;
    if (sign_in->can_link) {
        maage_ui_paragraph_color(ui, maage_ui_rect(content.x, content.y, content.width, url_height), ui->theme.body_size, ui->theme.text, sign_in->verification_uri);
        content.y += url_height + 16;
        code = maage_ui_rect(content.x, content.y, content.width, 88);
        maage_ui_fill(ui, code, ui->theme.inset); maage_ui_outline(ui, code, ui->theme.line);
        maage_ui_label(ui, maage_ui_rect(code.x + 16, code.y + 10, code.width - 32, 22), ui->theme.small_size, ui->theme.muted, "YOUR SIGN-IN CODE");
        maage_ui_sign_in_center(ui, maage_ui_rect(code.x + 8, code.y + 34, code.width - 16, 46), 32, sign_in->user_code);
        content.y += 104;
        half = stacked ? content.width : (content.width - 12) / 2;
        button = maage_ui_rect(content.x, content.y, half, ui->theme.control_height);
        if (maage_ui_button(ui, 9100, button, "Copy Code", MAAGE_UI_NORMAL, sign_in->request != NULL))
            sign_in->request(sign_in->context, MAAGE_SIGN_IN_COPY_CODE);
        if (stacked) { content.y += ui->theme.control_height + 12; button.y = content.y; }
        else button.x += half + 12;
        if (maage_ui_button(ui, 9101, button, "Open Browser", MAAGE_UI_PRIMARY, sign_in->request != NULL))
            sign_in->request(sign_in->context, MAAGE_SIGN_IN_OPEN_BROWSER);
        content.y += ui->theme.control_height + 12;
        sprintf(expiry, "Code expires in %lu:%02lu", remaining / 60, remaining % 60);
        maage_ui_label(ui, maage_ui_rect(content.x, content.y, content.width, 24), ui->theme.small_size, ui->theme.muted, expiry);
        content.y += 32;
        if (hint_height) {
            maage_ui_paragraph(ui, maage_ui_rect(content.x, content.y, content.width, hint_height), ui->theme.small_size, sign_in->hint);
            content.y += hint_height;
        }
    }
    maage_ui_paragraph(ui, maage_ui_rect(content.x, content.y, content.width, progress_height), ui->theme.body_size, progress);
    content.y += progress_height + 12;
    track = maage_ui_rect(content.x, content.y, content.width, 3);
    maage_ui_fill(ui, track, ui->theme.line);
    phase = now > 0 ? now / 2 - (double)(unsigned long)(now / 2) : 0;
    track.x += (float)phase * track.width * 0.75f; track.width *= 0.25f;
    maage_ui_fill(ui, track, ui->theme.accent);
    content.y += 20;
    if (storage) {
        if (maage_ui_button(ui, 9102, maage_ui_rect(content.x, content.y, content.width, ui->theme.control_height), "Continue for This Session", MAAGE_UI_PRIMARY,
            maage_launcher_can_request(ui->app, MAAGE_ACTION_SESSION_ONLY, NULL))) maage_launcher_request(ui->app, MAAGE_ACTION_SESSION_ONLY, NULL);
        content.y += ui->theme.control_height + 12;
    }
    if (maage_ui_button(ui, 9103, maage_ui_rect(content.x, content.y, content.width, ui->theme.control_height), "Cancel", MAAGE_UI_NORMAL,
        maage_launcher_can_request(ui->app, MAAGE_ACTION_CANCEL_SIGN_IN, NULL))) maage_launcher_request(ui->app, MAAGE_ACTION_CANCEL_SIGN_IN, NULL);
    maage_ui_clip_pop(ui);
}
