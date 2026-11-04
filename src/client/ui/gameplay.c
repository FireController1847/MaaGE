#include "internal.h"
#include "../resources.h"
#include "../item_models.h"
#include "maage/time.h"
#include "maage/containers.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void maage_client_ui_block_uv(const maage_client_ui *ui, const float source[4], float uv[4]) {
    uv[0] = source[0] * ui->resources.block_atlas_width / ui->resources.atlas_edge;
    uv[2] = source[2] * ui->resources.block_atlas_width / ui->resources.atlas_edge;
    uv[1] = (MAAGE_CLIENT_UI_BLOCK_ATLAS_Y + source[1] * ui->resources.block_atlas_height) / ui->resources.atlas_edge;
    uv[3] = (MAAGE_CLIENT_UI_BLOCK_ATLAS_Y + source[3] * ui->resources.block_atlas_height) / ui->resources.atlas_edge;
}

typedef struct maage_client_ui_icon_context {
    maage_client_ui *ui;
    float x, y;
} maage_client_ui_icon_context;
static maage_status maage_client_ui_icon_quad(void *context, const maage_render_ui_vertex source[4]) {
    maage_client_ui_icon_context *icon = (maage_client_ui_icon_context *) context;
    maage_render_ui_vertex vertices[4];
    float uv[4];
    unsigned int i;
    memcpy(vertices, source, sizeof(vertices));
    for (i = 0; i < 4; ++i) {
        vertices[i].position[0] += icon->x;
        vertices[i].position[1] += icon->y;
        uv[0] = uv[2] = vertices[i].uv[0];
        uv[1] = uv[3] = vertices[i].uv[1];
        maage_client_ui_block_uv(icon->ui, uv, uv);
        vertices[i].uv[0] = uv[0];
        vertices[i].uv[1] = uv[1];
    }
    if (!maage_client_ui_append_quads(icon->ui, vertices, 1)) return icon->ui->draw_result;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MESH, 0);
}
void maage_client_ui_stack(maage_client_ui *ui, const maage_item_stack *stack, float x, float y) {
    maage_client_ui_rect rectangle;
    const maage_item_definition *item;
    maage_client_ui_icon_context icon;
    maage_status result;
    float fraction;
    char count[16];
    if (!stack || !stack->count || !ui->items || !ui->blocks) return;
    item = maage_item_registry_get(ui->items, stack->item_id);
    if (!item) return;
    icon.ui = ui;
    icon.x = x;
    icon.y = y;
    result = maage_client_item_icon_cached(&ui->model_icons, ui->models, ui->items, ui->blocks, stack, ui->inventory_grass_color, ui->item_uv, ui->item_texture_count, maage_client_ui_icon_quad, &icon);
    if (maage_status_failed(result)) {
        ui->draw_result = result;
        return;
    }
    if (item->durability && stack->damage) {
        rectangle.x = x + 2;
        rectangle.y = y + 13;
        rectangle.width = 13;
        rectangle.height = 2;
        maage_client_ui_panel(ui, rectangle, 0, 0, 0, 1);
        fraction = 1.0f - (float) stack->damage / item->durability;
        rectangle.width = (float) (int) (13 * fraction + 0.5f);
        rectangle.height = 1;
        maage_client_ui_panel(ui, rectangle, 1 - fraction, fraction, 0, 1);
    }
    if (stack->count > 1) {
        sprintf(count, "%u", stack->count);
        maage_client_ui_text(ui, count, x + 18, y + 10, MAAGE_CLIENT_UI_RIGHT, 0.18f, 0.18f, 0.18f);
        maage_client_ui_text(ui, count, x + 17, y + 9, MAAGE_CLIENT_UI_RIGHT, 1, 1, 1);
    }
}

/* Cosmetic variation depends only on the displayed owner tick and icon. It
 * stays stable between snapshots and never consumes gameplay randomness. */
static unsigned int maage_client_ui_vital_jitter(unsigned long tick, unsigned int icon, unsigned int range) {
    unsigned long value = (tick ^ ((icon + 1UL) * 2654435761UL)) & 4294967295UL;
    value ^= value >> 16;
    value = (value * 1664525UL + 1013904223UL) & 4294967295UL;
    return (unsigned int) ((value >> 16) % range);
}

static void maage_client_ui_status(maage_client_ui *ui, float x, float y, unsigned int value, int hunger) {
    maage_client_ui_rect rectangle;
    unsigned int i, filled, previous, remaining;
    unsigned long age, tick = ui->snapshot.tick;
    int flashing = 0;
    float v, u;
    if (value > 20) value = 20;
    previous = ui->health_before_damage > 20 ? 20 : ui->health_before_damage;
    if (!hunger && ui->health_flashing && tick >= ui->health_flash_tick) {
        age = tick - ui->health_flash_tick;
        if (age <= 10) {
            remaining = 20U - (unsigned int) age;
            flashing = (remaining / 3U) % 2U != 0;
        }
    }
    rectangle.width = rectangle.height = 9;
    v = (float) (hunger ? 27 : 0) / ui->resources.atlas_edge;
    for (i = 0; i < 10; ++i) {
        rectangle.x = hunger ? x - (float) i * 8 : x + (float) i * 8;
        rectangle.y = y;
        if (hunger) {
            if (ui->snapshot.player.saturation <= 0 && tick % (value * 3UL + 1UL) == 0) rectangle.y += (int) maage_client_ui_vital_jitter(tick, i + 10, 3) - 1;
        } else if (value <= 4) rectangle.y += maage_client_ui_vital_jitter(tick, i, 2);
        u = (float) (flashing ? 25 : 16) / ui->resources.atlas_edge;
        maage_client_ui_rectangle(ui, rectangle, u, v, u + 9.0f / ui->resources.atlas_edge, v + 9.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
        if (flashing && previous > i * 2) {
            filled = previous - i * 2;
            u = (float) (filled >= 2 ? 70 : 79) / ui->resources.atlas_edge;
            maage_client_ui_rectangle(ui, rectangle, u, v, u + 9.0f / ui->resources.atlas_edge, v + 9.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
        }
        filled = value > i * 2 ? value - i * 2 : 0;
        if (!filled) continue;
        u = (float) (filled >= 2 ? 52 : 61) / ui->resources.atlas_edge;
        maage_client_ui_rectangle(ui, rectangle, u, v, u + 9.0f / ui->resources.atlas_edge, v + 9.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    }
}

static void maage_client_ui_air(maage_client_ui *ui, float x, float y) {
    maage_client_ui_rect rectangle;
    int filled, count, i;
    float u, edge = (float) ui->resources.atlas_edge;
    if (!ui->snapshot.player.submerged) return;
    filled = (int) ceil((ui->snapshot.player.air - 2) / 30.0);
    count = (int) ceil(ui->snapshot.player.air / 30.0);
    if (filled < 0) filled = 0;
    if (count > 10) count = 10;
    rectangle.width = rectangle.height = 9;
    rectangle.y = y;
    for (i = 0; i < count; ++i) {
        rectangle.x = x - i * 8;
        u = (float) (i < filled ? 16 : 25) / edge;
        maage_client_ui_rectangle(ui, rectangle, u, 18.0f / edge, u + 9.0f / edge, 27.0f / edge, 1, 1, 1, 1);
    }
}

void maage_client_ui_gameplay_draw(maage_client_ui *ui) {
    maage_client_ui_rect rectangle;
    const maage_item_stack *selected;
    const maage_item_definition *item;
    float x, y;
    unsigned int i;
    if (!ui->has_snapshot || !ui->items) return;
    x = (float) (int) (ui->content.x + (ui->content.width - 182) * 0.5f);
    y = ui->content.y + ui->content.height - 22;
    rectangle.x = x;
    rectangle.y = y;
    rectangle.width = 182;
    rectangle.height = 22;
    maage_client_ui_rectangle(ui, rectangle, 256.0f / ui->resources.atlas_edge, 0, 438.0f / ui->resources.atlas_edge, 22.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    rectangle.x = x - 1 + (float) ui->snapshot.player.inventory.selected * 20;
    rectangle.y = y - 1;
    rectangle.width = 24;
    rectangle.height = 22;
    maage_client_ui_rectangle(ui, rectangle, 256.0f / ui->resources.atlas_edge, 22.0f / ui->resources.atlas_edge, 280.0f / ui->resources.atlas_edge, 44.0f / ui->resources.atlas_edge, 1, 1, 1, 1);
    for (i = 0; i < MAAGE_HOTBAR_SLOTS; ++i)
        maage_client_ui_stack(ui, &ui->snapshot.player.inventory.slots[i], x + 3 + (float) i * 20, y + 3);
    if (ui->snapshot.player.game_mode == MAAGE_GAME_MODE_SURVIVAL) {
        unsigned long level;
        double progress;
        char label[16];
        float edge = (float) ui->resources.atlas_edge;
        maage_gameplay_experience_level(ui->snapshot.player.experience, &level, &progress);
        rectangle.x = x;
        rectangle.y = y - 7;
        rectangle.width = 182;
        rectangle.height = 5;
        maage_client_ui_rectangle(ui, rectangle, 0, 64.0f / edge, 182.0f / edge, 69.0f / edge, 1, 1, 1, 1);
        rectangle.width = (float) (unsigned int) (progress * 183.0);
        if (rectangle.width > 0) maage_client_ui_rectangle(ui, rectangle, 0, 69.0f / edge, rectangle.width / edge, 74.0f / edge, 1, 1, 1, 1);
        if (level) {
            sprintf(label, "%lu", level);
            maage_client_ui_text(ui, label, x + 91, y - 13, MAAGE_CLIENT_UI_CENTER, 0.5f, 1, 0.125f);
        }
        maage_client_ui_status(ui, x, y - 17, ui->snapshot.player.health, 0);
        maage_client_ui_status(ui, x + 173, y - 17, ui->snapshot.player.hunger, 1);
        maage_client_ui_air(ui, x + 173, y - 27);
    }
    selected = maage_inventory_selected(&ui->snapshot.player.inventory);
    item = selected && selected->count ? maage_item_registry_get(ui->items, selected->item_id) : NULL;
    if (item && !maage_client_ui_modal(ui) && ui->highlight_until > ui->notice_now) {
        size_t first = ui->batch.vertex_count, n;
        float alpha = (float) ((ui->highlight_until - ui->notice_now) * 2.0);
        float name_y = ui->content.y + ui->content.height - 59 + (ui->snapshot.player.game_mode == MAAGE_GAME_MODE_CREATIVE ? 14 : 0);
        if (alpha > 1.0f) alpha = 1.0f;
        maage_client_ui_text(ui, maage_client_item_name(ui->items, selected), x + 92, name_y + 1, MAAGE_CLIENT_UI_CENTER, 0.25f, 0.25f, 0.25f);
        maage_client_ui_text(ui, maage_client_item_name(ui->items, selected), x + 91, name_y, MAAGE_CLIENT_UI_CENTER, 1, 1, 1);
        for (n = first; n < ui->batch.vertex_count; ++n)
            ui->vertices[n].color[3] *= alpha;
    }
}

maage_status maage_client_ui_set_snapshot(maage_client_ui *ui, const maage_session_snapshot *snapshot) {
    const maage_crafting_container *container;
    const maage_container_definition *container_definition = NULL;
    size_t i;
    int respawned, opened;
    if (!ui || !snapshot) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SNAPSHOT, 0);
    if (snapshot->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SNAPSHOT, 0);
    container = &snapshot->player.crafting;
    if (snapshot->player.inventory.selected >= MAAGE_HOTBAR_SLOTS || container->view_count > MAAGE_CONTAINER_VIEW_SLOTS || container->progress_count > MAAGE_CONTAINER_VIEW_SLOTS || !maage_item_stack_valid(ui->items, &snapshot->player.inventory.cursor) || !maage_item_stack_valid(ui->items, &snapshot->player.food.stack) || !maage_item_stack_valid(ui->items, &container->output)) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SNAPSHOT, 0);
    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
        if (!maage_item_stack_valid(ui->items, &snapshot->player.inventory.slots[i])) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SNAPSHOT, 0);
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i)
        if (!maage_item_stack_valid(ui->items, &container->inputs[i])) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SNAPSHOT, 0);
    for (i = 0; i < container->view_count; ++i)
        if (!maage_item_stack_valid(ui->items, &container->view[i])) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SNAPSHOT, 0);
    if (container->kind) {
        container_definition = maage_container_get(maage_block_registry_containers(ui->blocks), container->kind);
        if (!container_definition || container->grid_width != container_definition->grid_width || (container->view_count && (container->view_first >= container_definition->slot_count || container->view_count > container_definition->slot_count - container->view_first)) || (container->progress_count && (container->view_first >= container_definition->progress_count || container->progress_count > container_definition->progress_count - container->view_first))) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SNAPSHOT, 0);
    }
    opened = snapshot->player.crafting.kind && (!ui->has_snapshot || ui->snapshot.player.crafting.identity != snapshot->player.crafting.identity || !ui->snapshot.player.crafting.kind);
    respawned = ui->has_snapshot && ui->snapshot.player.health == 0 && snapshot->player.health != 0;
    if (!ui->has_snapshot || snapshot->session_epoch != ui->snapshot.session_epoch || snapshot->world_epoch != ui->snapshot.world_epoch || snapshot->player.motion.life != ui->snapshot.player.motion.life || snapshot->player.game_mode != ui->snapshot.player.game_mode || snapshot->tick < ui->snapshot.tick) {
        ui->health_before_damage = snapshot->player.health;
        ui->health_flash_tick = 0;
        ui->health_flashing = 0;
    } else if (snapshot->player.damage_sequence != ui->snapshot.player.damage_sequence) {
        /* One occurrence starts one flash, even when snapshots repeat. Keep
         * the pre-hit value so lost full/half hearts can flash behind live HP. */
        ui->health_before_damage = ui->snapshot.player.health;
        ui->health_flash_tick = snapshot->player.damage_tick;
        ui->health_flashing = snapshot->player.damage_sequence != 0;
    } else if (ui->health_flashing && snapshot->tick >= ui->health_flash_tick && snapshot->tick - ui->health_flash_tick > 10) ui->health_flashing = 0;
    {
        const maage_item_stack *selected = maage_inventory_selected(&snapshot->player.inventory);
        const maage_item_definition *definition = selected && ui->items ? maage_item_registry_get(ui->items, selected->item_id) : NULL;
        if (!selected || !selected->count) ui->highlight_until = 0;
        else if (!ui->has_snapshot || selected->item_id != ui->highlighted_item.item_id || ((!definition || !definition->durability) && selected->variant != ui->highlighted_item.variant)) ui->highlight_until = ui->notice_now + 2.0;
        if (selected) ui->highlighted_item = *selected;
    }
    if (!ui->has_snapshot || ui->snapshot.player.crafting.identity != snapshot->player.crafting.identity || ui->snapshot.player.crafting.kind != snapshot->player.crafting.kind || ui->snapshot.player.game_mode != snapshot->player.game_mode) maage_client_ui_inventory_reset(ui);
    ui->snapshot = *snapshot;
    ui->has_snapshot = 1;
    if (opened) maage_client_ui_inventory_opened(ui);
    if (ui->container_command_pending && ui->container_command_sequence && snapshot->applied_command_sequence >= ui->container_command_sequence) {
        ui->container_command_sequence = 0;
        ui->container_command_pending = ui->container_queued != 0;
    }
    if (ui->container_close_requested && !ui->container_command_pending) {
        if (!snapshot->player.crafting.kind) ui->container_close_requested = 0;
        else {
            maage_session_command command;
            memset(&command, 0, sizeof(command));
            command.kind = MAAGE_CMD_CONTAINER_CLOSE;
            command.container_identity = snapshot->player.crafting.identity;
            ui->route_result = maage_client_ui_queue_command(ui, &command);
            if (maage_status_failed(ui->route_result)) return ui->route_result;
        }
    }
    if (snapshot->player.crafting.kind) {
        maage_client_ui_screen_id desired;
        desired = container_definition->personal ? MAAGE_CLIENT_UI_INVENTORY : MAAGE_CLIENT_UI_CRAFTING_TABLE;
        if (maage_client_ui_current(ui) != desired) {
            maage_status result;
            /* The Server opens containers only from gameplay; a late snapshot
             * may arrive just after Escape opened Pause. */
            if (maage_client_ui_current(ui) == MAAGE_CLIENT_UI_PAUSE) maage_client_ui_back(ui);
            result = maage_client_ui_open(ui, desired);
            if (maage_status_failed(result)) return result;
        }
    } else if (!ui->container_command_pending && (maage_client_ui_current(ui) == MAAGE_CLIENT_UI_INVENTORY || maage_client_ui_current(ui) == MAAGE_CLIENT_UI_CRAFTING_TABLE)) {
        maage_client_ui_back(ui);
    }
    if (respawned && maage_client_ui_current(ui) == MAAGE_CLIENT_UI_PAUSE) maage_client_ui_back(ui);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
}
static int maage_client_ui_container_command(const maage_session_command *command) {
    return command->kind == MAAGE_CMD_CONTAINER_OPEN || command->kind == MAAGE_CMD_CONTAINER_CLOSE || command->kind == MAAGE_CMD_CONTAINER_SLOT || command->kind == MAAGE_CMD_CONTAINER_PAGE || command->kind == MAAGE_CMD_CREATIVE_ACTION || (command->kind == MAAGE_CMD_DROP && command->inventory_slot == MAAGE_INVENTORY_SLOTS);
}
static maage_status maage_client_ui_command_reserve(maage_client_ui *ui) {
    maage_session_command *commands;
    size_t capacity, i;
    if (ui->command_count < ui->command_capacity) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_RESIZE, 0);
    capacity = ui->command_capacity ? ui->command_capacity * 2 : MAAGE_CLIENT_UI_COMMAND_INITIAL_CAPACITY;
    if (capacity <= ui->command_count || capacity > (size_t) - 1 / sizeof(*commands)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_RESIZE, 0);
    commands = (maage_session_command *) malloc(capacity * sizeof(*commands));
    if (!commands) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_RESIZE, 0);
    for (i = 0; i < ui->command_count; ++i)
        commands[i] = ui->commands[(ui->command_read + i) % ui->command_capacity];
    free(ui->commands);
    ui->commands = commands;
    ui->command_capacity = capacity;
    ui->command_read = 0;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_RESIZE, 0);
}
maage_status maage_client_ui_queue_command(maage_client_ui *ui, const maage_session_command *command) {
    maage_session_command next;
    maage_status result;
    if (!ui || !command) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SUBMIT, 0);
    result = maage_client_ui_command_reserve(ui);
    if (maage_status_failed(result)) return result;
    if (command->kind == MAAGE_CMD_CONTAINER_CLOSE) ui->container_close_requested = 1;
    next = *command;
    next.protocol_version = MAAGE_SESSION_PROTOCOL_VERSION;
    /* The controller assigns one global sequence across UI and gameplay input. */
    next.sequence = 0;
    ui->commands[(ui->command_read + ui->command_count) % ui->command_capacity] = next;
    ++ui->command_count;
    if (maage_client_ui_container_command(command)) {
        ++ui->container_queued;
        ui->container_command_pending = 1;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
}
void maage_client_ui_command_submitted(maage_client_ui *ui, const maage_session_command *command, unsigned long sequence) {
    if (!ui || !command) return;
    if (maage_client_ui_container_command(command)) {
        ui->container_command_sequence = sequence;
        ui->container_command_pending = 1;
    }
}
maage_status maage_client_ui_take_command(maage_client_ui *ui, maage_session_command *command) {
    if (!ui || !command) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_READ, 0);
    /* Keep all accepted clicks in FIFO order. Acknowledgement provides the
     * revision for the next Creative transaction, including rapid clicks and
     * tab changes. Generic output slots retain their observed result revision. */
    if (!ui->command_count || ui->container_command_sequence) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_READ, 0);
    *command = ui->commands[ui->command_read];
    if (maage_client_ui_container_command(command)) {
        if (command->kind == MAAGE_CMD_CREATIVE_ACTION && ui->has_snapshot && command->container_identity == ui->snapshot.player.crafting.identity) command->container_revision = ui->snapshot.player.crafting.revision;
        --ui->container_queued;
    }
    ui->command_read = (ui->command_read + 1) % ui->command_capacity;
    --ui->command_count;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_READ, 0);
}
void maage_client_ui_queue_game_mode(maage_client_ui *ui) {
    maage_session_command command;
    if (!ui || !ui->has_snapshot) return;
    memset(&command, 0, sizeof(command));
    command.kind = MAAGE_CMD_GAME_MODE;
    command.game_mode = ui->snapshot.player.game_mode == MAAGE_GAME_MODE_CREATIVE ? MAAGE_GAME_MODE_SURVIVAL : MAAGE_GAME_MODE_CREATIVE;
    ui->route_result = maage_client_ui_queue_command(ui, &command);
}
void maage_client_ui_queue_respawn(maage_client_ui *ui) {
    maage_session_command command;
    if (!ui) return;
    memset(&command, 0, sizeof(command));
    command.kind = MAAGE_CMD_RESPAWN;
    ui->route_result = maage_client_ui_queue_command(ui, &command);
}

maage_status maage_client_ui_retry_command(maage_client_ui *ui, const maage_session_command *command) {
    maage_status result;
    if (!ui || !command || command->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_GUI, MAAGE_OPERATION_SUBMIT, 0);
    result = maage_client_ui_command_reserve(ui);
    if (maage_status_failed(result)) return result;
    ui->command_read = (ui->command_read + ui->command_capacity - 1) % ui->command_capacity;
    ui->commands[ui->command_read] = *command;
    ++ui->command_count;
    if (maage_client_ui_container_command(command)) ++ui->container_queued;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
}
