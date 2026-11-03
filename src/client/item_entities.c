#include "item_entities.h"
#include "models.h"
#include "player_model.h"
#include "maage/internal/registry_pages.h"
#include "maage/log.h"
#include "maage/time.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct maage_item_models {
    maage_render_mesh **variants;
    maage_status *results;
    unsigned int count;
} maage_item_models;
typedef struct maage_client_model_cache {
    maage_render_mesh **parts;
    size_t count;
    maage_status result;
    struct maage_client_model_cache *next;
} maage_client_model_cache;
typedef struct maage_client_model_variant {
    maage_registry_pages textures;
} maage_client_model_variant;
typedef struct maage_client_registered_models {
    maage_registry_pages variants;
    maage_client_model_cache *first;
} maage_client_registered_models;
typedef struct maage_client_entity_pose {
    double body_yaw, head_yaw, pitch, walk_amount, walk_start, walk_distance, stationary, last_head, airborne, charge_start, charge;
} maage_client_entity_pose;
struct maage_client_item_entities {
    maage_renderer *renderer;
    maage_render_texture *atlas;
    maage_client_resources resources; /* Borrowed arrays, captured by value across staged publication. */
    const maage_item_registry *items;
    const maage_block_registry *blocks;
    maage_registry_pages models;
    maage_item_models *ordered;
    size_t model_count;
    const maage_entity_type_registry *types;
    const maage_model_registry *registered;
    maage_client_registered_models *entity_models;
    size_t entity_model_count;
    maage_session_entities current;
    maage_client_entity_pose *poses, *next_poses;
    size_t pose_capacity, next_pose_capacity;
    double pose_time;
    maage_render_instance *draws;
    size_t capacity;
    double received;
    int diagnostics;
    unsigned long prepared_sequence, submitted_sequence;
    unsigned long submitted_entity, prepared_entity;
    double prepared_at, diagnostic_received_at;
    struct {
        maage_entity_pickup pickup;
        double began;
        unsigned long session, world, tick;
        int active;
    } pickups[32];
};
static maage_status item_entities_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_CLIENT, operation, 0);
}
maage_status maage_client_item_entities_create(maage_renderer *renderer, maage_render_texture *atlas, const maage_client_resources *resources, const maage_item_registry *items, const maage_block_registry *blocks, maage_client_item_entities **output, maage_status_trace *trace) {
    maage_client_item_entities *view;
    const maage_item_definition *item;
    void **slot;
    size_t i;
    maage_status result = item_entities_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    if (!renderer || !atlas || !resources || !items || !blocks || !output) return item_entities_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    *output = NULL;
    view = (maage_client_item_entities *) calloc(1, sizeof(*view));
    if (!view) return result;
    view->renderer = renderer;
    view->atlas = atlas;
    view->resources = *resources;
    view->items = items;
    view->types = maage_content_entity_types(resources->content);
    view->registered = maage_content_models(resources->content);
    view->entity_model_count = maage_model_registry_count(view->registered);
    if (view->entity_model_count > (size_t) - 1 / sizeof(*view->entity_models)) {
        result = item_entities_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_CREATE);
        goto failed;
    }
    view->entity_models = view->entity_model_count ? (maage_client_registered_models *) calloc(view->entity_model_count, sizeof(*view->entity_models)) : NULL;
    if (view->entity_model_count && !view->entity_models) goto failed;
    view->blocks = blocks;
    view->diagnostics = maage_log_enabled("maage.client.latency", MAAGE_LOG_VERBOSE) && getenv("MAAGE_WORLD_DIAGNOSTICS") && !strcmp(getenv("MAAGE_WORLD_DIAGNOSTICS"), "1");
    view->model_count = maage_item_registry_count(items);
    view->ordered = (maage_item_models *) calloc(view->model_count, sizeof(*view->ordered));
    if (!view->ordered) goto failed;
    for (i = 0; i < view->model_count; ++i) {
        item = maage_item_registry_at(items, i);
        if (item->max_metadata == (unsigned int) -1) goto failed;
        view->ordered[i].count = item->max_metadata + 1;
        view->ordered[i].variants = (maage_render_mesh **) calloc(view->ordered[i].count, sizeof(maage_render_mesh *));
        view->ordered[i].results = (maage_status *) calloc(view->ordered[i].count, sizeof(maage_status));
        slot = maage_registry_pages_ensure(&view->models, item->id);
        if (!view->ordered[i].variants || !view->ordered[i].results || !slot) goto failed;
        *slot = &view->ordered[i];
    }
    *output = view;
    return item_entities_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
failed:
    maage_client_item_entities_destroy(view);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE);
}
static maage_status item_entities_model(maage_client_item_entities *view, const maage_item_stack *stack, const maage_render_mesh **mesh, int *notice, maage_status_trace *trace) {
    void **slot = maage_registry_pages_find(&view->models, stack->item_id);
    maage_item_models *models = slot ? (maage_item_models *) *slot : NULL;
    maage_mesh_data data;
    maage_status result;
    *notice = 0;
    if (!models || stack->metadata >= models->count || !maage_item_stack_valid(view->items, stack)) return item_entities_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_LOOKUP);
    if (!models->variants[stack->metadata]) {
        double began = view->diagnostics ? maage_time_monotonic_seconds() : -1, upload_at;
        if (maage_status_failed(models->results[stack->metadata])) return models->results[stack->metadata];
        result = maage_client_item_model_create(&view->resources, view->items, view->blocks, stack, &data);
        upload_at = view->diagnostics ? maage_time_monotonic_seconds() : -1;
        if (!maage_status_failed(result)) result = maage_renderer_upload_geometry(view->renderer, &data, view->atlas, &models->variants[stack->metadata], trace);
        maage_client_item_model_destroy(&data);
        if (view->diagnostics) MAAGE_LOG_VERBOSE_EVENT("maage.client.latency", maage_log_verbose("maage.client.latency", "item=%lu metadata=%u model_ms=%.3f upload_api_ms=%.3f", (unsigned long) stack->item_id, stack->metadata, (upload_at - began) * 1000, (maage_time_monotonic_seconds() - upload_at) * 1000));
        models->results[stack->metadata] = result;
        if (maage_status_failed(result)) {
            *notice = 1;
            return result;
        }
    }
    *mesh = models->variants[stack->metadata];
    return item_entities_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_LOOKUP);
}
static maage_status item_entities_registered_model(maage_client_item_entities *view, maage_model_id model_id, maage_texture_id texture, unsigned long variant, maage_client_model_cache **output, int *notice, maage_status_trace *trace) {
    maage_client_registered_models *models;
    maage_client_model_cache *cache;
    const maage_model_definition *model;
    maage_client_model_variant *variant_models;
    maage_mesh_data data;
    maage_status result;
    void **slot;
    size_t part;
    *output = NULL;
    *notice = 0;
    if (!model_id || model_id > view->entity_model_count || !(model = maage_model_registry_get(view->registered, model_id))) return item_entities_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOOKUP);
    if (texture == MAAGE_CONTENT_ID_INVALID) texture = model->texture_id;
    models = &view->entity_models[model_id - 1];
    slot = maage_registry_pages_find(&models->variants, variant);
    if (!slot) slot = maage_registry_pages_ensure(&models->variants, variant);
    if (!slot) return item_entities_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    if (!*slot) {
        *slot = calloc(1, sizeof(*variant_models));
        if (!*slot) return item_entities_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    }
    variant_models = (maage_client_model_variant *) *slot;
    slot = maage_registry_pages_find(&variant_models->textures, texture);
    if (!slot) slot = maage_registry_pages_ensure(&variant_models->textures, texture);
    if (!slot) return item_entities_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    cache = (maage_client_model_cache *) *slot;
    if (!cache) {
        cache = (maage_client_model_cache *) calloc(1, sizeof(*cache));
        if (!cache) return item_entities_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
        cache->next = models->first;
        models->first = cache;
        *slot = cache;
        cache->count = model->part_count;
        if (cache->count > (size_t) - 1 / sizeof(*cache->parts)) return cache->result = item_entities_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_CREATE);
        cache->parts = (maage_render_mesh **) calloc(cache->count, sizeof(*cache->parts));
        if (!cache->parts) return cache->result = item_entities_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
        for (part = 0; part < cache->count; ++part) {
            result = maage_client_model_part_create(&view->resources, model, part, texture, variant, &data);
            if (!maage_status_failed(result) && data.index_count) result = maage_renderer_upload_geometry(view->renderer, &data, view->atlas, &cache->parts[part], trace);
            maage_client_item_model_destroy(&data);
            if (maage_status_failed(result)) {
                cache->result = result;
                *notice = 1;
                return result;
            }
        }
        cache->result = item_entities_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
    }
    *output = cache;
    return cache->result;
}
static maage_status item_entities_registered_draw(maage_client_item_entities *view, const maage_entity_view *entity, const maage_session_snapshot *snapshot, maage_world *world, const double position[3], double alpha, const maage_client_entity_pose *pose, size_t *count, maage_status_trace *trace) {
    const maage_entity_type_definition *type = maage_entity_type_get(view->types, entity->registered_type);
    const maage_model_definition *model;
    maage_client_model_cache *cache;
    maage_model_animation animation;
    maage_render_instance *draw;
    maage_status result;
    float transform[16], part_transform[16];
    double yaw, pitch, horizontal;
    size_t part;
    unsigned int axis;
    unsigned char light = snapshot->observation.light_available ? snapshot->observation.light : 64;
    int visible, notice;
    if (!type) return item_entities_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOOKUP);
    if (!type->model_id) return item_entities_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_PRESENT);
    if (!(model = maage_model_registry_get(view->registered, type->model_id))) return item_entities_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOOKUP);
    memset(&animation, 0, sizeof(animation));
    animation.age = entity->age + (snapshot->paused ? 0 : alpha);
    if (entity->type == MAAGE_ENTITY_LIVING) {
        animation.variant = entity->payload.living.variant;
        animation.flags = entity->payload.living.flags;
        animation.walk_phase = pose->walk_distance * 4;
        animation.walk_amount = pose->walk_amount;
        animation.yaw = -atan2(sin(pose->head_yaw - pose->body_yaw), cos(pose->head_yaw - pose->body_yaw));
        animation.pitch = pose->pitch;
        animation.attack = type->attack_cooldown && entity->payload.living.attack_ticks ? 1 - ((double) type->attack_cooldown - entity->payload.living.attack_ticks + alpha) / 6 : 0;
        if (animation.attack < 0) animation.attack = 0;
        animation.charge = pose->charge;
        animation.hurt = entity->payload.living.damage_sequence && entity->payload.living.hurt_animation_ticks ? entity->payload.living.hurt_animation_ticks - (snapshot->paused ? 0 : alpha) : 0;
        animation.death = !entity->payload.living.health ? entity->payload.living.death_ticks + (snapshot->paused ? 0 : alpha) - 1 : 0;
        animation.airborne = pose->airborne;
        yaw = -pose->body_yaw;
        pitch = 0;
    } else {
        horizontal = sqrt(entity->velocity[0] * entity->velocity[0] + entity->velocity[2] * entity->velocity[2]);
        yaw = atan2(-entity->velocity[0], -entity->velocity[2]);
        pitch = atan2(entity->velocity[1], horizontal);
    }
    result = item_entities_registered_model(view, type->model_id, type->texture_count ? type->textures[animation.variant % type->texture_count] : type->texture_id, animation.variant, &cache, &notice, trace);
    if (maage_status_failed(result)) {
        if (notice) {
            maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.entities", result, trace, "Registered model unavailable; affected entity presentation is omitted until resource reload.");
            maage_status_trace_recover(trace, item_entities_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_PRESENT), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__);
        }
        return result;
    }
    maage_player_matrix_identity(transform);
    maage_player_matrix_translate(transform, position[0], position[1], position[2]);
    maage_player_matrix_rotate(transform, 1, yaw);
    maage_player_matrix_rotate(transform, 0, pitch);
    if (entity->type == MAAGE_ENTITY_LIVING && !entity->payload.living.health) {
        double roll = animation.death > 0 ? sqrt(animation.death / 20 * 1.6) : 0;
        if (roll > 1) roll = 1;
        maage_player_matrix_rotate(transform, 2, roll * 1.5707963267948966);
    } else if (animation.hurt > 0) {
        double bearing = entity->payload.living.damage_yaw - pose->body_yaw;
        double phase = animation.hurt / 10;
        phase *= phase;
        maage_player_matrix_rotate(transform, 1, -bearing);
        maage_player_matrix_rotate(transform, 2, sin(phase * phase * 3.141592653589793) * -0.244346095279206);
        maage_player_matrix_rotate(transform, 1, bearing);
    }
    maage_player_matrix_scale(transform, model->scale, model->scale, model->scale);
    (void) maage_world_present_light(world, (int) floor(position[0]), (int) floor(position[1] + type->eye_height), (int) floor(position[2]), &light);
    for (part = 0; part < model->part_count; ++part) {
        if (!cache->parts[part]) continue;
        result = maage_model_part_matrix(model, &animation, part, part_transform, &visible);
        if (maage_status_failed(result)) return result;
        if (!visible) continue;
        draw = &view->draws[(*count)++];
        memset(draw, 0, sizeof(*draw));
        draw->mesh = cache->parts[part];
        if (animation.hurt > 0 || (entity->type == MAAGE_ENTITY_LIVING && !entity->payload.living.health)) {
            draw->overlay[0] = 1;
            draw->overlay[3] = 0.4f;
        }
        maage_player_matrix_multiply(draw->transform, transform, part_transform);
        draw->sky_light = (float) (light >> 4) / 15;
        draw->block_light = (float) (light & 15) / 15;
        for (axis = 0; axis < 16; ++axis)
            if (!(draw->transform[axis] >= -1e30 && draw->transform[axis] <= 1e30)) return item_entities_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_PRESENT);
    }
    return item_entities_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_PRESENT);
}
static int item_entities_block_ready(const maage_world *world, const maage_client_chunk_mesh *columns, size_t column_count, int x, int z) {
    size_t i;
    int column_x = maage_world_chunk_coordinate(x), column_z = maage_world_chunk_coordinate(z);
    if (!maage_world_mesh_can_present(world, column_x, column_z)) return 0;
    for (i = 0; i < column_count; ++i) {
        if (columns[i].used && columns[i].x == column_x && columns[i].z == column_z) {
            /* Empty column meshes also receive the validated first-frame flag. */
            return columns[i].presented || columns[i].first_frame;
        }
    }
    return 0;
}
static maage_status item_entities_block_draw(maage_client_item_entities *view, const maage_block_entity_view *entity, const maage_session_snapshot *snapshot, maage_world *world, double alpha, size_t *count, maage_status_trace *trace) {
    const maage_model_definition *model = maage_model_registry_get(view->registered, entity->model);
    maage_client_model_cache *cache;
    maage_model_animation animation = entity->animation;
    maage_render_instance *draw;
    maage_status result;
    float transform[16], part_transform[16];
    unsigned char light = snapshot->observation.light_available ? snapshot->observation.light : 64;
    size_t part;
    unsigned int axis;
    int visible, notice;
    result = item_entities_registered_model(view, entity->model, MAAGE_CONTENT_ID_INVALID, animation.variant, &cache, &notice, trace);
    if (maage_status_failed(result)) {
        if (notice) maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.entities", result, trace, "Registered block model unavailable until resource reload.");
        return result;
    }
    animation.charge = entity->previous_charge + (animation.charge - entity->previous_charge) * alpha;
    maage_player_matrix_identity(transform);
    maage_player_matrix_translate(transform, entity->translation[0], entity->translation[1], entity->translation[2]);
    for (axis = 0; axis < 3; ++axis)
        maage_player_matrix_rotate(transform, axis, entity->rotation[axis]);
    maage_player_matrix_scale(transform, model->scale, model->scale, model->scale);
    (void) maage_world_present_light(world, entity->x, entity->y, entity->z, &light);
    for (part = 0; part < model->part_count; ++part) {
        if (!cache->parts[part]) continue;
        result = maage_model_part_matrix(model, &animation, part, part_transform, &visible);
        if (maage_status_failed(result)) return result;
        if (!visible) continue;
        draw = &view->draws[(*count)++];
        memset(draw, 0, sizeof(*draw));
        draw->mesh = cache->parts[part];
        maage_player_matrix_multiply(draw->transform, transform, part_transform);
        draw->sky_light = (float) (light >> 4) / 15;
        draw->block_light = (float) (light & 15) / 15;
    }
    return item_entities_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_PRESENT);
}
maage_status maage_client_item_entities_mesh(maage_client_item_entities *view, const maage_item_stack *stack, const maage_render_mesh **mesh, maage_status_trace *trace) {
    int notice;
    if (!view || !stack || !mesh) return item_entities_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_LOOKUP);
    *mesh = NULL;
    return item_entities_model(view, stack, mesh, &notice, trace);
}
maage_status maage_client_item_entities_clear(maage_client_item_entities *view) {
    if (!view) return item_entities_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    maage_renderer_set_instances(view->renderer, NULL, 0);
    free(view->current.entities);
    free(view->current.blocks);
    memset(&view->current, 0, sizeof(view->current));
    view->pose_time = 0;
    view->prepared_sequence = view->submitted_sequence = 0;
    view->submitted_entity = view->prepared_entity = 0;
    memset(view->pickups, 0, sizeof(view->pickups));
    return item_entities_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
maage_status maage_client_item_entities_pickup(maage_client_item_entities *view, const maage_session_event *event, double now) {
    unsigned int i, n, written = 0;
    if (!view || !event || event->kind != MAAGE_EVENT_PICKUP || !event->pickup_count || event->pickup_count > 32 || !(now >= 0)) return item_entities_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE);
    for (n = 0; n < event->pickup_count; ++n)
        if (!event->pickups[n].entity || !event->pickups[n].stack.count || !maage_item_stack_valid(view->items, &event->pickups[n].stack))
            return item_entities_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_UPDATE);
    for (n = 0; n < event->pickup_count; ++n)
        for (i = 0; i < 32; ++i)
            if (!view->pickups[i].active || now - view->pickups[i].began >= 0.15) {
                view->pickups[i].pickup = event->pickups[n];
                view->pickups[i].began = now;
                view->pickups[i].active = 1;
                view->pickups[i].session = event->session_epoch;
                view->pickups[i].world = event->world_epoch;
                view->pickups[i].tick = event->tick;
                ++written;
                break;
            }
    return item_entities_status(written == event->pickup_count ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_UPDATE);
}

static int item_entities_block_valid(const maage_client_item_entities *view, const maage_block_entity_view *block) {
    unsigned int axis;
    if (!block->identity || block->identity > 4294967295UL || !maage_model_registry_get(view->registered, block->model) || !maage_block_registry_state(view->blocks, block->state) || block->y < 0 || block->y >= MAAGE_WORLD_HEIGHT || block->x <= -MAAGE_WORLD_LIMIT || block->x >= MAAGE_WORLD_LIMIT || block->z <= -MAAGE_WORLD_LIMIT || block->z >= MAAGE_WORLD_LIMIT || !(block->animation.charge >= 0 && block->animation.charge <= 1 && block->previous_charge >= 0 && block->previous_charge <= 1)) return 0;
    if (block->secondary_state != MAAGE_CONTENT_ID_INVALID && (!maage_block_registry_state(view->blocks, block->secondary_state) || block->secondary_y < 0 || block->secondary_y >= MAAGE_WORLD_HEIGHT || block->secondary_x <= -MAAGE_WORLD_LIMIT || block->secondary_x >= MAAGE_WORLD_LIMIT || block->secondary_z <= -MAAGE_WORLD_LIMIT || block->secondary_z >= MAAGE_WORLD_LIMIT)) return 0;
    for (axis = 0; axis < 3; ++axis)
        if (!(block->translation[axis] > -MAAGE_WORLD_LIMIT && block->translation[axis] < MAAGE_WORLD_LIMIT && block->rotation[axis] >= -1e9 && block->rotation[axis] <= 1e9)) return 0;
    return 1;
}
static double item_entities_angle(double value) {
    return atan2(sin(value), cos(value));
}
static double item_entities_turn(double from, double to, double rate, double elapsed) {
    double difference = item_entities_angle(to - from), limit = rate * elapsed;
    if (difference > limit) difference = limit;
    if (difference < -limit) difference = -limit;
    return item_entities_angle(from + difference);
}
maage_status maage_client_item_entities_receive(maage_client_item_entities *view, maage_local_session *session, const maage_session_snapshot *snapshot, double now) {
    maage_session_entities incoming;
    maage_client_entity_pose *scratch, *pose;
    const maage_entity_view *entity, *old;
    const maage_entity_type_definition *type;
    maage_status result;
    size_t i, previous, capacity;
    double elapsed, alpha, dx, dy, dz, speed, limit, desired, difference, blend;
    int reset;
    if (!view || !session || !snapshot || !(now >= 0)) return item_entities_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE);
    if (view->current.session_epoch != snapshot->session_epoch || view->current.world_epoch != snapshot->world_epoch) (void) maage_client_item_entities_clear(view);
    result = maage_local_session_take_entities(session, &incoming);
    if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
        if (incoming.protocol_version != MAAGE_SESSION_PROTOCOL_VERSION || incoming.session_epoch != snapshot->session_epoch || incoming.world_epoch != snapshot->world_epoch || incoming.sequence <= view->current.sequence || incoming.tick < view->current.tick) {
            free(incoming.entities);
            free(incoming.blocks);
        } else {
            for (i = 0; i < incoming.count; ++i)
                if (!maage_entity_view_valid(&incoming.entities[i], view->items, view->types) || (i && incoming.entities[i - 1].id >= incoming.entities[i].id)) {
                    free(incoming.entities);
                    free(incoming.blocks);
                    return item_entities_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_UPDATE);
                }
            for (i = 0; i < incoming.block_count; ++i) {
                const maage_block_entity_view *block = &incoming.blocks[i];
                if (!item_entities_block_valid(view, block)) {
                    free(incoming.entities);
                    free(incoming.blocks);
                    return item_entities_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_UPDATE);
                }
            }
            if (incoming.count > view->next_pose_capacity) {
                capacity = incoming.count + incoming.count / 2;
                if (capacity < incoming.count || capacity > (size_t) - 1 / sizeof(*scratch)) {
                    free(incoming.entities);
                    free(incoming.blocks);
                    return item_entities_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_UPDATE);
                }
                scratch = (maage_client_entity_pose *) realloc(view->next_poses, capacity * sizeof(*scratch));
                if (!scratch) {
                    free(incoming.entities);
                    free(incoming.blocks);
                    return item_entities_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_UPDATE);
                }
                view->next_poses = scratch;
                view->next_pose_capacity = capacity;
            }
            previous = 0;
            for (i = 0; i < incoming.count; ++i) {
                entity = &incoming.entities[i];
                pose = &view->next_poses[i];
                while (previous < view->current.count && view->current.entities[previous].id < entity->id)
                    ++previous;
                old = previous < view->current.count ? &view->current.entities[previous] : NULL;
                reset = !old || old->id != entity->id || old->type != entity->type || old->registered_type != entity->registered_type || old->age > entity->age;
                if (!reset) {
                    dx = old->position[0] - entity->position[0];
                    dy = old->position[1] - entity->position[1];
                    dz = old->position[2] - entity->position[2];
                    reset = dx * dx + dy * dy + dz * dz > 16 || incoming.tick - view->current.tick > 20;
                }
                if (reset) {
                    memset(pose, 0, sizeof(*pose));
                    if (entity->type == MAAGE_ENTITY_LIVING) {
                        pose->body_yaw = entity->payload.living.yaw;
                        pose->head_yaw = pose->last_head = entity->payload.living.head_yaw;
                        pose->pitch = entity->payload.living.pitch;
                        pose->walk_start = pose->walk_distance = entity->payload.living.walk_distance;
                        type = maage_entity_type_get(view->types, entity->registered_type);
                        pose->charge_start = pose->charge = type->fuse_ticks ? (double) entity->payload.living.fuse_ticks / type->fuse_ticks : 0;
                    }
                    memcpy(incoming.entities[i].previous, entity->position, sizeof(entity->position));
                } else {
                    *pose = view->poses[previous];
                    if (old->age != entity->age) {
                        pose->walk_start = pose->walk_distance;
                        pose->charge_start = pose->charge;
                    }
                }
            }
            scratch = view->poses;
            capacity = view->pose_capacity;
            view->poses = view->next_poses;
            view->pose_capacity = view->next_pose_capacity;
            view->next_poses = scratch;
            view->next_pose_capacity = capacity;
            if (view->current.tick != incoming.tick || !view->current.sequence) view->received = now;
            free(view->current.entities);
            free(view->current.blocks);
            view->current = incoming;
            if (view->diagnostics) view->diagnostic_received_at = maage_time_monotonic_seconds();
            if (view->diagnostics) MAAGE_LOG_VERBOSE_EVENT("maage.client.latency", maage_log_verbose("maage.client.latency", "entity_sequence=%lu entity_tick=%lu count=%lu block_models=%lu first_entity=%lu last_entity=%lu received_at=%.6f entity_transport_ms=%.3f", incoming.sequence, incoming.tick, (unsigned long) incoming.count, (unsigned long) incoming.block_count, incoming.count ? incoming.entities[0].id : 0UL, incoming.count ? incoming.entities[incoming.count - 1].id : 0UL, view->diagnostic_received_at, incoming.offered_at >= 0 ? (view->diagnostic_received_at - incoming.offered_at) * 1000 : -1));
        }
    } else if (maage_status_failed(result) && maage_status_code(result) != MAAGE_STATUS_INVALID_STATE) return result;
    elapsed = view->pose_time > 0 && now > view->pose_time && !snapshot->paused ? now - view->pose_time : 0;
    view->pose_time = now;
    if (elapsed > 0.25) elapsed = 0.25;
    alpha = snapshot->paused ? 1 : (now - view->received) / MAAGE_PLAYER_TICK_SECONDS;
    if (alpha < 0) alpha = 0;
    if (alpha > 1) alpha = 1;
    blend = 1 - pow(0.6, elapsed / MAAGE_PLAYER_TICK_SECONDS);
    for (i = 0; i < view->current.count; ++i) {
        entity = &view->current.entities[i];
        if (entity->type != MAAGE_ENTITY_LIVING) continue;
        pose = &view->poses[i];
        speed = sqrt(entity->velocity[0] * entity->velocity[0] + entity->velocity[2] * entity->velocity[2]);
        if (!entity->payload.living.health) speed = 0;
        pose->walk_amount += ((speed * 4 > 1 ? 1 : speed * 4) - pose->walk_amount) * blend;
        pose->airborne += ((!entity->grounded && entity->payload.living.health ? 1 : 0) - pose->airborne) * blend;
        pose->walk_distance = pose->walk_start + (entity->payload.living.walk_distance - pose->walk_start) * alpha;
        type = maage_entity_type_get(view->types, entity->registered_type);
        pose->charge = pose->charge_start + ((type->fuse_ticks ? (double) entity->payload.living.fuse_ticks / type->fuse_ticks : 0) - pose->charge_start) * alpha;
        if (!entity->payload.living.health) continue;
        desired = entity->payload.living.head_yaw;
        if (fabs(item_entities_angle(desired - pose->last_head)) > 0.2617993877991494 || speed > 0.01) pose->stationary = 0;
        else pose->stationary += elapsed * 20;
        pose->last_head = desired;
        pose->head_yaw = item_entities_turn(pose->head_yaw, desired, 10.47197551196598, elapsed);
        pose->pitch = item_entities_turn(pose->pitch, entity->payload.living.pitch, 13.96263401595464, elapsed);
        desired = entity->payload.living.yaw;
        limit = 1.308996938995747;
        if (speed <= 0.01) {
            if (pose->stationary > 10) limit *= pose->stationary >= 20 ? 0 : (20 - pose->stationary) / 10;
            difference = item_entities_angle(pose->head_yaw - pose->body_yaw);
            desired = pose->head_yaw - (difference < -limit ? -limit : difference > limit ? limit
                                                                                          : difference);
        }
        pose->body_yaw = item_entities_turn(pose->body_yaw, desired, 10.47197551196598, elapsed);
        difference = item_entities_angle(pose->head_yaw - pose->body_yaw);
        if (difference > 1.308996938995747) pose->head_yaw = pose->body_yaw + 1.308996938995747;
        if (difference < -1.308996938995747) pose->head_yaw = pose->body_yaw - 1.308996938995747;
    }
    return item_entities_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
}
int maage_client_item_entities_target(const maage_client_item_entities *view, const maage_session_snapshot *snapshot, const double origin[3], const double direction[3], double limit, double now) {
    const maage_entity_view *entity;
    const maage_entity_type_definition *type;
    double position[3], distance, alpha;
    size_t i;
    unsigned int axis, sample;
    if (!view || view->current.session_epoch != snapshot->session_epoch || view->current.world_epoch != snapshot->world_epoch) return 0;
    alpha = snapshot->paused ? 1 : (now - view->received) / MAAGE_PLAYER_TICK_SECONDS;
    if (alpha < 0) alpha = 0;
    if (alpha > 1) alpha = 1;
    for (i = 0; i < view->current.count; ++i) {
        entity = &view->current.entities[i];
        if (entity->type != MAAGE_ENTITY_LIVING || !entity->payload.living.health) continue;
        type = maage_entity_type_get(view->types, entity->registered_type);
        if (!type) continue;
        /* Both displayed and most recently authoritative positions guard the
         * short snapshot interpolation interval against mining through a mob. */
        for (sample = 0; sample < 2; ++sample) {
            for (axis = 0; axis < 3; ++axis)
                position[axis] = sample ? entity->position[axis] : entity->previous[axis] + (entity->position[axis] - entity->previous[axis]) * alpha;
            position[1] -= 0.1;
            if (maage_entity_ray_box(origin, direction, position, type->width + 0.2, type->height + 0.2, limit, &distance) && distance < limit) return 1;
        }
    }
    return 0;
}

maage_status maage_client_item_entities_update(maage_client_item_entities *view, maage_local_session *session, maage_world *visual_world, const maage_session_snapshot *snapshot, double interpolation, const double eye[3], double now, const maage_render_instance *additional, size_t additional_count, const maage_client_chunk_mesh *columns, size_t column_count, maage_status_trace *trace) {
    maage_render_instance *draws, *draw;
    const maage_entity_view *entity;
    const maage_block_entity_view *block_view;
    const maage_entity_type_definition *type;
    const maage_model_definition *model;
    const maage_item_definition *item;
    const maage_render_mesh *mesh;
    maage_status result, outcome = 0;
    size_t i, column, count = 0, needed, amount;
    unsigned int copy, copies, axis, animation;
    int collected;
    int notice;
    unsigned char light = 64;
    unsigned long random;
    double position[3], phase, rotation, scale, offset[3], dx, dy, dz, age, sine, cosine, alpha;
    if (!view || !session || !snapshot || !eye || (additional_count && !additional)) return item_entities_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE);
    if (additional_count > (size_t) - 1 / sizeof(*draws) - 32) return item_entities_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_UPDATE);
    needed = 32 + additional_count;
    for (i = 0; i < view->current.count; ++i) {
        entity = &view->current.entities[i];
        if (!maage_entity_view_valid(entity, view->items, view->types) || (i && view->current.entities[i - 1].id >= entity->id)) return item_entities_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_UPDATE);
        amount = 5;
        if (entity->type == MAAGE_ENTITY_LIVING || entity->type == MAAGE_ENTITY_PROJECTILE) {
            type = maage_entity_type_get(view->types, entity->registered_type);
            if (type && !type->model_id) continue;
            model = type ? maage_model_registry_get(view->registered, type->model_id) : NULL;
            if (!model) return item_entities_status(MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_LOOKUP);
            amount = model->part_count;
        }
        if (amount > (size_t) - 1 / sizeof(*draws) - needed) return item_entities_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_UPDATE);
        needed += amount;
    }
    for (i = 0; i < view->current.block_count; ++i) {
        model = maage_model_registry_get(view->registered, view->current.blocks[i].model);
        if (!model || model->part_count > (size_t) - 1 / sizeof(*draws) - needed) return item_entities_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_UPDATE);
        needed += model->part_count;
    }
    if (needed > view->capacity) {
        draws = (maage_render_instance *) realloc(view->draws, needed * sizeof(*draws));
        if (!draws) return item_entities_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_UPDATE);
        view->draws = draws;
        view->capacity = needed;
    }
    view->prepared_entity = view->submitted_entity;
    alpha = (now - view->received) / 0.05;
    if (snapshot->paused) alpha = 1;
    if (alpha < 0) alpha = 0;
    if (alpha > 1) alpha = 1;
    for (i = 0; i < view->current.count; ++i) {
        entity = &view->current.entities[i];
        collected = 0;
        for (animation = 0; animation < 32; ++animation)
            if (view->pickups[animation].active && view->pickups[animation].pickup.entity == entity->id && view->current.tick < view->pickups[animation].tick) collected = 1;
        if (collected) continue;
        if (!maage_entity_view_valid(entity, view->items, view->types) || (i && view->current.entities[i - 1].id >= entity->id)) return item_entities_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_UPDATE);
        for (axis = 0; axis < 3; ++axis)
            position[axis] = entity->previous[axis] + (entity->position[axis] - entity->previous[axis]) * alpha;
        dx = position[0] - eye[0];
        dy = position[1] - eye[1];
        dz = position[2] - eye[2];
        if (dx * dx + dy * dy + dz * dz > 4096) continue;
        if (entity->type == MAAGE_ENTITY_LIVING || entity->type == MAAGE_ENTITY_PROJECTILE) {
            result = item_entities_registered_draw(view, entity, snapshot, visual_world, position, alpha, &view->poses[i], &count, trace);
            if (maage_status_failed(result)) {
                if (maage_status_code(result) != MAAGE_STATUS_ALLOCATION_FAILED && maage_status_code(result) != MAAGE_STATUS_BUFFER_OVERFLOW) return result;
                outcome = item_entities_status(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_UPDATE);
            }
            continue;
        }
        if (entity->type == MAAGE_ENTITY_FALLING_BLOCK && entity->payload.falling.source_revision) {
            for (column = 0; column < column_count; ++column)
                if (columns[column].used && columns[column].x == maage_world_chunk_coordinate(entity->payload.falling.source[0]) && columns[column].z == maage_world_chunk_coordinate(entity->payload.falling.source[2]) && columns[column].revision <= entity->payload.falling.source_revision) break;
            if (column < column_count) continue; /* Retain the old stationary presentation until removal geometry arrives. */
        }
        result = item_entities_model(view, (entity->type == MAAGE_ENTITY_ITEM ? &entity->payload.item : &entity->payload.falling.stack), &mesh, &notice, trace);
        if (maage_status_failed(result)) {
            if (maage_status_code(result) != MAAGE_STATUS_ALLOCATION_FAILED && maage_status_code(result) != MAAGE_STATUS_BUFFER_OVERFLOW) return result;
            if (notice) {
                MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPLOAD);
                maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.items", result, trace, "Item model unavailable; omitting affected instances until resource reload.");
                maage_status_trace_recover(trace, item_entities_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_PRESENT), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__);
                outcome = item_entities_status(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_UPDATE);
            }
            continue;
        }
        if (view->diagnostics && entity->id > view->submitted_entity) {
            if (entity->id > view->prepared_entity) view->prepared_entity = entity->id;
            MAAGE_LOG_VERBOSE_EVENT("maage.client.latency", maage_log_verbose("maage.client.latency", "prepared_entity=%lu entity_sequence=%lu prepared_at=%.6f", entity->id, view->current.sequence, maage_time_monotonic_seconds()));
        }
        if (entity->type == MAAGE_ENTITY_FALLING_BLOCK) {
            draw = &view->draws[count++];
            memset(draw, 0, sizeof(*draw));
            draw->mesh = mesh;
            draw->transform[0] = draw->transform[5] = draw->transform[10] = draw->transform[15] = 1;
            for (axis = 0; axis < 3; ++axis)
                draw->transform[12 + axis] = (float) (position[axis] + (axis == 1 ? 0.49 : 0));
            light = snapshot->observation.light_available ? snapshot->observation.light : 64;
            (void) maage_world_present_light(visual_world, (int) floor(position[0]), (int) floor(position[1] + 0.49), (int) floor(position[2]), &light);
            draw->sky_light = (float) (light >> 4) / 15;
            draw->block_light = (float) (light & 15) / 15;
            continue;
        }
        item = maage_item_registry_get(view->items, entity->payload.item.item_id);
        phase = (double) ((entity->id * 2654435761UL) & 65535UL) / 65536.0 * 6.283185307179586;
        age = entity->age + (snapshot->paused ? 0 : alpha);
        rotation = age / 20 + phase;
        sine = sin(rotation);
        cosine = cos(rotation);
        scale = item->model != MAAGE_ITEM_MODEL_SPRITE ? 0.25 : 0.5;
        position[1] += 0.1 + sin(age / 10 + phase) * 0.1 + (item->model != MAAGE_ITEM_MODEL_SPRITE ? 0.125 : 0.25);
        copies = entity->payload.item.count > 40 ? 5 : (entity->payload.item.count > 20 ? 4 : (entity->payload.item.count > 5 ? 3 : (entity->payload.item.count > 1 ? 2 : 1)));
        if (item->model == MAAGE_ITEM_MODEL_SPRITE)
            copies = entity->payload.item.count >= 32 ? 4 : (entity->payload.item.count >= 16 ? 3 : (entity->payload.item.count >= 2 ? 2 : 1));
        random = entity->id;
        for (copy = 0; copy < copies; ++copy) {
            for (axis = 0; axis < 3; ++axis) {
                random = (random * 1664525UL + 1013904223UL) & 4294967295UL;
                offset[axis] = copy ? ((double) (random & 255UL) / 255.0 - 0.5) * 0.15 : 0;
            }
            if (item->model == MAAGE_ITEM_MODEL_SPRITE) {
                offset[2] = ((double) copy - ((double) copies - 1) * 0.5) * 0.0421875;
                offset[0] = sine * offset[2];
                offset[2] *= cosine;
                offset[1] = 0;
            }
            draw = &view->draws[count++];
            memset(draw, 0, sizeof(*draw));
            draw->mesh = mesh;
            draw->transform[0] = (float) (cosine * scale);
            draw->transform[2] = (float) (-sine * scale);
            draw->transform[5] = (float) scale;
            draw->transform[8] = (float) (sine * scale);
            draw->transform[10] = (float) (cosine * scale);
            draw->transform[15] = 1;
            for (axis = 0; axis < 3; ++axis)
                draw->transform[12 + axis] = (float) (position[axis] + offset[axis]);
            light = snapshot->observation.light_available ? snapshot->observation.light : 64;
            (void) maage_world_present_light(visual_world, (int) floor(position[0]), (int) floor(position[1]), (int) floor(position[2]), &light);
            draw->sky_light = (float) (light >> 4) / 15;
            draw->block_light = (float) (light & 15) / 15;
        }
    }
    for (i = 0; i < view->current.block_count; ++i) {
        block_view = &view->current.blocks[i];
        if (!item_entities_block_ready(visual_world, columns, column_count, block_view->x, block_view->z) || maage_world_get(visual_world, block_view->x, block_view->y, block_view->z) != block_view->state) continue;
        if (block_view->secondary_state != MAAGE_CONTENT_ID_INVALID && (!item_entities_block_ready(visual_world, columns, column_count, block_view->secondary_x, block_view->secondary_z) || maage_world_get(visual_world, block_view->secondary_x, block_view->secondary_y, block_view->secondary_z) != block_view->secondary_state)) continue;
        result = item_entities_block_draw(view, block_view, snapshot, visual_world, alpha, &count, trace);
        if (maage_status_failed(result)) {
            if (maage_status_code(result) != MAAGE_STATUS_ALLOCATION_FAILED && maage_status_code(result) != MAAGE_STATUS_BUFFER_OVERFLOW) return result;
            maage_status_trace_recover(trace, item_entities_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_PRESENT), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__);
            outcome = item_entities_status(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_PRESENT);
        }
    }
    for (animation = 0; animation < 32; ++animation) {
        if (!view->pickups[animation].active) continue;
        phase = (now - view->pickups[animation].began) / 0.15;
        if (phase >= 1 || view->pickups[animation].session != snapshot->session_epoch || view->pickups[animation].world != snapshot->world_epoch) {
            view->pickups[animation].active = 0;
            continue;
        }
        if (phase < 0) phase = 0;
        phase *= phase;
        result = item_entities_model(view, &view->pickups[animation].pickup.stack, &mesh, &notice, trace);
        if (maage_status_failed(result)) {
            if (maage_status_code(result) != MAAGE_STATUS_ALLOCATION_FAILED && maage_status_code(result) != MAAGE_STATUS_BUFFER_OVERFLOW) return result;
            if (notice) {
                maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.items", result, trace, "Pickup model unavailable; omitting this decoration until resource reload.");
                maage_status_trace_recover(trace, item_entities_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_PRESENT), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__);
            }
            outcome = item_entities_status(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_UPDATE);
            continue;
        }
        item = maage_item_registry_get(view->items, view->pickups[animation].pickup.stack.item_id);
        scale = item->model != MAAGE_ITEM_MODEL_SPRITE ? 0.25 : 0.5;
        draw = &view->draws[count++];
        memset(draw, 0, sizeof(*draw));
        draw->mesh = mesh;
        rotation = (double) view->pickups[animation].pickup.age / 20 + (double) ((view->pickups[animation].pickup.entity * 2654435761UL) & 65535UL) / 65536.0 * 6.283185307179586;
        sine = sin(rotation);
        cosine = cos(rotation);
        draw->transform[0] = (float) (cosine * scale);
        draw->transform[2] = (float) (-sine * scale);
        draw->transform[5] = (float) scale;
        draw->transform[8] = (float) (sine * scale);
        draw->transform[10] = (float) (cosine * scale);
        draw->transform[15] = 1;
        for (axis = 0; axis < 3; ++axis) {
            double target = eye[axis] - (axis == 1 ? MAAGE_PLAYER_EYE_HEIGHT - 0.5 : 0);
            double source = view->pickups[animation].pickup.position[axis];
            if (axis == 1) source += 0.1 + sin(rotation + (double) view->pickups[animation].pickup.age / 20) * 0.1 + (item->model != MAAGE_ITEM_MODEL_SPRITE ? 0.125 : 0.25);
            draw->transform[12 + axis] = (float) (source + (target - source) * phase);
        }
        light = snapshot->observation.light_available ? snapshot->observation.light : 64;
        (void) maage_world_present_light(visual_world, (int) floor(draw->transform[12]), (int) floor(draw->transform[13]), (int) floor(draw->transform[14]), &light);
        draw->sky_light = (float) (light >> 4) / 15;
        draw->block_light = (float) (light & 15) / 15;
    }
    (void) interpolation;
    if (additional_count) memcpy(view->draws + count, additional, additional_count * sizeof(*additional));
    count += additional_count;
    result = maage_renderer_set_instances(view->renderer, view->draws, count);
    if (!maage_status_failed(result) && view->diagnostics && view->current.sequence != view->prepared_sequence) {
        view->prepared_sequence = view->current.sequence;
        view->prepared_at = maage_time_monotonic_seconds();
        MAAGE_LOG_VERBOSE_EVENT("maage.client.latency", maage_log_verbose("maage.client.latency", "entity_sequence=%lu entity_tick=%lu prepared_at=%.6f receive_prepare_ms=%.3f instances=%lu", view->current.sequence, view->current.tick, view->prepared_at, (view->prepared_at - view->diagnostic_received_at) * 1000, (unsigned long) (count - additional_count)));
    }
    return maage_status_failed(result) || !outcome ? result : outcome;
}
void maage_client_item_entities_submitted(maage_client_item_entities *view) {
    double now;
    if (!view || !view->diagnostics || view->prepared_sequence == view->submitted_sequence) return;
    now = maage_time_monotonic_seconds();
    view->submitted_sequence = view->prepared_sequence;
    MAAGE_LOG_VERBOSE_EVENT("maage.client.latency", maage_log_verbose("maage.client.latency", "entity_sequence=%lu entity_tick=%lu submitted_at=%.6f prepare_submit_ms=%.3f newest_prepared_entity=%lu", view->submitted_sequence, view->current.tick, now, (now - view->prepared_at) * 1000, view->prepared_entity));
    view->submitted_entity = view->prepared_entity;
}
static void item_entities_variant_destroy(void *opaque) {
    maage_client_model_variant *variant = (maage_client_model_variant *) opaque;
    maage_registry_pages_destroy(&variant->textures, NULL);
    free(variant);
}
maage_status maage_client_item_entities_destroy(maage_client_item_entities *view) {
    size_t i, part;
    maage_client_model_cache *cache, *next;
    unsigned int variant;
    maage_status result, outcome = item_entities_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
    if (!view) return item_entities_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    maage_client_item_entities_clear(view);
    if (view->ordered)
        for (i = 0; i < view->model_count; ++i) {
            if (view->ordered[i].variants)
                for (variant = 0; variant < view->ordered[i].count; ++variant) {
                    result = maage_render_mesh_destroy(view->ordered[i].variants[variant]);
                    if (maage_status_failed(result)) outcome = result;
                }
            free(view->ordered[i].variants);
            free(view->ordered[i].results);
        }
    if (view->entity_models)
        for (i = 0; i < view->entity_model_count; ++i) {
            for (cache = view->entity_models[i].first; cache; cache = next) {
                next = cache->next;
                if (cache->parts)
                    for (part = 0; part < cache->count; ++part) {
                        result = maage_render_mesh_destroy(cache->parts[part]);
                        if (maage_status_failed(result)) outcome = result;
                    }
                free(cache->parts);
                free(cache);
            }
            maage_registry_pages_destroy(&view->entity_models[i].variants, item_entities_variant_destroy);
        }
    free(view->entity_models);
    maage_registry_pages_destroy(&view->models, NULL);
    free(view->ordered);
    free(view->draws);
    free(view->poses);
    free(view->next_poses);
    free(view);
    return outcome;
}
