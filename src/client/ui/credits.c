#include "internal.h"
#include "../storage_context.h"
#include "maage/minecraft_assets.h"
#include "maage/log.h"
#include "maage/time.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>

#define CREDIT_RESOURCE_BYTES 1048576L
static maage_status credit_result(unsigned char code) {
    return maage_status_make(code, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_LOAD, 0);
}
static maage_status credit_line(maage_client_ui *ui, const char *text, size_t length, int centered) {
    maage_client_credit_line *grown, *line;
    size_t capacity;
    if (length >= sizeof(line->text)) return credit_result(MAAGE_STATUS_BUFFER_OVERFLOW);
    if (ui->credit_count == ui->credit_capacity) {
        capacity = ui->credit_capacity ? ui->credit_capacity * 2 : 64;
        if (capacity <= ui->credit_capacity || capacity > (size_t) - 1 / sizeof(*grown)) return credit_result(MAAGE_STATUS_BUFFER_OVERFLOW);
        grown = (maage_client_credit_line *) realloc(ui->credit_lines, capacity * sizeof(*grown));
        if (!grown) return credit_result(MAAGE_STATUS_ALLOCATION_FAILED);
        ui->credit_lines = grown;
        ui->credit_capacity = capacity;
    }
    line = &ui->credit_lines[ui->credit_count++];
    memcpy(line->text, text, length);
    line->text[length] = 0;
    line->centered = centered;
    return credit_result(MAAGE_STATUS_SUCCESS);
}
static maage_status credit_text(maage_client_ui *ui, const char *text) {
    const maage_skin_reference *player = maage_client_storage_player();
    const char *name = player && player->name[0] ? player->name : "Player";
    char line[80], measure[80];
    size_t length = 0, i, take;
    int centered = 0, at_start = 1;
    maage_status result;
    while (*text) {
        if (at_start && !strncmp(text, "[C]", 3)) {
            centered = 1;
            text += 3;
        }
        at_start = 0;
        if ((unsigned char) text[0] == 194 && (unsigned char) text[1] == 167 && text[2]) {
            text += 3;
            continue;
        }
        if ((unsigned char) *text == 167 && text[1]) {
            text += 2;
            continue;
        }
        if (!strncmp(text, "PLAYERNAME", 10)) {
            take = strlen(name);
            if (length + take >= sizeof(line) - 1) {
                result = credit_line(ui, line, length, centered);
                if (maage_status_failed(result)) return result;
                length = 0;
            }
            memcpy(line + length, name, take);
            length += take;
            text += 10;
            continue;
        }
        if (*text == '\r') {
            ++text;
            continue;
        }
        memcpy(measure, line, length);
        measure[length] = 0;
        if (*text == '\n' || length >= sizeof(line) - 1 || maage_client_ui_text_width(ui, measure) >= 274) {
            take = length;
            if (*text != '\n') {
                for (i = length; i > 0; --i)
                    if (line[i - 1] == ' ') {
                        take = i - 1;
                        break;
                    }
            }
            result = credit_line(ui, line, take, centered);
            if (maage_status_failed(result)) return result;
            i = take < length && line[take] == ' ' ? take + 1 : take;
            memmove(line, line + i, length - i);
            length -= i;
            if (*text == '\n') {
                ++text;
                centered = 0;
                at_start = 1;
            }
            continue;
        }
        if (*text == '\t') line[length++] = ' ';
        else if ((unsigned char) *text >= 32 && (unsigned char) *text < 127) line[length++] = *text;
        ++text;
    }
    if (length) {
        result = credit_line(ui, line, length, centered);
        if (maage_status_failed(result)) return result;
    }
    return credit_line(ui, "", 0, 0);
}
static maage_status credit_file(maage_client_ui *ui, const char *root, const char *source, maage_status_trace *trace) {
    char path[MAAGE_PATH_CAPACITY], *text;
    FILE *file;
    long length;
    size_t read;
    maage_status result = maage_minecraft_asset_path(root, source, path, sizeof(path), trace);
    if (maage_status_failed(result)) return result;
    file = fopen(path, "rb");
    if (!file) return maage_status_from_errno(errno, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_OPEN);
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 || length > CREDIT_RESOURCE_BYTES || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return credit_result(MAAGE_STATUS_MALFORMED_INPUT);
    }
    text = (char *) malloc((size_t) length + 1);
    if (!text) {
        fclose(file);
        return credit_result(MAAGE_STATUS_ALLOCATION_FAILED);
    }
    read = fread(text, 1, (size_t) length, file);
    result = read == (size_t) length && !ferror(file) ? credit_result(MAAGE_STATUS_SUCCESS) : credit_result(MAAGE_STATUS_IO_ERROR);
    if (fclose(file) && !maage_status_failed(result)) result = credit_result(MAAGE_STATUS_IO_ERROR);
    text[read] = 0;
    if (!maage_status_failed(result)) result = credit_text(ui, text);
    free(text);
    return result;
}
static unsigned int credit_input(maage_client_ui *ui, const maage_client_ui_input *input, void *context) {
    maage_session_command command;
    double now = maage_time_monotonic_seconds();
    (void) context;
    if (!ui->credit_done && (input->escape || now - ui->credit_started > ((double) ui->credit_count * 12 + ui->content.height + 120) / 10.0)) {
        memset(&command, 0, sizeof(command));
        command.kind = MAAGE_CMD_CREDITS_DONE;
        ui->route_result = maage_client_ui_queue_command(ui, &command);
        if (!maage_status_failed(ui->route_result)) ui->credit_done = 1;
    }
    return 0;
}
static void credit_draw(maage_client_ui *ui, void *context) {
    maage_client_ui_rect old = ui->clip, full, logo, fade;
    double scroll = (ui->notice_now - ui->credit_started) * 10;
    float y, center = ui->content.x + ui->content.width * 0.5f, u, shade;
    size_t i, first;
    (void) context;
    if (scroll < 0) scroll = 0;
    full.x = full.y = 0;
    full.width = ui->width;
    full.height = ui->height;
    ui->clip = full;
    maage_client_ui_menu_background_scroll(ui, (float) fmod(scroll * 0.5, 32));
    /* Keep the repeating dirt subdued behind the roll. */
    shade = scroll < 50 ? (float) (scroll / 50) : 1;
    maage_client_ui_panel(ui, full, 0, 0, 0, 0.45f + (1 - shade) * 0.35f);
    ui->clip = ui->content;
    logo.x = center - 137;
    logo.y = ui->content.y + ui->content.height + 20 - (float) scroll;
    logo.width = 155;
    logo.height = 44;
    if (ui->resources.title_available[0]) {
        u = 512.0f / ui->resources.atlas_edge;
        maage_client_ui_rectangle(ui, logo, u, 0, u + 155.0f / ui->resources.atlas_edge, 44.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
        logo.x += 155;
        maage_client_ui_rectangle(ui, logo, u, 45.0f / ui->resources.atlas_edge, u + 155.0f / ui->resources.atlas_edge, 89.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    }
    first = scroll > ui->content.height + 120 ? (size_t) ((scroll - ui->content.height - 120) / 12) : 0;
    for (i = first; i < ui->credit_count; ++i) {
        y = ui->content.y + ui->content.height + 120 + (float) ((double) i * 12 - scroll);
        if (y > ui->content.y + ui->content.height) break;
        if (y < ui->content.y - 12) continue;
        maage_client_ui_text_shadow(ui, ui->credit_lines[i].text, ui->credit_lines[i].centered ? center : center - 137, y, ui->credit_lines[i].centered ? MAAGE_CLIENT_UI_CENTER : MAAGE_CLIENT_UI_LEFT, 1, 1, 1);
    }
    fade = ui->content;
    fade.height = 32;
    maage_client_ui_gradient(ui, fade, 0xff000000UL, 0x00000000UL);
    fade.y = ui->content.y + ui->content.height - fade.height;
    maage_client_ui_gradient(ui, fade, 0x00000000UL, 0xff000000UL);
    ui->clip = old;
}
maage_status maage_client_ui_credits_observe(maage_client_ui *ui) {
    if (ui->snapshot.credits && maage_client_ui_current(ui) != MAAGE_CLIENT_UI_CREDITS) {
        ui->navigation.depth = 0;
        ui->credit_started = maage_time_monotonic_seconds();
        ui->credit_done = 0;
        return maage_client_ui_open(ui, MAAGE_CLIENT_UI_CREDITS);
    }
    if (!ui->snapshot.credits && maage_client_ui_current(ui) == MAAGE_CLIENT_UI_CREDITS) return maage_client_ui_back(ui);
    return credit_result(MAAGE_STATUS_NOOP);
}
maage_status maage_client_ui_credits_register(maage_client_ui *ui, const char *root, const maage_presentation_registry *presentation, maage_status_trace *trace) {
    const maage_credit_definition *entry;
    size_t i;
    maage_status result;
    for (i = 0; i < maage_presentation_credit_count(presentation); ++i) {
        entry = maage_presentation_credit(presentation, i);
        result = entry->text ? credit_text(ui, entry->text) : credit_file(ui, root, entry->source, trace);
        if (maage_status_failed(result)) {
            if (maage_status_code(result) == MAAGE_STATUS_ALLOCATION_FAILED || maage_status_code(result) == MAAGE_STATUS_BUFFER_OVERFLOW) return result;
            maage_log_status(MAAGE_LOG_WARNING, "maage.client.credits", result, "Optional credits resource unavailable: %s.", entry->source ? entry->source : entry->name);
            maage_status_trace_init(trace);
        }
    }
    return maage_client_ui_register_screen(ui, MAAGE_CLIENT_UI_CREDITS, credit_draw, credit_input, NULL);
}
