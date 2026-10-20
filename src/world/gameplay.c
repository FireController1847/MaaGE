#include "maage/gameplay.h"
#include <math.h>
#include <string.h>

void maage_gameplay_init(maage_gameplay *game, const maage_item_registry *items)
{
    const maage_item_definition *item;
    unsigned int i;
    if (!game) return;
    memset(game, 0, sizeof(*game));
    game->mode = MAAGE_GAME_MODE_CREATIVE; game->health = game->hunger = 20;
    game->saturation = 5;
    for (i = 0; i < MAAGE_HOTBAR_SLOTS; ++i) {
        item = maage_item_registry_at(items, i);
        if (!item) break;
        game->inventory.slots[i].item_id = item->id;
        game->inventory.slots[i].count = item->max_stack;
    }
}
int maage_gameplay_set_mode(maage_gameplay *game, unsigned int mode)
{
    if (!game || mode > MAAGE_GAME_MODE_CREATIVE) return 0;
    game->mode = mode; game->breaking_active = 0; game->break_progress = 0;
    game->fall_distance = 0; game->jump_window = 0;
    if (mode == MAAGE_GAME_MODE_SURVIVAL) game->flying = 0;
    else if (!game->health) game->health = 20;
    return 1;
}
void maage_gameplay_respawn(maage_gameplay *game, maage_player *player,
    const maage_world *world)
{
    if (!game || !player || !world) return;
    maage_player_spawn(player, world);
    game->health = game->hunger = 20; game->saturation = 5;
    game->exhaustion = game->fall_distance = 0; game->flying = 0;
    game->breaking_active = game->has_target = 0; game->break_progress = 0;
    game->hunger_ticks = game->jump_window = 0;
}
void maage_gameplay_snapshot(const maage_gameplay *game, maage_gameplay_save *save)
{
    memset(save, 0, sizeof(*save));
    save->inventory = game->inventory; save->mode = game->mode;
    save->health = game->health; save->hunger = game->hunger;
    save->saturation = game->saturation; save->exhaustion = game->exhaustion;
    save->fall_distance = game->fall_distance; save->flying = game->flying;
    memcpy(save->pickups, game->pickups, sizeof(save->pickups));
}
int maage_gameplay_restore(maage_gameplay *game, const maage_item_registry *items,
    const maage_gameplay_save *save)
{
    unsigned int i, axis;
    if (!game || !maage_item_registry_is_finalized(items) || !save || save->mode > MAAGE_GAME_MODE_CREATIVE ||
        save->health > 20 || save->hunger > 20 || save->inventory.selected >= MAAGE_HOTBAR_SLOTS ||
        !(save->saturation >= 0 && save->saturation <= save->hunger) ||
        !(save->exhaustion >= 0 && save->exhaustion <= 4) ||
        !(save->fall_distance >= 0 && save->fall_distance < MAAGE_WORLD_LIMIT) ||
        (save->flying != 0 && save->flying != 1) ||
        (save->mode == MAAGE_GAME_MODE_SURVIVAL && save->flying)) return 0;
    for (i = 0; i < MAAGE_INVENTORY_SLOTS; ++i)
        if (!maage_item_stack_valid(items, &save->inventory.slots[i])) return 0;
    if (!maage_item_stack_valid(items, &save->inventory.cursor)) return 0;
    for (i = 0; i < MAAGE_GAMEPLAY_PICKUPS; ++i) {
        if (!maage_item_stack_valid(items, &save->pickups[i].stack)) return 0;
        for (axis = 0; axis < 3; ++axis)
            if (!(save->pickups[i].position[axis] > -MAAGE_WORLD_LIMIT &&
                save->pickups[i].position[axis] < MAAGE_WORLD_LIMIT)) return 0;
        if (save->pickups[i].stack.count && !(save->pickups[i].position[1] >= 0 &&
            save->pickups[i].position[1] <= MAAGE_WORLD_HEIGHT)) return 0;
    }
    memset(game, 0, sizeof(*game));
    game->inventory = save->inventory; game->mode = save->mode;
    game->health = save->health; game->hunger = save->hunger;
    game->saturation = save->saturation; game->exhaustion = save->exhaustion;
    game->fall_distance = save->fall_distance; game->flying = save->flying;
    memcpy(game->pickups, save->pickups, sizeof(game->pickups));
    return 1;
}
void maage_gameplay_target(maage_gameplay *game, const maage_player *player,
    const maage_world *world, double yaw, double pitch)
{
    double origin[3], direction[3], range;
    if (!game || !player) return;
    memcpy(origin, player->position, sizeof(origin)); origin[1] += MAAGE_PLAYER_EYE_HEIGHT;
    direction[0] = sin(yaw) * cos(pitch); direction[1] = -sin(pitch);
    direction[2] = -cos(yaw) * cos(pitch);
    range = game->reach > 0 && game->reach <= 64 ? game->reach :
        (game->mode == MAAGE_GAME_MODE_CREATIVE ? 5.0 : 4.5);
    game->has_target = maage_world_raycast(world, origin, direction, range, &game->target) == 1;
    if (game->breaking_active && (!game->has_target || game->breaking.x != game->target.x ||
        game->breaking.y != game->target.y || game->breaking.z != game->target.z ||
        game->breaking.state_id != game->target.state_id)) {
        game->breaking_active = 0; game->break_progress = 0;
    }
}
static int maage_gameplay_same_target(const maage_world_target *a, const maage_world_target *b)
{
    return a->x == b->x && a->y == b->y && a->z == b->z && a->state_id == b->state_id;
}
static void maage_gameplay_break(maage_gameplay *game, maage_player *player,
    maage_world *world, const maage_item_registry *items)
{
    const maage_block_definition *block;
    const maage_item_definition *tool;
    maage_item_stack *held, drop;
    maage_inventory inventory;
    unsigned int remaining = 0, pickup = MAAGE_GAMEPLAY_PICKUPS, i;
    double speed = 1;
    int harvest;
    if (!game->has_target || game->break_delay) return;
    block = maage_block_registry_block(maage_world_registry(world), game->target.block_id);
    if (!block || (game->mode == MAAGE_GAME_MODE_SURVIVAL && block->hardness < 0)) return;
    held = &game->inventory.slots[game->inventory.selected];
    tool = held->count ? maage_item_registry_get(items, held->item_id) : NULL;
    harvest = !block->required_tool_level || (tool && tool->tool_class == block->required_tool &&
        tool->tool_level >= block->required_tool_level);
    if (!game->breaking_active || !maage_gameplay_same_target(&game->breaking, &game->target)) {
        game->breaking = game->target; game->breaking_active = 1; game->break_progress = 0;
    }
    if (game->mode == MAAGE_GAME_MODE_CREATIVE || block->hardness == 0) game->break_progress = 1;
    else {
        if (tool && tool->tool_class == block->required_tool) speed = tool->mining_speed;
        if (!player->grounded) speed /= 5;
        if (maage_world_get(world, (int)floor(player->position[0]),
            (int)floor(player->position[1] + MAAGE_PLAYER_EYE_HEIGHT),
            (int)floor(player->position[2])) == MAAGE_STATE_WATER) speed /= 5;
        game->break_progress += speed / (block->hardness * (harvest ? 30 : 100));
    }
    if (game->break_progress < 1) return;
    memset(&drop, 0, sizeof(drop)); inventory = game->inventory;
    if (game->mode == MAAGE_GAME_MODE_SURVIVAL && harvest && block->drop_count) {
        drop.item_id = block->drop_item_id; drop.count = block->drop_count;
        if (!maage_item_stack_valid(items, &drop)) return;
        remaining = maage_inventory_add(&inventory, items, &drop);
        if (remaining) {
            for (i = 0; i < MAAGE_GAMEPLAY_PICKUPS; ++i)
                if (!game->pickups[i].stack.count) { pickup = i; break; }
            /* Bounded overflow storage is explicit backpressure, never item loss. */
            if (pickup == MAAGE_GAMEPLAY_PICKUPS) return;
        }
    }
    if (maage_world_set(world, game->target.x, game->target.y, game->target.z,
        game->target.state_id, MAAGE_STATE_AIR) != MAAGE_WORLD_MUTATION_OK) return;
    game->inventory = inventory;
    held = &game->inventory.slots[game->inventory.selected];
    if (remaining) {
        game->pickups[pickup].stack = drop; game->pickups[pickup].stack.count = remaining;
        game->pickups[pickup].position[0] = game->target.x + 0.5;
        game->pickups[pickup].position[1] = game->target.y + 0.5;
        game->pickups[pickup].position[2] = game->target.z + 0.5;
    }
    if (game->mode == MAAGE_GAME_MODE_SURVIVAL) {
        game->exhaustion += 0.025;
        if (tool && tool->durability && ++held->damage >= tool->durability)
            memset(held, 0, sizeof(*held));
    }
    ++game->blocks_broken; game->breaking_active = 0; game->break_progress = 0;
    game->break_delay = 5;
}
static int maage_gameplay_player_overlap(const maage_player *player,
    int x, int y, int z, const maage_block_shape *shape)
{
    double minimum[3], maximum[3];
    int axis;
    minimum[0] = minimum[2] = -MAAGE_PLAYER_WIDTH * 0.5; minimum[1] = 0;
    maximum[0] = maximum[2] = MAAGE_PLAYER_WIDTH * 0.5; maximum[1] = MAAGE_PLAYER_HEIGHT;
    for (axis = 0; axis < 3; ++axis) {
        double coordinate = axis == 0 ? x : (axis == 1 ? y : z);
        if (player->position[axis] + maximum[axis] <= coordinate + shape->minimum[axis] ||
            player->position[axis] + minimum[axis] >= coordinate + shape->maximum[axis]) return 0;
    }
    return 1;
}
static void maage_gameplay_place(maage_gameplay *game, const maage_player *player,
    maage_world *world, const maage_item_registry *items, double yaw)
{
    static const int dx[MAAGE_FACE_COUNT] = {-1,1,0,0,0,0};
    static const int dy[MAAGE_FACE_COUNT] = {0,0,-1,1,0,0};
    static const int dz[MAAGE_FACE_COUNT] = {0,0,0,0,-1,1};
    maage_item_stack *held;
    const maage_item_definition *item;
    const maage_block_state_definition *state, *old_state;
    const maage_block_definition *block, *old_block;
    maage_block_state_id placed, old;
    int x, y, z;
    if (!game->has_target || game->use_delay) return;
    if (maage_world_get(world, game->target.x, game->target.y, game->target.z) != game->target.state_id) return;
    held = &game->inventory.slots[game->inventory.selected];
    item = held->count ? maage_item_registry_get(items, held->item_id) : NULL;
    if (!item || !maage_item_placement_state(item, held->metadata, game->target.face, yaw, &placed)) return;
    state = maage_block_registry_state(maage_world_registry(world), placed);
    block = state ? maage_block_registry_block(maage_world_registry(world), state->block_id) : NULL;
    if (!block) return;
    x = game->target.x; y = game->target.y; z = game->target.z;
    old_block = maage_block_registry_block(maage_world_registry(world), game->target.block_id);
    if (!old_block || !old_block->replaceable) {
        x += dx[game->target.face]; y += dy[game->target.face]; z += dz[game->target.face];
    }
    if (y < 0 || y >= MAAGE_WORLD_HEIGHT || !maage_world_chunk_loaded(world,
        maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z))) return;
    old = maage_world_get(world, x, y, z);
    old_state = maage_block_registry_state(maage_world_registry(world), old);
    old_block = old_state ? maage_block_registry_block(maage_world_registry(world), old_state->block_id) : NULL;
    if (!old_block || !old_block->replaceable ||
        (block->solid && maage_gameplay_player_overlap(player,x,y,z,&block->collision_shape))) return;
    if (maage_world_set(world,x,y,z,old,placed) != MAAGE_WORLD_MUTATION_OK) return;
    if (game->mode == MAAGE_GAME_MODE_SURVIVAL && --held->count == 0) memset(held,0,sizeof(*held));
    ++game->blocks_placed; game->use_delay = 4; game->swing = 1;
}
static void maage_gameplay_vitals(maage_gameplay *game, const maage_player *player,
    const double previous[3], int was_grounded, int jumped)
{
    double dx = player->position[0] - previous[0], dz = player->position[2] - previous[2];
    unsigned int damage;
    if (game->mode != MAAGE_GAME_MODE_SURVIVAL) { game->fall_distance = 0; return; }
    if (!was_grounded && player->position[1] < previous[1])
        game->fall_distance += previous[1] - player->position[1];
    if (player->grounded) {
        if (!was_grounded && game->fall_distance > 3) {
            damage = (unsigned int)ceil(game->fall_distance - 3);
            game->health = damage >= game->health ? 0 : game->health - damage;
        }
        game->fall_distance = 0;
    }
    game->exhaustion += sqrt(dx*dx+dz*dz) * 0.01;
    if (jumped && was_grounded) game->exhaustion += 0.2;
    while (game->exhaustion > 4) {
        game->exhaustion -= 4;
        if (game->saturation > 0) {
            game->saturation -= 1; if (game->saturation < 0) game->saturation = 0;
        } else if (game->hunger) --game->hunger;
    }
    if (game->saturation > game->hunger) game->saturation = game->hunger;
    if (game->hunger >= 18 && game->health < 20) {
        if (++game->hunger_ticks >= 80) { ++game->health; game->exhaustion += 3; game->hunger_ticks = 0; }
    } else if (!game->hunger && game->health > 1) {
        if (++game->hunger_ticks >= 80) { --game->health; game->hunger_ticks = 0; }
    } else game->hunger_ticks = 0;
    /* Normalize after regeneration as well, keeping every persisted snapshot valid. */
    while (game->exhaustion > 4) {
        game->exhaustion -= 4;
        if (game->saturation > 0) { game->saturation -= 1; if (game->saturation < 0) game->saturation = 0; }
        else if (game->hunger) --game->hunger;
    }
}
void maage_gameplay_tick(maage_gameplay *game, maage_player *player,
    maage_world *world, const maage_item_registry *items,
    const maage_gameplay_intent *intent)
{
    maage_player_input movement;
    double previous[3], dx, dy, dz;
    unsigned int i, remaining;
    int grounded;
    if (!game || !player || !world || !maage_item_registry_is_finalized(items) || !intent ||
        game->inventory.selected >= MAAGE_HOTBAR_SLOTS) return;
    ++game->ticks;
    if (game->swing > 0) { game->swing -= 1.0/6.0; if (game->swing < 0) game->swing = 0; }
    if (game->break_delay) --game->break_delay;
    if (game->use_delay) --game->use_delay;
    if (game->jump_window) --game->jump_window;
    if (!game->health && game->mode == MAAGE_GAME_MODE_SURVIVAL) {
        game->breaking_active = 0; game->break_progress = 0; return;
    }
    if (game->mode == MAAGE_GAME_MODE_CREATIVE && intent->jump_pressed) {
        if (game->jump_window) { game->flying = !game->flying; player->velocity[1] = 0; game->jump_window = 0; }
        else game->jump_window = 7;
    }
    movement = intent->movement; movement.flying = game->flying; movement.descend = intent->descend;
    memcpy(previous, player->position, sizeof(previous)); grounded = player->grounded;
    maage_player_tick(player, world, &movement);
    if (game->flying && player->grounded) game->flying = 0;
    maage_gameplay_vitals(game,player,previous,grounded,movement.jump);
    if (!game->health && game->mode == MAAGE_GAME_MODE_SURVIVAL) {
        game->breaking_active = 0; game->break_progress = 0; return;
    }
    for (i = 0; i < MAAGE_GAMEPLAY_PICKUPS; ++i) {
        if (!game->pickups[i].stack.count) continue;
        dx = player->position[0] - game->pickups[i].position[0];
        dy = player->position[1] + 0.8 - game->pickups[i].position[1];
        dz = player->position[2] - game->pickups[i].position[2];
        if (dx*dx + dy*dy + dz*dz > 4) continue;
        remaining = maage_inventory_add(&game->inventory,items,&game->pickups[i].stack);
        game->pickups[i].stack.count = remaining;
        if (!remaining) memset(&game->pickups[i],0,sizeof(game->pickups[i]));
    }
    maage_gameplay_target(game,player,world,intent->movement.yaw,intent->pitch);
    if (intent->attack) {
        if (game->swing <= 0) game->swing = 1;
        maage_gameplay_break(game,player,world,items);
    } else { game->breaking_active = 0; game->break_progress = 0; }
    if (intent->use) {
        if (game->swing <= 0) game->swing = 1;
        maage_gameplay_place(game,player,world,items,intent->movement.yaw);
    }
    if (!game->has_target) { game->breaking_active = 0; game->break_progress = 0; }
}
