#include "options.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void maage_client_options_defaults(maage_client_options *options) {
    if (!options) return;
    options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_MASTER] = MAAGE_CLIENT_AUDIO_DEFAULT_MASTER;
    options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_BLOCKS] = MAAGE_CLIENT_AUDIO_DEFAULT_BLOCKS;
    options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_PLAYER] = MAAGE_CLIENT_AUDIO_DEFAULT_PLAYER;
    options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_AMBIENT] = MAAGE_CLIENT_AUDIO_DEFAULT_AMBIENT;
    options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_WEATHER] = MAAGE_CLIENT_AUDIO_DEFAULT_WEATHER;
    options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_MUSIC] = MAAGE_CLIENT_AUDIO_DEFAULT_MUSIC;
    options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_UI] = MAAGE_CLIENT_AUDIO_DEFAULT_UI;
    options->fov_degrees = 70;
    options->max_fps = 240;
    options->vsync = 1;
    options->view_bobbing = 1;
    options->gui_scale = 0;
    options->leaves = MAAGE_LEAVES_FANCY;
    options->biome_blend = MAAGE_BIOME_BLEND_SMOOTH_5;
    options->window_mode = MAAGE_CLIENT_WINDOWED;
    options->fullscreen_style = MAAGE_CLIENT_BORDERLESS;
}

int maage_client_options_equal(const maage_client_options *a, const maage_client_options *b) {
    int i;
    if (!a || !b || a->fov_degrees != b->fov_degrees ||
        a->max_fps != b->max_fps || a->vsync != b->vsync || a->view_bobbing != b->view_bobbing ||
        a->gui_scale != b->gui_scale || a->leaves != b->leaves || a->biome_blend != b->biome_blend ||
        a->window_mode != b->window_mode || a->fullscreen_style != b->fullscreen_style)
        return 0;
    for (i = 0; i < MAAGE_CLIENT_AUDIO_BUS_COUNT; ++i)
        if (a->audio_volume[i] != b->audio_volume[i]) return 0;
    return 1;
}

static char *maage_client_options_trim(char *text) {
    char *end;
    while (isspace((unsigned char) *text))
        ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char) end[-1]))
        --end;
    *end = 0;
    return text;
}

static int maage_client_options_integer(const char *text, int minimum, int maximum, int step, int *value) {
    const unsigned char *cursor = (const unsigned char *) text;
    char *end;
    long number;
    if (!*cursor) return 0;
    for (; *cursor; ++cursor)
        if (!isdigit(*cursor)) return 0;
    errno = 0;
    number = strtol(text, &end, 10);
    if (errno || *end || number < minimum || number > maximum || (step && number % step)) return 0;
    *value = (int) number;
    return 1;
}

static void maage_client_options_parse(maage_client_options *options, char *line, int *section) {
    char *key, *value, *equals;
    static const char *const audio_keys[MAAGE_CLIENT_AUDIO_BUS_COUNT] = {
        MAAGE_CLIENT_AUDIO_KEY_MASTER, MAAGE_CLIENT_AUDIO_KEY_BLOCKS,
        MAAGE_CLIENT_AUDIO_KEY_PLAYER, MAAGE_CLIENT_AUDIO_KEY_AMBIENT,
        MAAGE_CLIENT_AUDIO_KEY_WEATHER, MAAGE_CLIENT_AUDIO_KEY_MUSIC,
        MAAGE_CLIENT_AUDIO_KEY_UI};
    int number, bus;
    key = maage_client_options_trim(line);
    if (!*key || *key == '#' || *key == ';') return;
    if (*key == '[') {
        *section = !strcmp(key, "[video]") ? 1 : (!strcmp(key, "[audio]") ? 2 : 0);
        return;
    }
    if (!*section) return;
    equals = strchr(key, '=');
    if (!equals) return;
    *equals = 0;
    value = maage_client_options_trim(equals + 1);
    key = maage_client_options_trim(key);
    if (*section == 2) {
        for (bus = 0; bus < MAAGE_CLIENT_AUDIO_BUS_COUNT; ++bus) {
            if (!strcmp(key, audio_keys[bus])) {
                /* An invalid single value cannot invalidate the whole settings file. */
                if (maage_client_options_integer(value, MAAGE_CLIENT_AUDIO_VOLUME_MIN, MAAGE_CLIENT_AUDIO_VOLUME_MAX, 1, &number)) options->audio_volume[bus] = number;
                break;
            }
        }
        return;
    }
    if (!strcmp(key, "fov_degrees")) {
        if (maage_client_options_integer(value, 30, 110, 1, &number)) options->fov_degrees = number;
    } else if (!strcmp(key, "max_fps")) {
        options->max_fps = maage_client_options_integer(value, 0, 260, 10, &number) ? number : 0;
    } else if (!strcmp(key, "vsync")) {
        if (!strcmp(value, "true")) options->vsync = 1;
        else if (!strcmp(value, "false")) options->vsync = 0;
        else
            options->vsync = 1;
    } else if (!strcmp(key, "view_bobbing")) {
        if (!strcmp(value, "true")) options->view_bobbing = 1;
        else if (!strcmp(value, "false")) options->view_bobbing = 0;
    } else if (!strcmp(key, "gui_scale")) {
        options->gui_scale = maage_client_options_integer(value, 0, 4, 1, &number) ? number : 0;
    } else if (!strcmp(key, "biome_blend")) {
        if (maage_client_options_integer(value, MAAGE_BIOME_BLEND_OFF, MAAGE_BIOME_BLEND_SMOOTH_9, 1, &number)) options->biome_blend = (maage_biome_blend) number;
    } else if (!strcmp(key, "leaves")) {
        if (!strcmp(value, "fast")) options->leaves = MAAGE_LEAVES_FAST;
        else if (!strcmp(value, "smart")) options->leaves = MAAGE_LEAVES_SMART;
        else
            options->leaves = MAAGE_LEAVES_FANCY;
    } else if (!strcmp(key, "window_mode")) {
        if (!strcmp(value, "borderless")) options->window_mode = MAAGE_CLIENT_BORDERLESS;
        else if (!strcmp(value, "fullscreen")) options->window_mode = MAAGE_CLIENT_FULLSCREEN;
        else
            options->window_mode = MAAGE_CLIENT_WINDOWED;
    } else if (!strcmp(key, "fullscreen_style")) {
        options->fullscreen_style = !strcmp(value, "fullscreen") ? MAAGE_CLIENT_FULLSCREEN : MAAGE_CLIENT_BORDERLESS;
    }
}

maage_status maage_client_options_load(const maage_instance *instance, maage_client_options *options) {
    char path[MAAGE_PATH_CAPACITY], line[256];
    FILE *file;
    maage_status result;
    int status, section = 0;
    if (!instance || !options) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_LOAD, 0);
    maage_client_options_defaults(options);
    result = maage_instance_resolve_path(instance, MAAGE_INSTANCE_FOLDER_CONFIG, "options.ini", path, sizeof(path));
    if (maage_status_failed(result)) return result;
    status = maage_storage_status(path);
    if (!status) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD, 0);
    if (status != 1) return maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_LOAD, 0);
    file = maage_storage_open(path, "rb");
    if (!file) return maage_status_from_errno(errno, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_LOAD);
    while (fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        if (length == sizeof(line) - 1 && line[length - 1] != '\n') {
            int character;
            while ((character = fgetc(file)) != '\n' && character != EOF) {
            }
            continue; /* Never interpret truncated lines as options. */
        }
        maage_client_options_parse(options, line, &section);
    }
    result = ferror(file) ? maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_READ, 0) : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD, 0);
    if (fclose(file) && !maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CLOSE, 0);
    if (maage_status_failed(result)) maage_client_options_defaults(options);
    return result;
}

maage_status maage_client_options_save(const maage_instance *instance, const maage_client_options *options) {
    static const char *const leaves[] = {"fast", "smart", "fancy"};
    static const char *const modes[] = {"windowed", "borderless", "fullscreen"};
    char destination[MAAGE_PATH_CAPACITY], temporary[MAAGE_PATH_CAPACITY];
    FILE *file;
    maage_status result;
    int status, bus;
    if (!instance || !options || options->fov_degrees < 30 ||
        options->fov_degrees > 110 || (options->max_fps != 0 && (options->max_fps < 10 || options->max_fps > 260 || options->max_fps % 10)) ||
        (options->vsync != 0 && options->vsync != 1) ||
        (options->view_bobbing != 0 && options->view_bobbing != 1) ||
        options->gui_scale < 0 || options->gui_scale > 4 ||
        options->biome_blend < MAAGE_BIOME_BLEND_OFF || options->biome_blend > MAAGE_BIOME_BLEND_SMOOTH_9 ||
        options->leaves < MAAGE_LEAVES_FAST || options->leaves > MAAGE_LEAVES_FANCY ||
        options->window_mode < MAAGE_CLIENT_WINDOWED || options->window_mode > MAAGE_CLIENT_FULLSCREEN ||
        options->fullscreen_style < MAAGE_CLIENT_BORDERLESS ||
        options->fullscreen_style > MAAGE_CLIENT_FULLSCREEN)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SAVE, 0);
    for (bus = 0; bus < MAAGE_CLIENT_AUDIO_BUS_COUNT; ++bus) {
        if (options->audio_volume[bus] < MAAGE_CLIENT_AUDIO_VOLUME_MIN || options->audio_volume[bus] > MAAGE_CLIENT_AUDIO_VOLUME_MAX) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SAVE, 0);
    }
    result = maage_instance_resolve_path(instance, MAAGE_INSTANCE_FOLDER_CONFIG, "options.ini", destination, sizeof(destination));
    if (maage_status_failed(result)) return result;
    result = maage_instance_resolve_path(instance, MAAGE_INSTANCE_FOLDER_CONFIG, "options.ini.tmp", temporary, sizeof(temporary));
    if (maage_status_failed(result)) return result;
    status = maage_storage_status(temporary);
    if (status < 0 || status == 2) return maage_status_make(MAAGE_STATUS_ACCESS_DENIED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SAVE, 0);
    file = maage_storage_open(temporary, "wb");
    if (!file) return maage_status_from_errno(errno, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SAVE);
    if (fprintf(file, "[video]\nfov_degrees=%d\nmax_fps=%d\nvsync=%s\nview_bobbing=%s\ngui_scale=%d\nleaves=%s\nbiome_blend=%d\n"
                      "window_mode=%s\nfullscreen_style=%s\n\n"
                      "[audio]\nmaster=%d\nblocks=%d\nplayer=%d\nambient=%d\nweather=%d\nmusic=%d\nui=%d\n",
                options->fov_degrees, options->max_fps,
                options->vsync ? "true" : "false", options->view_bobbing ? "true" : "false", options->gui_scale, leaves[options->leaves], (int) options->biome_blend,
                modes[options->window_mode], modes[options->fullscreen_style],
                options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_MASTER],
                options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_BLOCKS],
                options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_PLAYER],
                options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_AMBIENT],
                options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_WEATHER],
                options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_MUSIC],
                options->audio_volume[MAAGE_CLIENT_AUDIO_BUS_UI]) < 0)
        result = maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_WRITE, 0);
    else
        result = maage_storage_sync(file);
    if (fclose(file) && !maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CLOSE, 0);
    if (!maage_status_failed(result)) result = maage_storage_replace(temporary, destination);
    if (maage_status_failed(result)) maage_storage_remove(temporary);
    return result;
}
