#include "internal.h"
#include <stdio.h>
#include <string.h>

/* Two 150x20 columns around the centered
 * GUI, 24 logical pixels between rows, and a single centered Done button.
 * Keep only implemented controls active; missing categories are intentionally
 * visible, but disabled rather than impersonating implemented screens. */
#define MAAGE_OPTIONS_MAIN_COUNT 12U
#define MAAGE_OPTIONS_VIDEO_COUNT 18U
#define MAAGE_OPTIONS_SOUND_COUNT 8U
#define MAAGE_OPTIONS_VIDEO_DONE 17U
#define MAAGE_OPTIONS_SOUND_DONE 7U
#define MAAGE_OPTIONS_MAIN_DONE 11U

static const unsigned int maage_sound_buses[7] = {
    MAAGE_CLIENT_AUDIO_BUS_MASTER, MAAGE_CLIENT_AUDIO_BUS_MUSIC,
    MAAGE_CLIENT_AUDIO_BUS_BLOCKS, MAAGE_CLIENT_AUDIO_BUS_WEATHER,
    MAAGE_CLIENT_AUDIO_BUS_PLAYER, MAAGE_CLIENT_AUDIO_BUS_AMBIENT,
    MAAGE_CLIENT_AUDIO_BUS_UI};
static const char *const maage_sound_names[7] = {
    "Master Volume", "Music", "Blocks", "Weather", "Player", "Ambient", "UI"};
static const char *const maage_video_unsupported[17] = {
    "Graphics: Fancy", "Render Distance: 8 Normal",
    "Smooth Lighting: Maximum", "", "3D Anaglyph: OFF", "",
    "", "Advanced OpenGL: OFF", "Brightness: Moody", "Clouds: ON",
    "Particles: All", "", "", "Mipmap Levels: OFF",
    "Anisotropic Filtering: OFF", "", ""};
static const char *const maage_main_labels[12] = {
    "", "Difficulty: Normal", "Super Secret Settings...",
    "Music & Sounds...", "Broadcast Settings...",
    "Video Settings...", "Controls...", "Language...",
    "Multiplayer Settings...", "Resource Packs...",
    "Snooper Settings...", "Done"};

static int maage_options_contains(maage_client_ui_rect r, float x, float y) {
    return r.width > 0.0f && r.height > 0.0f && x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}
static float maage_options_top(const maage_client_ui *ui) {
    return (float) (int) (ui->content.y + ui->content.height / 6.0f) - 12.0f;
}
static float maage_options_center(const maage_client_ui *ui) {
    return (float) (int) (ui->content.x + ui->content.width * 0.5f);
}
static float maage_options_column_width(const maage_client_ui *ui) {
    float width = (ui->content.width - 10.0f) * 0.5f;
    return width < 150.0f ? width : 150.0f;
}
/* Video screens with only 240 GUI pixels have fewer than nine fully visible
 * rows. Keep Done visible and permit vertical mouse-wheel/keyboard scrolling
 * rather than letting the last row overlap the footer. */
static unsigned int maage_options_visible_rows(const maage_client_ui *ui) {
    float available = ui->content.height - 32.0f - 36.0f - 20.0f;
    int rows = (int) (available / 25.0f) + 1;
    if (rows < 1) rows = 1;
    if (rows > 9) rows = 9;
    return (unsigned int) rows;
}
static maage_client_ui_rect maage_options_rect(const maage_client_ui *ui, maage_client_ui_screen_id screen, unsigned int id) {
    maage_client_ui_rect r;
    float center = maage_options_center(ui);
    float width = maage_options_column_width(ui);
    unsigned int col, row;
    r.width = width;
    r.height = 20.0f;
    r.x = center - 5.0f - width;
    r.y = maage_options_top(ui);
    if (screen == MAAGE_CLIENT_UI_OPTIONS) {
        if (id == MAAGE_OPTIONS_MAIN_DONE) {
            r.width = ui->content.width > 224.0f ? 200.0f : ui->content.width - 24.0f;
            r.x = center - r.width * 0.5f;
            r.y = maage_options_top(ui) + 180.0f;
            return r;
        }
        if (id == 0U || id == 1U) {
            if (id == 1U) r.x = center + 5.0f;
            return r;
        }
        if (id == 2U) {
            r.x = center + 5.0f;
            r.y += 54.0f;
            return r;
        }
        row = (id - 3U) / 2U;
        col = (id - 3U) % 2U;
        r.x = col ? center + 5.0f : center - 5.0f - width;
        r.y += 78.0f + (float) row * 24.0f;
        return r;
    }
    if (screen == MAAGE_CLIENT_UI_SOUND_SETTINGS) {
        if (id == MAAGE_OPTIONS_SOUND_DONE) {
            r.width = ui->content.width > 224.0f ? 200.0f : ui->content.width - 24.0f;
            r.x = center - r.width * 0.5f;
            r.y = maage_options_top(ui) + 180.0f;
        } else if (id == 0U) {
            r.width = width * 2.0f + 10.0f;
            r.x = center - r.width * 0.5f;
        } else {
            row = (id - 1U) / 2U;
            col = (id - 1U) % 2U;
            r.x = col ? center + 5.0f : center - 5.0f - width;
            r.y += 24.0f + (float) row * 24.0f;
        }
        return r;
    }
    if (id == MAAGE_OPTIONS_VIDEO_DONE) {
        r.width = ui->content.width > 224.0f ? 200.0f : ui->content.width - 24.0f;
        r.x = center - r.width * 0.5f;
        r.y = ui->content.y + ui->content.height - 27.0f;
        return r;
    }
    row = id / 2U;
    col = id % 2U;
    if (row < (unsigned int) ui->video_first_row || row >= (unsigned int) ui->video_first_row + maage_options_visible_rows(ui)) {
        r.width = r.height = 0.0f;
        return r;
    }
    r.x = col ? center + 5.0f : center - 5.0f - width;
    r.y = ui->content.y + 36.0f + (float) (row - (unsigned int) ui->video_first_row) * 25.0f;
    return r;
}
static unsigned int maage_options_count(maage_client_ui_screen_id screen) {
    if (screen == MAAGE_CLIENT_UI_SOUND_SETTINGS) return MAAGE_OPTIONS_SOUND_COUNT;
    if (screen == MAAGE_CLIENT_UI_VIDEO_SETTINGS) return MAAGE_OPTIONS_VIDEO_COUNT;
    return MAAGE_OPTIONS_MAIN_COUNT;
}
static int maage_options_enabled(const maage_client_ui *ui, maage_client_ui_screen_id screen, unsigned int id) {
    if (screen == MAAGE_CLIENT_UI_OPTIONS) return id == 0U || id == 3U || id == 5U || id == MAAGE_OPTIONS_MAIN_DONE;
    if (screen == MAAGE_CLIENT_UI_SOUND_SETTINGS) return id < MAAGE_OPTIONS_SOUND_COUNT;
    if (screen == MAAGE_CLIENT_UI_VIDEO_SETTINGS) {
        if (id == 3U || id == 5U || id == 6U || id == 14U || id == 15U || id == MAAGE_OPTIONS_VIDEO_DONE) return 1;
        if (id == 12U) return ui->vsync_supported;
        if (id == 11U) return !ui->mode_pending && !ui->mode_changing;
        if (id == 16U) return ui->has_snapshot;
    }
    return 0;
}
static int maage_options_slider_id(maage_client_ui_screen_id screen, unsigned int id) {
    return (screen == MAAGE_CLIENT_UI_OPTIONS && id == 0U) ||
           (screen == MAAGE_CLIENT_UI_VIDEO_SETTINGS && id == 3U) ||
           (screen == MAAGE_CLIENT_UI_SOUND_SETTINGS && id < 7U);
}
static int maage_options_hover(const maage_client_ui *ui, maage_client_ui_screen_id screen) {
    unsigned int n, count = maage_options_count(screen);
    maage_client_ui_rect r;
    for (n = 0; n < count; ++n) {
        if (!maage_options_enabled(ui, screen, n)) continue;
        r = maage_options_rect(ui, screen, n);
        if (maage_options_contains(r, ui->mouse_x, ui->mouse_y)) return (int) n;
    }
    return -1;
}
static void maage_options_focus_step(maage_client_ui *ui, maage_client_ui_screen_id screen, int direction) {
    unsigned int i, idx, count = maage_options_count(screen);
    idx = ui->selected;
    for (i = 0; i < count; ++i) {
        idx = direction > 0 ? (idx + 1U) % count : (idx + count - 1U) % count;
        if (maage_options_enabled(ui, screen, idx)) {
            ui->selected = idx;
            ui->selected_keyboard = 1;
            return;
        }
    }
}
static void maage_options_scroll_selected(maage_client_ui *ui) {
    unsigned int row, visible;
    if (ui->selected == MAAGE_OPTIONS_VIDEO_DONE) return;
    row = ui->selected / 2U;
    visible = maage_options_visible_rows(ui);
    if (row < (unsigned int) ui->video_first_row) ui->video_first_row = (int) row;
    else if (row >= (unsigned int) ui->video_first_row + visible) ui->video_first_row = (int) (row - visible + 1U);
}
static int maage_options_value_from_mouse(const maage_client_ui *ui, maage_client_ui_rect r, int maximum) {
    float fraction;
    int result;
    if (r.width <= 8.0f) return 0;
    fraction = (ui->mouse_x - r.x - 4.0f) / (r.width - 8.0f);
    result = (int) (fraction * (float) maximum + 0.5f);
    if (result < 0) result = 0;
    if (result > maximum) result = maximum;
    return result;
}
static void maage_options_drag(maage_client_ui *ui, maage_client_ui_screen_id screen, unsigned int id) {
    maage_client_ui_rect r = maage_options_rect(ui, screen, id);
    unsigned int bus;
    if (screen == MAAGE_CLIENT_UI_OPTIONS) {
        ui->fov_degrees = 30 + maage_options_value_from_mouse(ui, r, 80);
    } else if (screen == MAAGE_CLIENT_UI_VIDEO_SETTINGS) {
        ui->fps_step = maage_options_value_from_mouse(ui, r, 26);
    } else {
        bus = maage_sound_buses[id];
        ui->audio_volume[bus] = maage_options_value_from_mouse(ui, r, 100);
    }
}
static int maage_options_adjust(maage_client_ui *ui, maage_client_ui_screen_id screen, unsigned int id, int direction) {
    int *value, previous, minimum, maximum, step;
    value = NULL;
    minimum = 0;
    maximum = 100;
    step = direction;
    if (screen == MAAGE_CLIENT_UI_OPTIONS && id == 0U) {
        value = &ui->fov_degrees;
        minimum = 30;
        maximum = 110;
    } else if (screen == MAAGE_CLIENT_UI_VIDEO_SETTINGS && id == 3U) {
        value = &ui->fps_step;
        maximum = 26;
    } else if (screen == MAAGE_CLIENT_UI_SOUND_SETTINGS && id < 7U) {
        value = &ui->audio_volume[maage_sound_buses[id]];
        step *= 5;
    }
    if (!value) return 0;
    previous = *value;
    *value += step;
    if (*value < minimum) *value = minimum;
    if (*value > maximum) *value = maximum;
    return *value != previous;
}
static void maage_options_action(maage_client_ui *ui, maage_client_ui_screen_id screen, unsigned int id) {
    maage_status result;
    if (!maage_options_enabled(ui, screen, id)) return;
    if (screen == MAAGE_CLIENT_UI_OPTIONS) {
        if (id == 0U) maage_options_adjust(ui, screen, id, 1);
        else if (id == 3U) {
            ui->route_result = maage_client_ui_open(ui, MAAGE_CLIENT_UI_SOUND_SETTINGS);
        } else if (id == 5U) {
            ui->video_first_row = 0;
            ui->route_result = maage_client_ui_open(ui, MAAGE_CLIENT_UI_VIDEO_SETTINGS);
        } else if (id == MAAGE_OPTIONS_MAIN_DONE) ui->route_result = maage_client_ui_back(ui);
    } else if (screen == MAAGE_CLIENT_UI_SOUND_SETTINGS) {
        if (id == MAAGE_OPTIONS_SOUND_DONE) ui->route_result = maage_client_ui_back(ui);
        else
            maage_options_adjust(ui, screen, id, 1);
    } else {
        if (id == MAAGE_OPTIONS_VIDEO_DONE) ui->route_result = maage_client_ui_back(ui);
        else if (id == 3U) maage_options_adjust(ui, screen, id, 1);
        else if (id == 5U) ui->view_bobbing = !ui->view_bobbing;
        else if (id == 6U) ui->requested_scale = (ui->requested_scale + 1) % 5;
        else if (id == 14U) ui->biome_blend = (ui->biome_blend + 1) % 5;
        else if (id == 15U) ui->leaves_mode = (ui->leaves_mode + 1) % 3;
        else if (id == 11U) maage_client_ui_request_window_mode(ui, (maage_client_window_choice) ((ui->window_mode + 1) % 3));
        else if (id == 16U && ui->has_snapshot) maage_client_ui_queue_game_mode(ui);
        else if (id == 12U && ui->vsync_supported) {
            result = maage_renderer_set_vsync(ui->renderer, !ui->vsync);
            if (maage_status_is_success(result)) ui->vsync = !ui->vsync;
            else {
                if (maage_status_code(result) == MAAGE_STATUS_NOT_IMPLEMENTED || maage_status_code(result) == MAAGE_STATUS_SYSTEM_NOT_SUPPORTED) ui->vsync_supported = 0;
                ui->route_result = result;
            }
        }
    }
}
static unsigned int maage_options_input(maage_client_ui *ui, const maage_client_ui_input *input, void *context) {
    maage_client_ui_screen_id screen = maage_client_ui_current(ui);
    unsigned int chosen, visible;
    int hovered, click, keyboard, held, adjusted = 0;
    (void) context;
    hovered = maage_options_hover(ui, screen);
    if (ui->mouse_x != ui->previous_mouse_x || ui->mouse_y != ui->previous_mouse_y || input->primary_pressed) ui->selected_keyboard = 0;
    if (input->previous) maage_options_focus_step(ui, screen, -1);
    if (input->next) maage_options_focus_step(ui, screen, 1);
    if (screen == MAAGE_CLIENT_UI_VIDEO_SETTINGS) {
        visible = maage_options_visible_rows(ui);
        if (input->scroll != 0.0) ui->selected_keyboard = 0;
        if (input->scroll > 0.0 && ui->video_first_row > 0) --ui->video_first_row;
        if (input->scroll < 0.0 && (unsigned int) ui->video_first_row + visible < 9U) ++ui->video_first_row;
        if (input->previous || input->next) maage_options_scroll_selected(ui);
        hovered = maage_options_hover(ui, screen);
    }
    if (hovered >= 0 && !ui->selected_keyboard) ui->selected = (unsigned int) hovered;
    if (ui->selected_keyboard && (input->left || input->right) && maage_options_enabled(ui, screen, ui->selected)) {
        chosen = ui->selected;
        if (maage_options_slider_id(screen, chosen)) adjusted = maage_options_adjust(ui, screen, chosen, input->right ? 1 : -1);
        else if (screen == MAAGE_CLIENT_UI_VIDEO_SETTINGS && chosen == 6U) {
            ui->requested_scale = (ui->requested_scale + (input->right ? 1 : 4)) % 5;
            adjusted = 1;
        } else if (screen == MAAGE_CLIENT_UI_VIDEO_SETTINGS && chosen == 14U) {
            ui->biome_blend = (ui->biome_blend + (input->right ? 1 : 4)) % 5;
            adjusted = 1;
        } else if (screen == MAAGE_CLIENT_UI_VIDEO_SETTINGS && chosen == 15U) {
            ui->leaves_mode = (ui->leaves_mode + (input->right ? 1 : 2)) % 3;
            adjusted = 1;
        }
        /* Keyboard navigation is silent; an actual discrete value change is
         * the keyboard equivalent of activating the focused control. */
        if (adjusted && ui->click) ui->click(ui->click_context);
    }
    /* Keep an existing drag locked to its original slider, even when the
     * cursor leaves the widget; never replay click audio on mouse movement. */
    held = ui->dragging_fov || ui->dragging_fps || ui->dragging_volume;
    if (held && input->primary_down) {
        if (ui->dragging_fov && screen == MAAGE_CLIENT_UI_OPTIONS) maage_options_drag(ui, screen, 0U);
        else if (ui->dragging_fps && screen == MAAGE_CLIENT_UI_VIDEO_SETTINGS) maage_options_drag(ui, screen, 3U);
        else if (ui->dragging_volume && screen == MAAGE_CLIENT_UI_SOUND_SETTINGS) {
            unsigned int n;
            for (n = 0; n < 7U; ++n)
                if (maage_sound_buses[n] + 1U == (unsigned int) ui->dragging_volume) {
                    maage_options_drag(ui, screen, n);
                    break;
                }
        }
    }
    if (!input->primary_down) {
        /* Volume sliders suppress press audio and preview once on release. */
        if (ui->dragging_volume && screen == MAAGE_CLIENT_UI_SOUND_SETTINGS && ui->click) ui->click(ui->click_context);
        ui->dragging_fov = 0;
        ui->dragging_fps = 0;
        ui->dragging_volume = 0;
    }
    click = input->primary_pressed && hovered >= 0;
    keyboard = input->accept && maage_options_enabled(ui, screen, ui->selected);
    if (!click && !keyboard) return 0;
    chosen = click ? (unsigned int) hovered : ui->selected;
    if (!maage_options_enabled(ui, screen, chosen)) return 0;
    if (click && maage_options_slider_id(screen, chosen)) {
        if (screen == MAAGE_CLIENT_UI_OPTIONS) ui->dragging_fov = 1;
        else if (screen == MAAGE_CLIENT_UI_VIDEO_SETTINGS) ui->dragging_fps = 1;
        else
            ui->dragging_volume = (int) maage_sound_buses[chosen] + 1;
        maage_options_drag(ui, screen, chosen);
        /* FOV/FPS sliders use press audio. A completed volume press/release
         * may arrive in one native input snapshot. */
        if (screen != MAAGE_CLIENT_UI_SOUND_SETTINGS || !input->primary_down) {
            if (ui->click) ui->click(ui->click_context);
            if (screen == MAAGE_CLIENT_UI_SOUND_SETTINGS) ui->dragging_volume = 0;
        }
        return 0;
    }
    if (keyboard && maage_options_slider_id(screen, chosen)) {
        if (!adjusted && maage_options_adjust(ui, screen, chosen, 1) && ui->click) ui->click(ui->click_context);
        return 0;
    }
    if (ui->click) ui->click(ui->click_context);
    maage_options_action(ui, screen, chosen);
    return 0;
}
static void maage_options_video_label(const maage_client_ui *ui, unsigned int id, char text[96]) {
    static const char *const scales[5] = {"Auto", "Small", "Normal", "Large", "4x"};
    static const char *const blends[5] = {"OFF", "Vanilla 3x3", "Smooth 5x5", "Smooth 7x7", "Smooth 9x9"};
    static const char *const leaves[3] = {"Fast", "Smart", "Fancy"};
    static const char *const modes[3] = {"Windowed", "Borderless", "Fullscreen"};
    const char *setting = id < 17U ? maage_video_unsupported[id] : "";
    if (id == 3U) {
        if (ui->fps_step == 26) strcpy(text, "Max Framerate: Unlimited");
        else
            sprintf(text, "Max Framerate: %d", (ui->fps_step + 1) * 10);
    } else if (id == 5U) strcpy(text, ui->view_bobbing ? "View Bobbing: ON" : "View Bobbing: OFF");
    else if (id == 6U) sprintf(text, "GUI Scale: %s", scales[ui->requested_scale]);
    else if (id == 12U)
        strcpy(text, !ui->vsync_supported ? "VSync: Unavailable" : (ui->vsync ? "VSync: ON" : "VSync: OFF"));
    else if (id == 14U) sprintf(text, "Biome Blend: %s", blends[ui->biome_blend]);
    else if (id == 15U) sprintf(text, "Leaves: %s", leaves[ui->leaves_mode]);
    else if (id == 11U) sprintf(text, "Window: %s", ui->mode_pending || ui->mode_changing ? "Switching..." : modes[ui->window_mode]);
    else if (id == 16U && ui->has_snapshot) strcpy(text, ui->snapshot.player.game_mode == MAAGE_GAME_MODE_CREATIVE ? "Game: Creative" : "Game: Survival");
    else if (id == MAAGE_OPTIONS_VIDEO_DONE) strcpy(text, "Done");
    else {
        strncpy(text, setting, 95);
        text[95] = 0;
    }
}
static void maage_options_draw(maage_client_ui *ui, void *context) {
    maage_client_ui_screen_id screen = maage_client_ui_current(ui);
    unsigned int n, count = maage_options_count(screen), bus;
    maage_client_ui_rect r;
    char label[96];
    float center = maage_options_center(ui), title_y = ui->content.y + (screen == MAAGE_CLIENT_UI_VIDEO_SETTINGS ? 5.0f : 15.0f);
    int highlight, enabled;
    const char *title = screen == MAAGE_CLIENT_UI_OPTIONS ? "Options" : (screen == MAAGE_CLIENT_UI_VIDEO_SETTINGS ? "Video Settings" : "Music & Sounds");
    (void) context;
    maage_client_ui_text_shadow(ui, title, center, title_y, MAAGE_CLIENT_UI_CENTER, 1, 1, 1);
    for (n = 0; n < count; ++n) {
        r = maage_options_rect(ui, screen, n);
        if (r.width <= 0.0f || r.height <= 0.0f) continue;
        enabled = maage_options_enabled(ui, screen, n);
        highlight = enabled && (ui->selected_keyboard ? ui->selected == n : maage_options_contains(r, ui->mouse_x, ui->mouse_y));
        if (screen == MAAGE_CLIENT_UI_OPTIONS) {
            if (n == 0U) {
                if (ui->fov_degrees == 70) strcpy(label, "FOV: Normal");
                else if (ui->fov_degrees == 110) strcpy(label, "FOV: Quake Pro");
                else
                    sprintf(label, "FOV: %d", ui->fov_degrees);
                maage_client_ui_slider(ui, r, label, (float) (ui->fov_degrees - 30) / 80.0f, highlight);
            } else
                maage_client_ui_button_state(ui, r, maage_main_labels[n],
                                             !enabled ? MAAGE_CLIENT_UI_BUTTON_DISABLED : (highlight ? MAAGE_CLIENT_UI_BUTTON_HOVER : MAAGE_CLIENT_UI_BUTTON_NORMAL));
        } else if (screen == MAAGE_CLIENT_UI_SOUND_SETTINGS) {
            if (n == MAAGE_OPTIONS_SOUND_DONE) {
                maage_client_ui_button(ui, r, "Done", highlight);
            } else {
                bus = maage_sound_buses[n];
                if (ui->audio_volume[bus] == 0) sprintf(label, "%s: OFF", maage_sound_names[n]);
                else
                    sprintf(label, "%s: %d%%", maage_sound_names[n], ui->audio_volume[bus]);
                maage_client_ui_slider(ui, r, label, (float) ui->audio_volume[bus] / 100.0f, highlight);
            }
        } else {
            maage_options_video_label(ui, n, label);
            if (n == 3U) maage_client_ui_slider(ui, r, label, (float) ui->fps_step / 26.0f, highlight);
            else
                maage_client_ui_button_state(ui, r, label,
                                             !enabled ? MAAGE_CLIENT_UI_BUTTON_DISABLED : (highlight ? MAAGE_CLIENT_UI_BUTTON_HOVER : MAAGE_CLIENT_UI_BUTTON_NORMAL));
        }
    }
}
maage_status maage_client_ui_options_register(maage_client_ui *ui) {
    maage_status result = maage_client_ui_register_screen(ui, MAAGE_CLIENT_UI_OPTIONS, maage_options_draw, maage_options_input, NULL);
    if (!maage_status_failed(result)) result = maage_client_ui_register_screen(ui, MAAGE_CLIENT_UI_SOUND_SETTINGS, maage_options_draw, maage_options_input, NULL);
    if (!maage_status_failed(result)) result = maage_client_ui_register_screen(ui, MAAGE_CLIENT_UI_VIDEO_SETTINGS, maage_options_draw, maage_options_input, NULL);
    return result;
}
