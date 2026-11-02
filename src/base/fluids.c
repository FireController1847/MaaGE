#include "base_internal.h"
#include <string.h>

/* The generic solver owns scheduling, routing and mutation. Base owns the
 * material reaction: shallow lava beside water hardens, while downward flow
 * into water forms stone and horizontal lava can displace water. */
static maage_status maage_base_lava_contact(const void *context, const maage_block_state_definition *source, const maage_block_state_definition *target, int flowing, int downward, unsigned long *replacement, unsigned int *delay, int *allow) {
    const maage_base_ids *ids = (const maage_base_ids *) context;
    if (!ids || !source || !target || !replacement || !delay || !allow) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
    *replacement = MAAGE_CONTENT_ID_INVALID;
    *delay = 0;
    *allow = 0;
    if (target->fluid_kind != ids->fluid_water) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    if (flowing) {
        if (downward) *replacement = ids->state_stone;
        else *allow = 1;
    } else if (source->fluid_level <= 4) {
        *replacement = source->fluid_level ? ids->state_cobblestone : ids->state_obsidian;
        *delay = 1;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}

maage_status maage_base_fluids_register(maage_content_context *context, maage_base_ids *ids) {
    maage_fluid_definition water, lava;
    maage_status result;
    if (!context || !ids) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_REGISTER, 0);
    memset(&water, 0, sizeof(water));
    water.flow_levels = 8;
    water.tick_delay = 5;
    water.level_step = water.falling_spread_level = 1;
    water.source_neighbors = 2;
    water.routing_depth = 4;
    water.drops_displaced = 1;
    water.swimming = water.submerges = water.resets_fall = 1;
    water.acceleration = 0.02;
    water.drag = 0.8;
    water.gravity = 0.02;
    water.swim_impulse = 0.04;
    water.current_impulse = 0.014;
    water.wall_exit_impulse = 0.3;
    water.material = ids->material_water;
    water.fog_color[0] = water.fog_color[1] = 0.02f;
    water.fog_color[2] = 0.2f;
    water.fog_density = 0.1f;
    water.fog_light_scaled = 1;
    water.fov_scale = 60.0f / 70.0f;
    result = maage_fluid_register(context->fluids, "minecraft:water", &water);
    if (maage_status_failed(result)) return result;
    ids->fluid_water = water.id;

    memset(&lava, 0, sizeof(lava));
    lava.flow_levels = 8;
    lava.tick_delay = 30;
    lava.level_step = 2;
    lava.falling_spread_level = 1;
    lava.routing_depth = 4;
    lava.reverse_faces = 1;
    lava.material = ids->material_lava;
    lava.fov_scale = 1;
    lava.contact = maage_base_lava_contact;
    lava.context = ids;
    result = maage_fluid_register(context->fluids, "minecraft:lava", &lava);
    if (maage_status_failed(result)) return result;
    ids->fluid_lava = lava.id;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}

maage_status maage_base_fluids_resolve(maage_content_context *context, const maage_base_ids *ids) {
    const maage_fluid_definition *registered;
    maage_fluid_definition fluid;
    maage_status result;
    if (!context || !ids) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    registered = maage_fluid_get(context->fluids, ids->fluid_water);
    if (!registered) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    fluid = *registered;
    fluid.states = ids->water;
    fluid.state_count = sizeof(ids->water) / sizeof(ids->water[0]);
    fluid.has_overlay_texture = 1;
    fluid.overlay_texture = ids->texture_water_overlay;
    result = maage_fluid_update(context->fluids, &fluid);
    if (maage_status_failed(result)) return result;
    registered = maage_fluid_get(context->fluids, ids->fluid_lava);
    if (!registered) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_REGISTRY, MAAGE_OPERATION_RESOLVE, 0);
    fluid = *registered;
    fluid.states = ids->lava;
    fluid.state_count = sizeof(ids->lava) / sizeof(ids->lava[0]);
    return maage_fluid_update(context->fluids, &fluid);
}
