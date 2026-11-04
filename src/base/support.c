#include "base_internal.h"
#include "maage/entities.h"
#include <string.h>

static maage_status support_result(unsigned char code) {
    return maage_status_make(code, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
}
static int support_full(const maage_block_definition *block, const maage_block_state_definition *state) {
    unsigned int axis;
    maage_block_shape shape;
    if (!block->opaque || !maage_block_collision_shape(block, state, &shape)) return 0;
    for (axis = 0; axis < 3; ++axis)
        if (shape.minimum[axis] != 0 || shape.maximum[axis] != 1) return 0;
    return 1;
}
static maage_status support_test(const void *context, const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, unsigned int face, const maage_block_state_definition *neighbor, int *valid, unsigned int policy) {
    const maage_base_ids *ids = (const maage_base_ids *) context;
    const maage_block_definition *block = maage_block_registry_block(maage_world_registry(world), neighbor->block_id);
    unsigned char light;
    maage_environment_state environment;
    maage_status result;
    *valid = 0;
    if (!block) return support_result(MAAGE_STATUS_INVARIANT_VIOLATION);
    if (policy == 0) *valid = !block->material_solid;
    else if (policy == 1) *valid = support_full(block, neighbor);
    else if (policy == 3) *valid = maage_base_state_tag(ids, neighbor, ids->tag_soil);
    else if (policy == 4) *valid = neighbor->block_id == ids->block_dirt || maage_base_state_tag(ids, neighbor, ids->tag_sand);
    else if (policy == 5) *valid = face == MAAGE_FACE_NEGATIVE_Y ? maage_base_state_tag(ids, neighbor, ids->tag_sand) || neighbor->block_id == state->block_id : !block->material_solid;
    else if (policy == 6) *valid = neighbor->block_id != ids->block_ice && (maage_base_state_tag(ids, neighbor, ids->tag_leaves) || (block->opaque && block->material_solid));
    else if (policy == 8) *valid = neighbor->block_id != ids->block_air && !neighbor->fluid_kind;
    else if (policy == 2 || policy == 7) {
        *valid = face != MAAGE_FACE_NEGATIVE_Y || (policy == 2 ? maage_base_state_tag(ids, neighbor, ids->tag_farmland) : block->opaque);
        /* The down-face predicate samples combined light once; the full
         * dependency mask also admits changes to its other light inputs. */
        if (face != MAAGE_FACE_NEGATIVE_Y || !*valid) return support_result(MAAGE_STATUS_SUCCESS);
        result = maage_world_sample_light(world, x, y, z, &light);
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
        maage_world_environment(world, &environment);
        *valid = policy == 2 ? (light >> 4) == 15 || (light & 15) >= 8 || (int) (light >> 4) - (int) maage_environment_sky_subtracted(&environment) >= 8 : (light >> 4) < 13 && (light & 15) < 13;
    } else return support_result(MAAGE_STATUS_INVARIANT_VIOLATION);
    return support_result(MAAGE_STATUS_SUCCESS);
}
#define SUPPORT_TEST(name, policy) \
    static maage_status name(const void *context, const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, unsigned int face, const maage_block_state_definition *neighbor, int *valid) { \
        return support_test(context, world, x, y, z, state, face, neighbor, valid, policy); \
    }
SUPPORT_TEST(support_farmland_test, 0)
SUPPORT_TEST(support_torch_test, 1)
SUPPORT_TEST(support_wheat_test, 2)
SUPPORT_TEST(support_plant_test, 3)
SUPPORT_TEST(support_dead_bush_test, 4)
SUPPORT_TEST(support_cactus_test, 5)
SUPPORT_TEST(support_snow_test, 6)
SUPPORT_TEST(support_mushroom_test, 7)
SUPPORT_TEST(support_falling_test, 8)
#undef SUPPORT_TEST
#define SUPPORT_RULE(name, mask, predicate) \
    maage_status name(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_block_support_rule *rule) { \
        (void) x; \
        (void) y; \
        (void) z; \
        rule->faces = mask; \
        rule->mode = MAAGE_SUPPORT_ALL; \
        rule->context = maage_base_state_references(maage_world_registry(world), state); \
        rule->test = predicate; \
        return support_result(MAAGE_STATUS_SUCCESS); \
    }
SUPPORT_RULE(maage_support_farmland, 1U << MAAGE_FACE_POSITIVE_Y, support_farmland_test)
SUPPORT_RULE(maage_support_wheat, 63U, support_wheat_test)
SUPPORT_RULE(maage_support_plant, 1U << MAAGE_FACE_NEGATIVE_Y, support_plant_test)
SUPPORT_RULE(maage_support_dead_bush, 1U << MAAGE_FACE_NEGATIVE_Y, support_dead_bush_test)
SUPPORT_RULE(maage_support_cactus, 63U & ~(1U << MAAGE_FACE_POSITIVE_Y), support_cactus_test)
SUPPORT_RULE(maage_support_snow, 1U << MAAGE_FACE_NEGATIVE_Y, support_snow_test)
SUPPORT_RULE(maage_support_mushroom, 63U, support_mushroom_test)
SUPPORT_RULE(maage_support_falling, 1U << MAAGE_FACE_NEGATIVE_Y, support_falling_test)
#undef SUPPORT_RULE
maage_status maage_support_torch(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, maage_block_support_rule *rule) {
    static const unsigned int faces[5] = {MAAGE_FACE_NEGATIVE_X, MAAGE_FACE_POSITIVE_X, MAAGE_FACE_NEGATIVE_Z, MAAGE_FACE_POSITIVE_Z, MAAGE_FACE_NEGATIVE_Y};
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), state);
    maage_property_value attachment = maage_base_state_value(maage_world_registry(world), state, ids->property_attachment);
    (void) x;
    (void) y;
    (void) z;
    if (attachment < 1 || attachment > 5) return support_result(MAAGE_STATUS_INVARIANT_VIOLATION);
    rule->faces = 1U << faces[attachment - 1];
    rule->mode = MAAGE_SUPPORT_ALL;
    rule->context = ids;
    rule->test = support_torch_test;
    return support_result(MAAGE_STATUS_SUCCESS);
}
maage_status maage_base_support_place(const maage_world *world, int x, int y, int z, const maage_block_state_definition *state, int *valid) {
    return maage_world_can_survive(world, x, y, z, state->id, valid);
}
maage_status maage_base_support_lost(maage_world *world, int x, int y, int z, const maage_block_state_definition *state) {
    const maage_base_ids *ids = maage_base_state_references(maage_world_registry(world), state);
    const maage_block_definition *block = maage_block_registry_block(maage_world_registry(world), state->block_id);
    maage_entities *entities = maage_world_entities(world);
    const maage_item_registry *items = maage_world_item_registry(world);
    maage_item_drops drops;
    maage_status result;
    unsigned long random;
    unsigned int i;
    double position[3], velocity[3] = {0, 0.2, 0};
    if (block->falls) return maage_entities_fall(world, x, y, z, state->id);
    if (maage_base_state_tag(ids, state, ids->tag_farmland)) return maage_world_set(world, x, y, z, state->id, ids->state_dirt);
    memset(&drops, 0, sizeof(drops));
    random = maage_entities_random(entities);
    /* Support loss does not harvest snowballs. All other support drops use
     * ordinary environmental loot, independent of the initiating game mode. */
    if (state->block_id != ids->block_snow) {
        result = maage_item_generate_drops(maage_world_registry(world), items, state->id, NULL, &random, &drops);
        if (maage_status_failed(result)) return result;
        result = maage_entities_reserve(entities, drops.count);
        if (maage_status_failed(result)) return result;
    }
    result = maage_world_set(world, x, y, z, state->id, ids->state_air);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    for (i = 0; i < drops.count; ++i) {
        random = (random * 1664525UL + 1013904223UL) & 4294967295UL;
        position[0] = x + 0.15 + (random & 65535UL) / 65535.0 * 0.7;
        random = (random * 1664525UL + 1013904223UL) & 4294967295UL;
        position[1] = y + 0.15 + (random & 65535UL) / 65535.0 * 0.7;
        random = (random * 1664525UL + 1013904223UL) & 4294967295UL;
        position[2] = z + 0.15 + (random & 65535UL) / 65535.0 * 0.7;
        result = maage_entities_spawn(entities, items, &drops.stacks[i], position, velocity, 10);
        if (maage_status_failed(result)) return result;
    }
    return maage_entities_set_random(entities, random);
}
