#include "item_entities.h"
#include "maage/internal/registry_pages.h"
#include "maage/log.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct maage_item_models {
    maage_render_mesh **variants;
    maage_status *results;
    unsigned int count;
} maage_item_models;
struct maage_client_item_entities {
    maage_renderer *renderer;
    maage_render_texture *atlas;
    const maage_client_resources *resources;
    const maage_item_registry *items;
    const maage_block_registry *blocks;
    maage_registry_pages models;
    maage_item_models *ordered;
    size_t model_count;
    maage_session_entities current;
    maage_render_instance *draws;
    size_t capacity;
    double received;
};
static maage_status item_entities_status(unsigned char code, unsigned char operation)
{ return maage_status_make(code, MAAGE_CONTEXT_CLIENT, operation, 0); }
maage_status maage_client_item_entities_create(maage_renderer *renderer,
    maage_render_texture *atlas, const maage_client_resources *resources,
    const maage_item_registry *items, const maage_block_registry *blocks,
    maage_client_item_entities **output, maage_status_trace *trace)
{
    maage_client_item_entities *view;
    const maage_item_definition *item;
    void **slot;
    size_t i;
    maage_status result = item_entities_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    if (!renderer || !atlas || !resources || !items || !blocks || !output)
        return item_entities_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    *output = NULL; view = (maage_client_item_entities *)calloc(1, sizeof(*view));
    if (!view) return result;
    view->renderer = renderer; view->atlas = atlas; view->resources = resources;
    view->items = items; view->blocks = blocks;
    view->model_count = maage_item_registry_count(items);
    view->ordered = (maage_item_models *)calloc(view->model_count, sizeof(*view->ordered));
    if (!view->ordered) goto failed;
    for (i = 0; i < view->model_count; ++i) {
        item = maage_item_registry_at(items, i);
        if (item->max_metadata == (unsigned int)-1) goto failed;
        view->ordered[i].count = item->max_metadata+1;
        view->ordered[i].variants = (maage_render_mesh **)calloc(view->ordered[i].count, sizeof(maage_render_mesh *));
        view->ordered[i].results = (maage_status *)calloc(view->ordered[i].count, sizeof(maage_status));
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
static maage_status item_entities_model(maage_client_item_entities *view,
    const maage_item_stack *stack, const maage_render_mesh **mesh, int *notice, maage_status_trace *trace)
{
    void **slot = maage_registry_pages_find(&view->models, stack->item_id);
    maage_item_models *models = slot ? (maage_item_models *)*slot : NULL;
    maage_mesh_vertex vertices[MAAGE_ITEM_MODEL_FACES*4];
    unsigned int indices[MAAGE_ITEM_MODEL_FACES*6];
    maage_mesh_data data;
    maage_status result;
    *notice = 0;
    if (!models || stack->metadata >= models->count || !maage_item_stack_valid(view->items, stack))
        return item_entities_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_LOOKUP);
    if (!models->variants[stack->metadata]) {
        if (maage_status_failed(models->results[stack->metadata])) return models->results[stack->metadata];
        result = maage_client_item_model(view->resources, view->items, view->blocks, stack, vertices, indices, &data);
        if (!maage_status_failed(result)) result = maage_renderer_upload_geometry(view->renderer, &data,
            view->atlas, &models->variants[stack->metadata], trace);
        models->results[stack->metadata] = result;
        if (maage_status_failed(result)) { *notice = 1; return result; }
    }
    *mesh = models->variants[stack->metadata];
    return item_entities_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_LOOKUP);
}
maage_status maage_client_item_entities_clear(maage_client_item_entities *view)
{
    if (!view) return item_entities_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    maage_renderer_set_instances(view->renderer, NULL, 0);
    free(view->current.entities); memset(&view->current, 0, sizeof(view->current));
    return item_entities_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
maage_status maage_client_item_entities_update(maage_client_item_entities *view,
    maage_local_session *session, const maage_session_snapshot *snapshot,
    double interpolation, const double eye[3], double now, maage_status_trace *trace)
{
    maage_session_entities incoming;
    maage_render_instance *draws, *draw;
    const maage_entity *entity;
    const maage_item_definition *item;
    const maage_render_mesh *mesh;
    maage_status result, outcome = 0;
    size_t i, count = 0, needed;
    unsigned int copy, copies, axis;
    int notice;
    unsigned long random;
    double position[3], phase, rotation, scale, offset[3], dx, dy, dz, age, sine, cosine, alpha;
    if (!view || !session || !snapshot || !eye) return item_entities_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE);
    result = maage_local_session_take_entities(session, &incoming);
    if (maage_status_code(result) == MAAGE_STATUS_SUCCESS) {
        if (incoming.protocol_version == MAAGE_SESSION_PROTOCOL_VERSION &&
            incoming.session_epoch == snapshot->session_epoch && incoming.world_epoch == snapshot->world_epoch &&
            (!view->current.sequence || view->current.session_epoch != incoming.session_epoch ||
             view->current.world_epoch != incoming.world_epoch || incoming.sequence > view->current.sequence)) {
            if (view->current.tick != incoming.tick || view->current.world_epoch != incoming.world_epoch ||
                view->current.session_epoch != incoming.session_epoch) view->received = now;
            free(view->current.entities); view->current = incoming;
        } else free(incoming.entities);
    } else if (maage_status_failed(result) && maage_status_code(result) != MAAGE_STATUS_INVALID_STATE) return result;
    if (view->current.session_epoch != snapshot->session_epoch || view->current.world_epoch != snapshot->world_epoch)
        return maage_client_item_entities_clear(view);
    if (view->current.count > (size_t)-1 / 5 / sizeof(*draws))
        return item_entities_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_UPDATE);
    needed = view->current.count*5;
    if (needed > view->capacity) {
        draws = (maage_render_instance *)realloc(view->draws, needed*sizeof(*draws));
        if (!draws) return item_entities_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_UPDATE);
        view->draws = draws; view->capacity = needed;
    }
    alpha = (now-view->received)/0.05;
    if (snapshot->paused) alpha = 1;
    if (alpha < 0) alpha = 0;
    if (alpha > 1) alpha = 1;
    for (i = 0; i < view->current.count; ++i) {
        entity = &view->current.entities[i];
        if (!maage_entity_valid(entity, view->items) || (i && view->current.entities[i-1].id >= entity->id)) return item_entities_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_UPDATE);
        for (axis = 0; axis < 3; ++axis) position[axis] = entity->previous[axis]+(entity->position[axis]-entity->previous[axis])*alpha;
        dx = position[0]-eye[0]; dy = position[1]-eye[1]; dz = position[2]-eye[2];
        if (dx*dx+dy*dy+dz*dz > 4096) continue;
        result = item_entities_model(view, &entity->payload.item, &mesh, &notice, trace);
        if (maage_status_failed(result)) {
            if (maage_status_code(result) != MAAGE_STATUS_ALLOCATION_FAILED &&
                maage_status_code(result) != MAAGE_STATUS_BUFFER_OVERFLOW) return result;
            if (notice) {
                MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPLOAD);
                maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.items", result, trace,
                    "Item model unavailable; omitting affected instances until resource reload.");
                maage_status_trace_recover(trace, item_entities_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_PRESENT),
                    MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__);
                outcome = item_entities_status(MAAGE_STATUS_PARTIAL, MAAGE_OPERATION_UPDATE);
            }
            continue;
        }
        item = maage_item_registry_get(view->items, entity->payload.item.item_id);
        phase = (double)((entity->id*2654435761UL)&65535UL)/65536.0*6.283185307179586;
        age = entity->age + (snapshot->paused ? 0 : alpha);
        rotation = age/20+phase; sine = sin(rotation); cosine = cos(rotation);
        scale = item->model == MAAGE_ITEM_MODEL_BLOCK ? 0.25 : 0.5;
        position[1] += 0.1+sin(age/10+phase)*0.1 + (item->model == MAAGE_ITEM_MODEL_BLOCK ? 0.125 : 0.25);
        copies = entity->payload.item.count > 40 ? 5 : (entity->payload.item.count > 20 ? 4 :
            (entity->payload.item.count > 5 ? 3 : (entity->payload.item.count > 1 ? 2 : 1)));
        if (item->model == MAAGE_ITEM_MODEL_SPRITE)
            copies = entity->payload.item.count >= 32 ? 4 : (entity->payload.item.count >= 16 ? 3 :
                (entity->payload.item.count >= 2 ? 2 : 1));
        random = entity->id;
        for (copy = 0; copy < copies; ++copy) {
            for (axis = 0; axis < 3; ++axis) {
                random = (random*1664525UL+1013904223UL)&4294967295UL;
                offset[axis] = copy ? ((double)(random&255UL)/255.0-0.5)*0.15 : 0;
            }
            if (item->model == MAAGE_ITEM_MODEL_SPRITE) {
                offset[2] = ((double)copy-((double)copies-1)*0.5)*0.0421875;
                offset[0] = sine*offset[2]; offset[2] *= cosine; offset[1] = 0;
            }
            draw = &view->draws[count++]; memset(draw, 0, sizeof(*draw)); draw->mesh = mesh;
            draw->transform[0] = (float)(cosine*scale); draw->transform[2] = (float)(-sine*scale);
            draw->transform[5] = (float)scale; draw->transform[8] = (float)(sine*scale);
            draw->transform[10] = (float)(cosine*scale); draw->transform[15] = 1;
            for (axis = 0; axis < 3; ++axis) draw->transform[12+axis] = (float)(position[axis]+offset[axis]);
            draw->sky_light = (float)(entity->light>>4)/15;
            draw->block_light = (float)(entity->light&15)/15;
        }
    }
    (void)interpolation;
    result = maage_renderer_set_instances(view->renderer, view->draws, count);
    return maage_status_failed(result) || !outcome ? result : outcome;
}
maage_status maage_client_item_entities_destroy(maage_client_item_entities *view)
{
    size_t i;
    unsigned int variant;
    maage_status result, outcome = item_entities_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
    if (!view) return item_entities_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    maage_client_item_entities_clear(view);
    if (view->ordered) for (i = 0; i < view->model_count; ++i) {
        if (view->ordered[i].variants) for (variant = 0; variant < view->ordered[i].count; ++variant) {
            result = maage_render_mesh_destroy(view->ordered[i].variants[variant]);
            if (maage_status_failed(result)) outcome = result;
        }
        free(view->ordered[i].variants); free(view->ordered[i].results);
    }
    maage_registry_pages_destroy(&view->models, NULL);
    free(view->ordered); free(view->draws); free(view);
    return outcome;
}
