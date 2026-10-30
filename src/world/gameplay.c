#include "maage/gameplay.h"
#include "maage/entities.h"
#include "maage/session_protocol.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Material classification uses numeric builtin definitions; unknown blocks
 * deliberately use DEFAULT. State metadata is resolved through the registry. */
static unsigned int maage_gameplay_material(maage_block_id id) {
    switch (id) {
    case MAAGE_BLOCK_STONE:
    case MAAGE_BLOCK_COBBLESTONE:
    case MAAGE_BLOCK_BEDROCK:
    case MAAGE_BLOCK_GOLD_ORE:
    case MAAGE_BLOCK_IRON_ORE:
    case MAAGE_BLOCK_COAL_ORE:
    case MAAGE_BLOCK_LAPIS_ORE:
    case MAAGE_BLOCK_SANDSTONE:
    case MAAGE_BLOCK_MOSSY_COBBLESTONE:
    case MAAGE_BLOCK_OBSIDIAN:
    case MAAGE_BLOCK_DIAMOND_ORE:
    case MAAGE_BLOCK_REDSTONE_ORE:
    case MAAGE_BLOCK_EMERALD_ORE:
        return MAAGE_MATERIAL_STONE;
    case MAAGE_BLOCK_GRASS:
        return MAAGE_MATERIAL_GRASS;
    case MAAGE_BLOCK_DIRT:
        return MAAGE_MATERIAL_DIRT;
    case MAAGE_BLOCK_CRAFTING_TABLE:
    case MAAGE_BLOCK_PLANKS:
    case MAAGE_BLOCK_LOG:
    case MAAGE_BLOCK_TORCH:
    case 47UL:
    case 53UL:
    case 54UL:
    case 85UL:
    case 107UL:
    case 134UL:
    case 135UL:
    case 136UL:
    case 162UL:
        return MAAGE_MATERIAL_WOOD;
    case MAAGE_BLOCK_SAND:
        return MAAGE_MATERIAL_SAND;
    case MAAGE_BLOCK_GRAVEL:
    case 60UL:
    case 82UL:
        return MAAGE_MATERIAL_GRAVEL;
    case MAAGE_BLOCK_SNOW:
    case 80UL:
        return MAAGE_MATERIAL_SNOW;
    case MAAGE_BLOCK_ICE:
    case 20UL:
    case 95UL:
    case 102UL:
    case 160UL:
    case 174UL:
        return MAAGE_MATERIAL_GLASS;
    case 41UL:
    case 42UL:
    case 57UL:
    case 101UL:
    case 133UL:
        return MAAGE_MATERIAL_METAL;
    case MAAGE_BLOCK_WATER:
        return MAAGE_MATERIAL_WATER;
    case MAAGE_BLOCK_LAVA:
        return MAAGE_MATERIAL_LAVA;
    case MAAGE_BLOCK_CACTUS:
    case 35UL:
    case 171UL:
        return MAAGE_MATERIAL_CLOTH;
    case 65UL:
        return MAAGE_MATERIAL_LADDER;
    case 145UL:
        return MAAGE_MATERIAL_ANVIL;
    case MAAGE_BLOCK_LEAVES:
    case MAAGE_BLOCK_TALL_GRASS:
    case MAAGE_BLOCK_DEAD_BUSH:
    case MAAGE_BLOCK_DANDELION:
    case MAAGE_BLOCK_POPPY:
    case MAAGE_BLOCK_BROWN_MUSHROOM:
    case MAAGE_BLOCK_RED_MUSHROOM:
        return MAAGE_MATERIAL_FOLIAGE;
    default:
        return MAAGE_MATERIAL_DEFAULT;
    }
}
static unsigned int maage_gameplay_state_material(const maage_world *world, maage_block_state_id id) {
    const maage_block_state_definition *state = maage_block_registry_state(maage_world_registry(world), id);
    if (!state) return MAAGE_MATERIAL_DEFAULT;
    if (state->fluid_kind == MAAGE_FLUID_WATER) return MAAGE_MATERIAL_WATER;
    if (state->fluid_kind == MAAGE_FLUID_LAVA) return MAAGE_MATERIAL_LAVA;
    return maage_gameplay_material(state->block_id);
}
maage_status maage_gameplay_build_sound(maage_gameplay *game, maage_sound_event *event, unsigned int kind, unsigned int material, const double position[3], double intensity, double pitch) {
    if (!game || !event || !position || kind < MAAGE_SOUND_FOOTSTEP ||
        kind > MAAGE_SOUND_MAX || material > MAAGE_MATERIAL_MAX ||
        !(intensity >= 0.0 && intensity <= 4.0) ||
        !(pitch > 0.0 && pitch <= 4.0))
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0);
    memset(event, 0, sizeof(*event));
    event->event_id = kind;
    event->material = material;
    event->tick = game->ticks;
    event->sequence = game->sound_sequence++;
    memcpy(event->position, position, sizeof(event->position));
    event->intensity = intensity;
    event->pitch = pitch;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
}
typedef struct maage_gameplay_sound_sink {
    maage_gameplay *game;
    maage_sound_event *events;
    size_t capacity, written, dropped;
    int enabled;
} maage_gameplay_sound_sink;
static void maage_gameplay_sound(maage_gameplay_sound_sink *sink, unsigned int kind, unsigned int material, double x, double y, double z, double intensity, double pitch) {
    double position[3];
    maage_sound_event discarded;
    if (!sink->enabled) return;
    position[0] = x;
    position[1] = y;
    position[2] = z;
    if (sink->written < sink->capacity) {
        if (maage_status_code(maage_gameplay_build_sound(sink->game, &sink->events[sink->written], kind, material, position, intensity, pitch)) == MAAGE_STATUS_SUCCESS) ++sink->written;
    } else {
        maage_gameplay_build_sound(sink->game, &discarded, kind, material, position, intensity, pitch);
        ++sink->dropped;
    }
}
/* Isolated sound variation never advances world-generation randomness. */
static double maage_gameplay_sound_random(maage_gameplay *game) {
    game->sound_random = (game->sound_random * 1664525UL + 1013904223UL) & 4294967295UL;
    return (double) (game->sound_random & 16777215UL) / 16777216.0;
}
static double maage_gameplay_sound_pitch(maage_gameplay *game, double spread) {
    double first = maage_gameplay_sound_random(game);
    return 1.0 + (first - maage_gameplay_sound_random(game)) * spread;
}
/* Sample the central supporting cell, including lower fence/gate/wall support. */
static const maage_block_state_definition *maage_gameplay_underfoot(const maage_world *world, const maage_player *player, int walking) {
    const maage_block_registry *registry = maage_world_registry(world);
    const maage_block_state_definition *state, *lower, *above;
    int x = (int) floor(player->position[0]);
    int y = (int) floor(player->position[1] - 0.2);
    int z = (int) floor(player->position[2]);
    if (!maage_world_chunk_loaded(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z))) return NULL;
    state = maage_block_registry_state(registry, maage_world_get(world, x, y, z));
    lower = maage_block_registry_state(registry, maage_world_get(world, x, y - 1, z));
    if (walking && lower && (lower->block_id == 85UL || lower->block_id == 107UL || lower->block_id == 113UL || lower->block_id == 139UL)) {
        state = lower;
        --y;
    }
    if (!state || state->block_id == MAAGE_BLOCK_AIR) return NULL;
    above = walking == 2 ? maage_block_registry_state(registry, maage_world_get(world, x, y + 1, z)) : NULL;
    return above && above->block_id == MAAGE_BLOCK_SNOW ? above : state;
}
static maage_status maage_gameplay_sound_result(maage_status outcome, const maage_gameplay_sound_sink *sink, size_t *written, maage_status_trace *trace) {
    if (written) *written = sink->written;
    if (sink->dropped) {
        maage_status overflow = maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
        MAAGE_STATUS_TRACE(trace, overflow, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK);
        if (!maage_status_failed(outcome)) return overflow;
    }
    return outcome;
}

static void maage_gameplay_begin_life(maage_gameplay *game) {
    if (++game->life == 0) ++game->life;
    game->walk_distance = game->damage_yaw = 0;
    game->damage_sequence = game->damage_tick = game->death_tick = 0;
}
maage_status maage_gameplay_init(maage_gameplay *game, const maage_item_registry *items) {
    const maage_item_definition *item;
    unsigned int i;
    if (!game) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
    memset(game, 0, sizeof(*game));
    maage_gameplay_begin_life(game);
    game->mode = MAAGE_GAME_MODE_CREATIVE;
    game->health = game->hunger = 20;
    game->saturation = 5;
    for (i = 0; i < MAAGE_HOTBAR_SLOTS; ++i) {
        item = maage_item_registry_at(items, i);
        if (!item) break;
        game->inventory.slots[i].item_id = item->id;
        game->inventory.slots[i].count = item->max_stack;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
}
maage_status maage_gameplay_set_mode(maage_gameplay *game, unsigned int mode) {
    if (!game || mode > MAAGE_GAME_MODE_CREATIVE) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
    game->mode = mode;
    game->breaking_active = 0;
    game->break_progress = 0;
    game->dig_ticks = game->attack_miss_delay = 0;
    game->attack_held = game->use_held = 0;
    game->fall_distance = 0;
    game->jump_window = 0;
    game->sprint_window = game->sprint_ticks = 0;
    game->forward_down = 0;
    game->footstep_distance = 0;
    game->audio_initialized = 0;
    if (mode == MAAGE_GAME_MODE_SURVIVAL) game->flying = 0;
    else if (!game->health) {
        game->health = 20;
        maage_gameplay_begin_life(game);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
}
maage_status maage_gameplay_respawn(maage_gameplay *game, maage_player *player, const maage_world *world) {
    if (!game || !player || !world) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
    maage_player_spawn(player, world);
    maage_gameplay_begin_life(game);
    game->health = game->hunger = 20;
    game->saturation = 5;
    game->exhaustion = game->fall_distance = 0;
    game->flying = 0;
    game->footstep_distance = 0;
    game->audio_initialized = 0;
    game->dig_ticks = game->attack_miss_delay = 0;
    game->attack_held = game->use_held = 0;
    game->breaking_active = game->has_target = 0;
    game->break_progress = 0;
    game->hunger_ticks = game->jump_window = 0;
    game->sprint_window = game->sprint_ticks = 0;
    game->forward_down = 0;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
}
maage_status maage_gameplay_destroy(maage_gameplay *game) {
    if (!game) return maage_status_make(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_DESTROY, 0);
    memset(game, 0, sizeof(*game));
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_gameplay_save_clear(maage_gameplay_save *save) {
    if (!save) return maage_status_make(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_DESTROY, 0);
    memset(save, 0, sizeof(*save));
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_gameplay_snapshot(const maage_gameplay *game, maage_gameplay_save *save) {
    if (!game || !save) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SNAPSHOT, 0);
    memset(save, 0, sizeof(*save));
    memcpy(save->crafting_inputs, game->crafting.inputs, sizeof(save->crafting_inputs));
    save->inventory = game->inventory;
    save->mode = game->mode;
    save->health = game->health;
    save->hunger = game->hunger;
    save->saturation = game->saturation;
    save->exhaustion = game->exhaustion;
    save->fall_distance = game->fall_distance;
    save->flying = game->flying;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SNAPSHOT, 0);
}
maage_status maage_gameplay_restore(maage_gameplay *game, const maage_item_registry *items, const maage_gameplay_save *save) {
    unsigned int i;
    if (!game || !maage_item_registry_is_finalized(items) || !save || save->mode > MAAGE_GAME_MODE_CREATIVE ||
        save->health > 20 || save->hunger > 20 || save->inventory.selected >= MAAGE_HOTBAR_SLOTS ||
        !(save->saturation >= 0 && save->saturation <= save->hunger) ||
        !(save->exhaustion >= 0 && save->exhaustion <= 40) ||
        !(save->fall_distance >= 0 && save->fall_distance < MAAGE_WORLD_LIMIT) ||
        (save->flying != 0 && save->flying != 1) ||
        (save->mode == MAAGE_GAME_MODE_SURVIVAL && save->flying)) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LOAD, 0);
    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
        if (!maage_item_stack_valid(items, &save->inventory.slots[i])) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LOAD, 0);
    if (!maage_item_stack_valid(items, &save->inventory.cursor)) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LOAD, 0);
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i)
        if (!maage_item_stack_valid(items, &save->crafting_inputs[i]))
            return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LOAD, 0);
    memset(game, 0, sizeof(*game));
    memcpy(game->crafting.inputs, save->crafting_inputs, sizeof(game->crafting.inputs));
    maage_gameplay_begin_life(game);
    for (i = 0; i < MAAGE_CRAFTING_CELLS; ++i)
        if (game->crafting.inputs[i].count) game->crafting.closing = 1;
    game->inventory = save->inventory;
    game->mode = save->mode;
    game->health = save->health;
    game->hunger = save->hunger;
    game->saturation = save->saturation;
    game->exhaustion = save->exhaustion;
    game->fall_distance = save->fall_distance;
    game->flying = save->flying;
    return maage_status_make(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_LOAD, 0);
}
maage_status maage_gameplay_target(maage_gameplay *game, const maage_player *player, const maage_world *world, double yaw, double pitch) {
    double origin[3], direction[3], range;
    maage_status result;
    if (!game || !player || !world) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_RAYCAST, 0);
    memcpy(origin, player->position, sizeof(origin));
    origin[1] += MAAGE_PLAYER_EYE_HEIGHT;
    direction[0] = sin(yaw) * cos(pitch);
    direction[1] = -sin(pitch);
    direction[2] = -cos(yaw) * cos(pitch);
    range = game->reach > 0 && game->reach <= 64 ? game->reach : (game->mode == MAAGE_GAME_MODE_CREATIVE ? 5.0 : 4.5);
    result = maage_world_raycast(world, origin, direction, range, &game->target);
    game->has_target = maage_status_code(result) == MAAGE_STATUS_SUCCESS;
    if (game->breaking_active && (!game->has_target || game->breaking.x != game->target.x ||
                                  game->breaking.y != game->target.y || game->breaking.z != game->target.z ||
                                  game->breaking.state_id != game->target.state_id)) {
        game->breaking_active = 0;
        game->break_progress = 0;
        game->dig_ticks = 0;
    }
    return result;
}
static void maage_gameplay_swing(maage_gameplay *game, unsigned int kind) {
    if (game->swing <= 0.500001) {
        game->swing = 1;
        game->swing_tick = game->ticks;
        game->swing_kind = kind;
    }
}
static int maage_gameplay_same_target(const maage_world_target *a, const maage_world_target *b) {
    return a->x == b->x && a->y == b->y && a->z == b->z && a->state_id == b->state_id;
}
static maage_status maage_gameplay_break(maage_gameplay *game, maage_player *player, maage_world *world, const maage_item_registry *items, maage_gameplay_sound_sink *sink, int pressed) {
    const maage_block_definition *block;
    const maage_item_definition *tool;
    maage_item_stack *held;
    maage_item_drops drops;
    maage_entities *entities = maage_world_entities(world);
    unsigned long random;
    size_t i;
    double position[3], velocity[3];
    double progress;
    int harvest, starting;
    maage_status result;
    if (!game->has_target) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MUTATE, 0);
    if (pressed) game->break_delay = 0;
    if (game->break_delay) {
        --game->break_delay;
        return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MUTATE, 0);
    }
    if (maage_world_get(world, game->target.x, game->target.y, game->target.z) != game->target.state_id) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MUTATE, 0);
    block = maage_block_registry_block(maage_world_registry(world), game->target.block_id);
    if (!block) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MUTATE, 0);
    held = &game->inventory.slots[game->inventory.selected];
    tool = held->count ? maage_item_registry_get(items, held->item_id) : NULL;
    harvest = maage_item_can_harvest(tool, block);
    starting = !game->breaking_active || !maage_gameplay_same_target(&game->breaking, &game->target) ||
               game->breaking_item != (held->count ? held->item_id : 0) ||
               game->breaking_metadata != (held->count ? held->metadata : 0) ||
               game->breaking_damage != (held->count ? held->damage : 0);
    if (starting) {
        game->breaking = game->target;
        game->breaking_active = 1;
        game->break_progress = 0;
        game->dig_ticks = 0;
        game->breaking_item = held->count ? held->item_id : 0;
        game->breaking_metadata = held->count ? held->metadata : 0;
        game->breaking_damage = held->count ? held->damage : 0;
    }
    if (game->mode == MAAGE_GAME_MODE_CREATIVE || block->hardness == 0) progress = 1;
    else if (block->hardness < 0) progress = 0;
    else {
        const maage_block_state_definition *eye = maage_block_registry_state(maage_world_registry(world), maage_world_get(world, (int) floor(player->position[0]), (int) floor(player->position[1] + MAAGE_PLAYER_EYE_HEIGHT), (int) floor(player->position[2])));
        progress = maage_item_mining_damage(tool, block, player->grounded, eye && eye->fluid_kind == MAAGE_FLUID_WATER);
    }
    /* A changed held target initializes before the following damage updates
     * advance both crack progression and dig cadence. */
    if (starting && progress < 1 && !pressed) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MUTATE, 0);
    game->break_progress += progress;
    if (game->break_progress > 1) game->break_progress = 1;
    if (game->mode == MAAGE_GAME_MODE_SURVIVAL && !(starting && progress >= 1)) {
        if (game->dig_ticks == 0) maage_gameplay_sound(sink, MAAGE_SOUND_BLOCK_DIG, maage_gameplay_material(block->id), game->target.x + 0.5, game->target.y + 0.5, game->target.z + 0.5, 1.0, 1.0);
        game->dig_ticks = (game->dig_ticks + 1U) % 4U;
    }
    if (game->break_progress < 1.0 - 1e-12) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MUTATE, 0);
    memset(&drops, 0, sizeof(drops));
    random = maage_entities_random(entities);
    if (game->mode == MAAGE_GAME_MODE_SURVIVAL && harvest) {
        result = maage_item_generate_drops(maage_world_registry(world), items, game->target.state_id, tool, &random, &drops);
        if (maage_status_failed(result)) return result;
        result = maage_entities_reserve(entities, drops.count);
        if (maage_status_failed(result)) return result;
    }
    result = maage_world_set(world, game->target.x, game->target.y, game->target.z, game->target.state_id, MAAGE_STATE_AIR);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    maage_gameplay_sound(sink, MAAGE_SOUND_BLOCK_BREAK, maage_gameplay_material(block->id), game->target.x + 0.5, game->target.y + 0.5, game->target.z + 0.5, 1.0, 1.0);
    for (i = 0; i < drops.count; ++i) {
        position[0] = game->target.x + 0.5;
        position[1] = game->target.y + 0.375;
        position[2] = game->target.z + 0.5;
        random = (random * 1664525UL + 1013904223UL) & 4294967295UL;
        velocity[0] = ((double) (random & 65535UL) / 65536.0 - 0.5) * 0.2;
        random = (random * 1664525UL + 1013904223UL) & 4294967295UL;
        velocity[2] = ((double) (random & 65535UL) / 65536.0 - 0.5) * 0.2;
        velocity[1] = 0.2;
        result = maage_entities_spawn(entities, items, &drops.stacks[i], position, velocity, 10);
        if (maage_status_failed(result)) return result;
    }
    maage_entities_set_random(entities, random);
    if (game->mode == MAAGE_GAME_MODE_SURVIVAL) {
        game->exhaustion += 0.025;
        if (tool && tool->durability && (tool->tool_class == MAAGE_TOOL_SHEARS ? (block->id == MAAGE_BLOCK_LEAVES || block->id == MAAGE_BLOCK_TALL_GRASS || block->id == MAAGE_BLOCK_DEAD_BUSH) : block->hardness != 0)) {
            /* Maximum damage is still a usable final state. Check before
             * incrementing so even the full unsigned domain cannot wrap. */
            if (held->damage == tool->durability) {
                memset(held, 0, sizeof(*held));
                maage_gameplay_sound(sink, MAAGE_SOUND_ITEM_BREAK, MAAGE_MATERIAL_DEFAULT, player->position[0], player->position[1], player->position[2], 0.8, 0.8 + maage_gameplay_sound_random(game) * 0.4);
            } else ++held->damage;
        }
    }
    game->last_break = game->target;
    ++game->blocks_broken;
    game->breaking_active = 0;
    game->break_progress = 0;
    game->dig_ticks = 0;
    game->break_delay = game->mode == MAAGE_GAME_MODE_CREATIVE ? (game->attack_held ? 5U : 4U) : (starting && progress >= 1 ? 0U : 5U);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MUTATE, 0);
}
static int maage_gameplay_player_overlap(const maage_player *player, int x, int y, int z, const maage_block_shape *shape) {
    double minimum[3], maximum[3];
    int axis;
    minimum[0] = minimum[2] = -MAAGE_PLAYER_WIDTH * 0.5;
    minimum[1] = 0;
    maximum[0] = maximum[2] = MAAGE_PLAYER_WIDTH * 0.5;
    maximum[1] = MAAGE_PLAYER_HEIGHT;
    for (axis = 0; axis < 3; ++axis) {
        double coordinate = axis == 0 ? x : (axis == 1 ? y : z);
        if (player->position[axis] + maximum[axis] <= coordinate + shape->minimum[axis] || player->position[axis] + minimum[axis] >= coordinate + shape->maximum[axis]) return 0;
    }
    return 1;
}
maage_status maage_gameplay_placement(const maage_player *player, const maage_world *world, const maage_item_registry *items, const maage_item_stack *held, const maage_world_target *target, double yaw, maage_world_target *destination, maage_block_state_id *placed) {
    static const int dx[MAAGE_FACE_COUNT] = {-1, 1, 0, 0, 0, 0};
    static const int dy[MAAGE_FACE_COUNT] = {0, 0, -1, 1, 0, 0};
    static const int dz[MAAGE_FACE_COUNT] = {0, 0, 0, 0, -1, 1};
    const maage_item_definition *item;
    const maage_block_state_definition *state, *old_state;
    const maage_block_definition *block, *old_block;
    int x, y, z, valid;
    maage_status result;
    if (!player || !world || !items || !held || !target || !destination || !placed || (unsigned int) target->face >= MAAGE_FACE_COUNT) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_QUERY, 0);
    if (target->x <= -MAAGE_WORLD_LIMIT || target->x >= MAAGE_WORLD_LIMIT || target->z <= -MAAGE_WORLD_LIMIT || target->z >= MAAGE_WORLD_LIMIT || target->y < 0 || target->y >= MAAGE_WORLD_HEIGHT) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_QUERY, 0);
    if (maage_world_get(world, target->x, target->y, target->z) != target->state_id) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_QUERY, 0);
    item = held->count ? maage_item_registry_get(items, held->item_id) : NULL;
    if (!item || !item->has_block) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_QUERY, 0);
    result = maage_item_placement_state(item, held->metadata, target->face, yaw, placed);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    state = maage_block_registry_state(maage_world_registry(world), *placed);
    block = state ? maage_block_registry_block(maage_world_registry(world), state->block_id) : NULL;
    if (!block) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_QUERY, 0);
    x = target->x;
    y = target->y;
    z = target->z;
    old_block = maage_block_registry_block(maage_world_registry(world), target->block_id);
    if (!old_block || !old_block->replaceable) {
        x += dx[target->face];
        y += dy[target->face];
        z += dz[target->face];
    }
    if (y < 0 || y >= MAAGE_WORLD_HEIGHT || !maage_world_chunk_loaded(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z))) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_QUERY, 0);
    memset(destination, 0, sizeof(*destination));
    destination->x = x;
    destination->y = y;
    destination->z = z;
    destination->state_id = maage_world_get(world, x, y, z);
    old_state = maage_block_registry_state(maage_world_registry(world), destination->state_id);
    old_block = old_state ? maage_block_registry_block(maage_world_registry(world), old_state->block_id) : NULL;
    if (!old_block || !old_block->replaceable || (block->solid && maage_gameplay_player_overlap(player, x, y, z, &block->collision_shape))) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_QUERY, 0);
    destination->block_id = old_state->block_id;
    result = maage_world_can_survive(world, x, y, z, *placed, &valid);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    return maage_status_make(valid ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_QUERY, 0);
}
static maage_status maage_gameplay_place(maage_gameplay *game, const maage_player *player, maage_world *world, const maage_item_registry *items, double yaw, int sneaking, maage_gameplay_sound_sink *sink) {
    maage_item_stack *held;
    maage_world_target destination;
    maage_block_state_id placed;
    maage_status result;
    if (game->use_delay) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MUTATE, 0);
    /* Failed use also arms the held-input repeat timer. */
    game->use_delay = 4;
    if (!game->has_target) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MUTATE, 0);
    if (maage_world_get(world, game->target.x, game->target.y, game->target.z) != game->target.state_id) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MUTATE, 0);
    held = &game->inventory.slots[game->inventory.selected];
    if (game->target.block_id == MAAGE_BLOCK_CRAFTING_TABLE && (!sneaking || !held->count)) return maage_crafting_open(&game->crafting, MAAGE_CONTAINER_TABLE, game->target.x, game->target.y, game->target.z);
    result = maage_gameplay_placement(player, world, items, held, &game->target, yaw, &destination, &placed);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    result = maage_world_set(world, destination.x, destination.y, destination.z, destination.state_id, placed);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    game->last_place = destination;
    game->last_placed_state = placed;
    maage_gameplay_sound(sink, MAAGE_SOUND_BLOCK_PLACE, maage_gameplay_state_material(world, placed), destination.x + 0.5, destination.y + 0.5, destination.z + 0.5, 1.0, 1.0);
    if (game->mode == MAAGE_GAME_MODE_SURVIVAL && --held->count == 0) memset(held, 0, sizeof(*held));
    ++game->blocks_placed;
    maage_gameplay_swing(game, 2U);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MUTATE, 0);
}
static void maage_gameplay_hurt(maage_gameplay *game, const maage_player *player, unsigned int damage, maage_gameplay_sound_sink *sink) {
    if (!damage || !game->health) return;
    game->health = damage >= game->health ? 0 : game->health - damage;
    if (++game->damage_sequence == 0) ++game->damage_sequence;
    game->damage_tick = game->ticks;
    game->damage_yaw = 0; /* Current sources are fall impact and starvation, with no attacker. */
    if (!game->health) game->death_tick = game->ticks;
    maage_gameplay_sound(sink, game->health ? MAAGE_SOUND_PLAYER_HURT : MAAGE_SOUND_PLAYER_DEATH, MAAGE_MATERIAL_DEFAULT, player->position[0], player->position[1], player->position[2], 1.0, maage_gameplay_sound_pitch(game, 0.2));
}
static void maage_gameplay_vitals(maage_gameplay *game, const maage_player *player, const maage_world *world, const double previous[3], int was_grounded, int jumped, int sprint_jump, int in_water, maage_gameplay_sound_sink *sink) {
    double dx = player->position[0] - previous[0], dz = player->position[2] - previous[2];
    const maage_block_state_definition *surface;
    unsigned int damage;
    double distance;
    if (game->mode != MAAGE_GAME_MODE_SURVIVAL) {
        game->fall_distance = 0;
        return;
    }
    if (in_water) game->fall_distance = 0;
    else if (!was_grounded && player->position[1] < previous[1]) game->fall_distance += previous[1] - player->position[1];
    if (player->grounded) {
        if (!was_grounded && game->fall_distance > 3) {
            damage = (unsigned int) ceil(game->fall_distance - 3);
            /* Fall impact, actual damage, then the supporting block's
             * quieter/lower-pitched step family. */
            maage_gameplay_sound(sink, damage > 4 ? MAAGE_SOUND_PLAYER_FALL_BIG : MAAGE_SOUND_PLAYER_FALL_SMALL, MAAGE_MATERIAL_DEFAULT, player->position[0], player->position[1], player->position[2], 1.0, 1.0);
            maage_gameplay_hurt(game, player, damage, sink);
            surface = maage_gameplay_underfoot(world, player, 0);
            if (surface) maage_gameplay_sound(sink, MAAGE_SOUND_PLAYER_LAND, maage_gameplay_material(surface->block_id), player->position[0], player->position[1], player->position[2], 1.0, 1.0);
        }
        game->fall_distance = 0;
    }
    distance = floor(sqrt(dx * dx + dz * dz) * 100.0 + 0.5) * 0.01;
    if (in_water) game->exhaustion += distance * 0.015;
    else if (player->grounded) game->exhaustion += distance * (player->sprinting ? 0.1 : 0.01);
    if (jumped && was_grounded) game->exhaustion += sprint_jump ? 0.8 : 0.2;
    while (game->exhaustion > 4) {
        game->exhaustion -= 4;
        if (game->saturation > 0) {
            game->saturation -= 1;
            if (game->saturation < 0) game->saturation = 0;
        } else if (game->hunger) --game->hunger;
    }
    if (game->saturation > game->hunger) game->saturation = game->hunger;
    if (game->hunger >= 18 && game->health && game->health < 20) {
        if (++game->hunger_ticks >= 80) {
            ++game->health;
            game->exhaustion += 3;
            game->hunger_ticks = 0;
        }
    } else if (!game->hunger && game->health > 1) {
        if (++game->hunger_ticks >= 80) {
            maage_gameplay_hurt(game, player, 1, sink);
            game->hunger_ticks = 0;
        }
    } else
        game->hunger_ticks = 0;
    /* Normalize after regeneration as well, keeping every persisted snapshot valid. */
    while (game->exhaustion > 4) {
        game->exhaustion -= 4;
        if (game->saturation > 0) {
            game->saturation -= 1;
            if (game->saturation < 0) game->saturation = 0;
        } else if (game->hunger) --game->hunger;
    }
}
/* Deferred owner actions keep their original cause while the successful
 * recovery leaves the block/drop untouched and permits the next fixed step. */
static maage_status maage_gameplay_action_result(maage_status result, const maage_world *world, maage_status_trace *trace) {
    if (!maage_status_failed(result)) return result;
    MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE);
    if (!maage_world_failed(world) && (maage_status_code(result) == MAAGE_STATUS_ALLOCATION_FAILED ||
                                       maage_status_code(result) == MAAGE_STATUS_BUFFER_OVERFLOW ||
                                       maage_status_code(result) == MAAGE_STATUS_NOT_FOUND ||
                                       (maage_status_code(result) == MAAGE_STATUS_INTERRUPTED && maage_status_detail(result) == MAAGE_DETAIL_STALE))) {
        maage_status_trace_recover(trace, maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MUTATE, 0), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, __FILE__, __LINE__);
        return maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
    }
    return result;
}
static void maage_gameplay_sprint(maage_gameplay *game, maage_player *player, const maage_gameplay_intent *intent) {
    int forward = intent->movement.forward * (intent->descend ? MAAGE_PLAYER_SNEAK_INPUT : 1.0) >= 0.8;
    int allowed = game->health && (game->mode == MAAGE_GAME_MODE_CREATIVE || game->hunger > 6);
    if (game->sprint_window) --game->sprint_window;
    if (game->sprint_ticks && !--game->sprint_ticks) player->sprinting = 0;
    if (!allowed || !forward || player->collided_horizontally) {
        player->sprinting = 0;
        game->sprint_ticks = 0;
    } else if (!player->sprinting) {
        if (intent->movement.sprint || (player->grounded && (!game->forward_down || intent->forward_pressed) && game->sprint_window)) {
            player->sprinting = 1;
            game->sprint_ticks = 600;
        } else if (player->grounded && (!game->forward_down || intent->forward_pressed)) game->sprint_window = 7;
    }
    /* Retain a short forward tap even if its release reached the mailbox
     * before this fixed step; only sustained forward input can start sprint. */
    if (!forward && intent->forward_pressed && player->grounded && allowed && !intent->descend && !player->collided_horizontally) game->sprint_window = 7;
    if (intent->descend || !allowed) game->sprint_window = 0;
    game->forward_down = forward;
}

maage_status maage_gameplay_tick(maage_gameplay *game, maage_player *player, maage_world *world, const maage_item_registry *items, const maage_gameplay_intent *intent, maage_sound_event *events, size_t capacity, size_t *written, maage_status_trace *trace) {
    maage_player_input movement;
    double previous[3], velocity[3], dx, dy, dz, travel, strength;
    const maage_block_state_definition *surface;
    int in_water;
    maage_gameplay_sound_sink sink;
    int grounded, sprint_jump, jumped;
    maage_status result, outcome = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_TICK, 0);
    if (written) *written = 0;
    if ((capacity && (!events || !written)) || (events && !written) ||
        !game || !player || !world || !maage_item_registry_is_finalized(items) || !intent ||
        game->inventory.selected >= MAAGE_HOTBAR_SLOTS)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
    memset(&sink, 0, sizeof(sink));
    sink.game = game;
    sink.events = events;
    sink.capacity = capacity;
    sink.enabled = written != NULL;
    ++game->ticks;
    if (game->swing > 0) {
        game->swing -= 1.0 / 6.0;
        if (game->swing < 0) game->swing = 0;
    }
    if (game->use_delay) --game->use_delay;
    if (game->attack_miss_delay) --game->attack_miss_delay;
    if (game->jump_window) --game->jump_window;
    if (!game->health && game->mode == MAAGE_GAME_MODE_SURVIVAL) {
        player->sprinting = 0;
        game->sprint_window = game->sprint_ticks = 0;
        game->forward_down = 0;
        game->breaking_active = 0;
        game->break_progress = 0;
        return maage_status_make(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
    }
    if (game->mode == MAAGE_GAME_MODE_CREATIVE && intent->jump_pressed) {
        if (game->jump_window) {
            game->flying = !game->flying;
            player->velocity[1] = 0;
            game->jump_window = 0;
        } else
            game->jump_window = 7;
    }
    movement = intent->movement;
    movement.flying = game->flying;
    movement.descend = intent->descend;
    maage_gameplay_sprint(game, player, intent);
    sprint_jump = player->sprinting;
    jumped = movement.jump && !movement.flying && maage_player_fluid(player, world) == MAAGE_FLUID_NONE;
    memcpy(previous, player->position, sizeof(previous));
    grounded = player->grounded;
    memcpy(velocity, player->velocity, sizeof(velocity));
    result = maage_player_tick(player, world, &movement);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK);
    if (game->flying && player->grounded) game->flying = 0;
    dx = player->position[0] - previous[0];
    dy = player->position[1] - previous[1];
    dz = player->position[2] - previous[2];
    travel = sqrt(dx * dx + dz * dz);
    if (!(grounded && movement.descend)) game->walk_distance += travel * 0.6;
    in_water = maage_player_fluid(player, world) == MAAGE_FLUID_WATER;
    if (in_water && !game->in_water && game->audio_initialized) {
        strength = sqrt((velocity[0] * velocity[0] + velocity[2] * velocity[2]) * 0.2 + velocity[1] * velocity[1]) * 0.2;
        if (strength > 1.0) strength = 1.0;
        maage_gameplay_sound(&sink, MAAGE_SOUND_PLAYER_SPLASH, MAAGE_MATERIAL_WATER, player->position[0], player->position[1], player->position[2], strength, maage_gameplay_sound_pitch(game, 0.4));
    }
    game->in_water = in_water;
    game->audio_initialized = 1;
    maage_gameplay_vitals(game, player, world, previous, grounded, jumped, sprint_jump, in_water, &sink);
    if (!game->health || (game->mode == MAAGE_GAME_MODE_SURVIVAL && game->hunger <= 6)) {
        player->sprinting = 0;
        game->sprint_ticks = 0;
    }
    /* Accumulate 0.6 * displacement and cross successive
     * integer thresholds. Keep the fractional phase across stops and jumps.
     * Shift is the current crouch intent; flying disables walking triggers. */
    if (!movement.flying && !(grounded && intent->descend)) {
        surface = maage_gameplay_underfoot(world, player, 1);
        if (!surface || surface->block_id != 65UL) dy = 0;
        game->footstep_distance += sqrt(dx * dx + dy * dy + dz * dz) * 0.6;
        if ((travel > 0.0 || dy != 0.0) && game->footstep_distance > 1.0 && surface) {
            game->footstep_distance -= floor(game->footstep_distance);
            if (in_water) {
                strength = sqrt((dx * dx + dz * dz) * 0.2 + (player->grounded ? 0.0 : (player->position[1] - previous[1]) * (player->position[1] - previous[1]))) * 0.35;
                if (strength > 1.0) strength = 1.0;
                maage_gameplay_sound(&sink, MAAGE_SOUND_PLAYER_SWIM, MAAGE_MATERIAL_WATER, player->position[0], player->position[1], player->position[2], strength, maage_gameplay_sound_pitch(game, 0.4));
            }
            surface = maage_gameplay_underfoot(world, player, 2);
            /* A nearby surface can still be sampled after takeoff. Ground
             * steps require current contact; climbing keeps its own cadence. */
            if (surface && surface->fluid_kind == MAAGE_FLUID_NONE && (player->grounded || surface->block_id == 65UL)) maage_gameplay_sound(&sink, MAAGE_SOUND_FOOTSTEP, maage_gameplay_material(surface->block_id), player->position[0], player->position[1], player->position[2], 1.0, 1.0);
        }
    }
    if (!game->health && game->mode == MAAGE_GAME_MODE_SURVIVAL) {
        game->breaking_active = 0;
        game->break_progress = 0;
        return maage_gameplay_sound_result(maage_status_make(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0), &sink, written, trace);
    }
    result = maage_gameplay_target(game, player, world, intent->movement.yaw, intent->pitch);
    if (maage_status_failed(result) && maage_status_code(result) != MAAGE_STATUS_NOT_FOUND) return maage_gameplay_sound_result(result, &sink, written, trace);
    if (intent->attack) {
        if (intent->attack_pressed) {
            game->attack_held = 0;
            game->attack_miss_delay = 0;
            game->breaking_active = 0;
            game->break_progress = 0;
            game->dig_ticks = 0;
        }
        /* A missed Survival press temporarily suppresses held attacks. */
        if (!game->attack_held && !game->has_target && game->mode == MAAGE_GAME_MODE_SURVIVAL) game->attack_miss_delay = 10;
        if (intent->attack_pressed || (game->has_target && !game->attack_miss_delay)) maage_gameplay_swing(game, 1U);
        if (!game->attack_miss_delay) {
            result = maage_gameplay_action_result(maage_gameplay_break(game, player, world, items, &sink, intent->attack_pressed), world, trace);
            if (maage_status_failed(result)) return maage_gameplay_sound_result(result, &sink, written, trace);
            if (maage_status_code(result) == MAAGE_STATUS_PARTIAL) outcome = result;
        }
    } else {
        game->breaking_active = 0;
        game->break_progress = 0;
        game->dig_ticks = game->attack_miss_delay = 0;
    }
    game->attack_held = intent->attack;
    if (intent->use) {
        if (intent->use_pressed || !game->use_held) game->use_delay = 0;
        result = maage_gameplay_action_result(maage_gameplay_place(game, player, world, items, intent->movement.yaw, intent->descend, &sink), world, trace);
        if (maage_status_failed(result)) return maage_gameplay_sound_result(result, &sink, written, trace);
        if (maage_status_code(result) == MAAGE_STATUS_PARTIAL) outcome = result;
    }
    game->use_held = intent->use;
    if (!game->has_target) {
        game->breaking_active = 0;
        game->break_progress = 0;
    }
    if (maage_world_failed(world)) outcome = MAAGE_STATUS_PROPAGATE(trace, maage_world_status(world), MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK);
    return maage_gameplay_sound_result(outcome, &sink, written, trace);
}

maage_status maage_gameplay_drop(maage_gameplay *game, const maage_player *player, maage_world *world, const maage_item_registry *items, double yaw, double pitch, unsigned int slot, int whole) {
    maage_item_stack *source, stack;
    maage_entities *entities = maage_world_entities(world);
    double position[3], velocity[3];
    maage_status result;
    if (!game || !player || !entities || slot > MAAGE_INVENTORY_SLOTS || (whole != 0 && whole != 1) || !(yaw >= -1e9 && yaw <= 1e9) || !(pitch >= -1.6 && pitch <= 1.6)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
    source = slot == MAAGE_INVENTORY_SLOTS ? &game->inventory.cursor : &game->inventory.slots[slot];
    if (!source->count || !game->health) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_MUTATE, 0);
    if (!maage_item_stack_valid(items, source)) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_MUTATE, 0);
    result = maage_entities_reserve(entities, 1);
    if (maage_status_failed(result)) return result;
    stack = *source;
    if (!whole) stack.count = 1;
    memcpy(position, player->position, sizeof(position));
    position[1] += MAAGE_PLAYER_EYE_HEIGHT - 0.3;
    velocity[0] = sin(yaw) * cos(pitch) * 0.3;
    velocity[2] = -cos(yaw) * cos(pitch) * 0.3;
    velocity[1] = -sin(pitch) * 0.3 + 0.1;
    result = maage_entities_spawn(entities, items, &stack, position, velocity, 40);
    if (maage_status_failed(result)) return result;
    source->count -= stack.count;
    if (!source->count) memset(source, 0, sizeof(*source));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_MUTATE, 0);
}
