#include "interaction.h"
#include "prediction.h"
#include "item_models.h"
#include "maage/log.h"
#include <math.h>
#include <string.h>

static const int maage_interaction_corners[6][4][3] = {
    {{0,0,0},{0,0,1},{0,1,1},{0,1,0}},
    {{1,0,1},{1,0,0},{1,1,0},{1,1,1}},
    {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
    {{0,1,1},{1,1,1},{1,1,0},{0,1,0}},
    {{1,0,0},{0,0,0},{0,1,0},{1,1,0}},
    {{0,0,1},{1,0,1},{1,1,1},{0,1,1}}
};

static void maage_interaction_box(maage_mesh_vertex *vertices, unsigned int *indices,
    size_t *faces, const double minimum[3], const double maximum[3],
    const float uv[4], float brightness, float alpha, float material)
{
    static const unsigned int offsets[6] = {0,1,2,0,2,3};
    maage_mesh_vertex *vertex;
    unsigned int face, corner, axis, base;
    for (face = 0; face < 6; ++face) {
        base = (unsigned int)(*faces * 4);
        for (corner = 0; corner < 4; ++corner) {
            vertex = &vertices[base + corner];
            memset(vertex, 0, sizeof(*vertex));
            for (axis = 0; axis < 3; ++axis)
                vertex->position[axis] = (float)(maage_interaction_corners[face][corner][axis] ? maximum[axis] : minimum[axis]);
            vertex->uv[0] = uv[corner == 1 || corner == 2 ? 2 : 0];
            vertex->uv[1] = uv[corner < 2 ? 3 : 1];
            for (axis = 0; axis < 3; ++axis)
                vertex->color[axis] = brightness;
            vertex->normal[face / 2] = face % 2 ? 1.0f : -1.0f;
            vertex->color[3] = alpha; vertex->material = material;
            vertex->light[1] = 1;
        }
        for (corner = 0; corner < 6; ++corner) indices[*faces * 6 + corner] = base + offsets[corner];
        ++*faces;
    }
}

/* First-person geometry lives in camera space and uses a separate perspective
 * pass. The 1.7.10 ItemRenderer distinguishes block models from extruded icons;
 * the arm uses ModelBiped's original 4x12x4 right-arm dimensions. */
static void maage_first_person_identity(float matrix[16])
{
    memset(matrix, 0, 16 * sizeof(*matrix));
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1;
}
static void maage_first_person_multiply(float output[16], const float left[16],
    const float right[16])
{
    float product[16];
    int row, column, k;
    for (column = 0; column < 4; ++column) for (row = 0; row < 4; ++row) {
        product[column * 4 + row] = 0;
        for (k = 0; k < 4; ++k)
            product[column * 4 + row] += left[k * 4 + row] * right[column * 4 + k];
    }
    memcpy(output, product, sizeof(product));
}
static void maage_first_person_translate(float matrix[16], double x, double y, double z)
{
    float transform[16];
    maage_first_person_identity(transform);
    transform[12] = (float)x; transform[13] = (float)y; transform[14] = (float)z;
    maage_first_person_multiply(matrix, matrix, transform);
}
static void maage_first_person_rotate(float matrix[16], unsigned int axis, double degrees)
{
    float transform[16];
    unsigned int a = (axis + 1) % 3, b = (axis + 2) % 3;
    double radians = degrees * 0.017453292519943295;
    maage_first_person_identity(transform);
    transform[a * 4 + a] = transform[b * 4 + b] = (float)cos(radians);
    transform[a * 4 + b] = (float)sin(radians);
    transform[b * 4 + a] = (float)-sin(radians);
    maage_first_person_multiply(matrix, matrix, transform);
}
static void maage_first_person_scale(float matrix[16], float scale)
{
    float transform[16];
    maage_first_person_identity(transform);
    transform[0] = transform[5] = transform[10] = scale;
    maage_first_person_multiply(matrix, matrix, transform);
}
static maage_status maage_first_person_upload(maage_client_first_person *visual,
    maage_renderer *renderer, maage_render_texture *atlas,
    const maage_item_registry *items, const maage_block_registry *blocks,
    maage_status_trace *trace)
{
    maage_mesh_vertex vertices[66 * 4];
    unsigned int indices[66 * 6];
    maage_mesh_data data;
    maage_block_state_definition arm;
    const maage_item_definition *item;
    maage_render_mesh *replacement = NULL;
    maage_status result;
    double minimum[3], maximum[3];
    float temporary;
    size_t faces = 0;
    unsigned int face, axis;
    visual->sprite = 0;
    if (!visual->item_id) {
        memset(&arm, 0, sizeof(arm));
        for (face = 0; face < MAAGE_FACE_COUNT; ++face) {
            memcpy(arm.faces[face].uv, visual->arm_uv[face], sizeof(arm.faces[face].uv));
            /* ModelBox's skin coordinates use +Y toward the hand. Its four
             * sides reverse both axes relative to world block face mapping. */
            if (face / 2 != 1) {
                temporary = arm.faces[face].uv[0];
                arm.faces[face].uv[0] = arm.faces[face].uv[2];
                arm.faces[face].uv[2] = temporary;
            }
            if (face != MAAGE_FACE_NEGATIVE_Y) {
                temporary = arm.faces[face].uv[1];
                arm.faces[face].uv[1] = arm.faces[face].uv[3];
                arm.faces[face].uv[3] = temporary;
            }
            for (axis = 0; axis < 4; ++axis) arm.faces[face].tint[axis] = 1;
        }
        minimum[0] = -3.0/16; minimum[1] = minimum[2] = -2.0/16;
        maximum[0] = 1.0/16; maximum[1] = 10.0/16; maximum[2] = 2.0/16;
        maage_client_item_box(visual->grass_color, vertices, indices, &faces, minimum, maximum,
            &arm, MAAGE_MESH_MATERIAL_OPAQUE);
    } else {
        maage_item_stack stack;
        stack.item_id = visual->item_id; stack.metadata = visual->metadata; stack.count = 1; stack.damage = 0;
        item = maage_item_registry_get(items, visual->item_id);
        if (!item) return maage_status_make(MAAGE_STATUS_NOT_FOUND, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_LOOKUP, 0);
        visual->sprite = item->model == MAAGE_ITEM_MODEL_SPRITE;
        result = maage_client_item_model(visual->resources, items, blocks, &stack, vertices, indices, &data);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPLOAD);
        faces = data.index_count / 6;
    }
    data.vertices = vertices; data.vertex_count = faces * 4;
    data.indices = indices; data.index_count = faces * 6;
    result = maage_renderer_upload_geometry(renderer, &data, atlas, &replacement, trace);
    if (maage_status_failed(result)) { maage_render_mesh_destroy(replacement); return result; }
    maage_render_mesh_destroy(visual->mesh); visual->mesh = replacement;
    return result;
}

maage_status maage_client_first_person_init(maage_client_first_person *visual,
    const maage_client_resources *resources)
{
    if (!visual || !resources) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE, 0);
    memset(visual, 0, sizeof(*visual));
    visual->resources = resources;
    memcpy(visual->arm_uv, resources->arm_uv, sizeof(visual->arm_uv));
    memcpy(visual->grass_color, resources->inventory_grass_color, sizeof(visual->grass_color));
    maage_first_person_identity(visual->camera_bob);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}

maage_status maage_client_first_person_update(maage_client_first_person *visual,
    maage_renderer *renderer, maage_render_texture *atlas,
    const maage_item_registry *items, const maage_block_registry *blocks,
    const maage_session_snapshot *previous, const maage_session_snapshot *snapshot,
    double interpolation, const double position[3], double yaw, double pitch,
    double elapsed, double aspect, int visible, maage_status_trace *trace)
{
    const maage_item_stack *selected;
    maage_item_id item_id;
    maage_status result;
    float matrix[16], projection[16], sky = 1, emission = 0;
    double target, amount, distance, dx, dz, speed, lag, arc, remaining, swing, wave, root_wave, square_wave;
    unsigned int metadata;
    int changed = 0;
    if (!visual || !renderer || !atlas || !items || !blocks || !previous || !snapshot ||
        !position || !(aspect > 0) || snapshot->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION)
        return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE, 0), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE);
    if (!visible) {
        /* Keep interpolation anchored while a modal hides the hand. A paused
         * scene retains its current bob; a new world starts with no bob. */
        memcpy(visual->last_position, position, sizeof(visual->last_position));
        visual->arm_yaw = yaw; visual->arm_pitch = pitch;
        if (!visual->initialized || visual->session_epoch != snapshot->session_epoch ||
            visual->world_epoch != snapshot->world_epoch) {
            maage_first_person_identity(visual->camera_bob);
            visual->walk_distance = visual->walk_amount = 0;
        }
        return maage_renderer_set_foreground(renderer, NULL, NULL, 0, 0);
    }
    selected = maage_inventory_selected(&snapshot->player.inventory);
    item_id = selected && selected->count ? selected->item_id : 0;
    metadata = item_id ? selected->metadata : 0;
    if (elapsed < 0) elapsed = 0;
    if (elapsed > 0.1) elapsed = 0.1;
    if (!visual->initialized || visual->session_epoch != snapshot->session_epoch ||
        visual->world_epoch != snapshot->world_epoch) {
        visual->initialized = 1; visual->equipped = 1;
        visual->item_id = item_id; visual->metadata = metadata;
        visual->arm_yaw = yaw; visual->arm_pitch = pitch;
        visual->walk_distance = visual->walk_amount = 0;
        memcpy(visual->last_position, position, sizeof(visual->last_position));
        visual->session_epoch = snapshot->session_epoch; visual->world_epoch = snapshot->world_epoch;
        changed = 1;
    }
    target = visual->item_id == item_id && visual->metadata == metadata ? 1 : 0;
    amount = elapsed * 8;
    if (target > visual->equipped) {
        visual->equipped += amount;
        if (visual->equipped > target) visual->equipped = target;
    } else {
        visual->equipped -= amount;
        if (visual->equipped < target) visual->equipped = target;
    }
    if (visual->equipped < 0.1 && (visual->item_id != item_id || visual->metadata != metadata)) {
        visual->item_id = item_id; visual->metadata = metadata; changed = 1;
    }
    if (changed) {
        /* Clear the borrowed pass before replacing its immutable geometry. */
        result = maage_renderer_set_foreground(renderer, NULL, NULL, 0, 0);
        if (maage_status_failed(result)) return result;
        result = maage_first_person_upload(visual, renderer, atlas, items, blocks, trace);
        if (maage_status_failed(result)) {
            if (maage_status_code(result) != MAAGE_STATUS_ALLOCATION_FAILED) return result;
            maage_status_trace_recover(trace, maage_status_make(MAAGE_STATUS_NOOP, 0,
                MAAGE_OPERATION_PRESENT, 0), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT,
                __FILE__, __LINE__);
            if (visual->last_result != result) {
                visual->last_result = result;
                maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.interaction", result, trace,
                    "Item %lu: omitting the optional first-person visual.", visual->item_id);
            }
            maage_render_mesh_destroy(visual->mesh); visual->mesh = NULL;
            return maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE, 0);
        }
    }
    dx = position[0] - visual->last_position[0]; dz = position[2] - visual->last_position[2];
    distance = sqrt(dx*dx + dz*dz);
    memcpy(visual->last_position, position, sizeof(visual->last_position));
    if (snapshot->player.player.grounded && !snapshot->player.flying && distance < 1)
        visual->walk_distance = fmod(visual->walk_distance + distance * 0.6, 2.0);
    speed = sqrt(snapshot->player.player.velocity[0]*snapshot->player.player.velocity[0] +
        snapshot->player.player.velocity[2]*snapshot->player.player.velocity[2]);
    if (speed > 0.1) speed = 0.1;
    if (!snapshot->player.player.grounded || snapshot->player.flying) speed = 0;
    lag = 1 - exp(-elapsed * 10.216512475);
    visual->walk_amount += (speed - visual->walk_amount) * lag;
    maage_first_person_identity(matrix);
    wave = -visual->walk_distance * 3.141592653589793;
    maage_first_person_translate(matrix, sin(wave)*visual->walk_amount*0.5,
        -fabs(cos(wave)*visual->walk_amount), 0);
    maage_first_person_rotate(matrix, 2, sin(wave)*visual->walk_amount*3);
    maage_first_person_rotate(matrix, 0, fabs(cos(wave-0.2)*visual->walk_amount)*5);
    memcpy(visual->camera_bob, matrix, sizeof(visual->camera_bob));
    arc = atan2(sin(yaw-visual->arm_yaw), cos(yaw-visual->arm_yaw));
    maage_first_person_rotate(matrix, 0, (pitch-visual->arm_pitch)*5.7295779513);
    maage_first_person_rotate(matrix, 1, arc*5.7295779513);
    lag = 1 - exp(-elapsed * 13.862943611);
    visual->arm_yaw = yaw - arc*(1-lag);
    visual->arm_pitch += (pitch-visual->arm_pitch)*lag;
    remaining = previous->player.swing;
    if (snapshot->player.swing > remaining || changed) remaining = snapshot->player.swing;
    remaining += (snapshot->player.swing-remaining)*interpolation;
    swing = remaining > 0.00001 ? 1-remaining : 0;
    if (swing < 0) swing = 0;
    if (swing > 1) swing = 1;
    wave = sin(swing*3.141592653589793);
    root_wave = sin(sqrt(swing)*3.141592653589793);
    square_wave = sin(swing*swing*3.141592653589793);
    maage_first_person_translate(matrix, -root_wave*(visual->item_id ? 0.4 : 0.3),
        sin(sqrt(swing)*6.283185307179586)*(visual->item_id ? 0.2 : 0.4),
        -wave*(visual->item_id ? 0.2 : 0.4));
    maage_first_person_translate(matrix, visual->item_id ? 0.56 : 0.64,
        (visual->item_id ? -0.52 : -0.60)-(1-visual->equipped)*0.6, -0.72);
    maage_first_person_rotate(matrix, 1, 45);
    if (visual->item_id) {
        maage_first_person_rotate(matrix, 1, -square_wave*20);
        maage_first_person_rotate(matrix, 2, -root_wave*20);
        maage_first_person_rotate(matrix, 0, -root_wave*80);
        maage_first_person_scale(matrix, 0.4f);
        if (visual->sprite) {
            maage_first_person_translate(matrix, 0, -0.3, 0);
            maage_first_person_scale(matrix, 1.5f);
            maage_first_person_rotate(matrix, 1, 50);
            maage_first_person_rotate(matrix, 2, -25);
            maage_first_person_translate(matrix, -0.4375, 0.4375, 0);
        } else maage_first_person_rotate(matrix, 1, 90);
    } else {
        maage_first_person_rotate(matrix, 1, root_wave*70);
        maage_first_person_rotate(matrix, 2, -square_wave*20);
        maage_first_person_translate(matrix, -1, 3.6, 3.5);
        maage_first_person_rotate(matrix, 2, 120);
        maage_first_person_rotate(matrix, 0, 200);
        maage_first_person_rotate(matrix, 1, -135);
        maage_first_person_translate(matrix, 5.6-5.0/16, 2.0/16, 0);
        maage_first_person_rotate(matrix, 2, 5.7295779513);
    }
    /* Vanilla keeps first-person FOV at 70 degrees independently of world FOV. */
    memset(projection, 0, sizeof(projection));
    projection[0] = (float)(1.4281480067421144 / aspect);
    projection[5] = 1.4281480067421144f;
    projection[10] = 16.0f/(0.05f-16.0f); projection[11] = -1;
    projection[14] = 0.05f*16.0f/(0.05f-16.0f);
    maage_first_person_multiply(matrix, projection, matrix);
    if (snapshot->has_observation && snapshot->observation.light_available) {
        sky = (float)(snapshot->observation.light >> 4)/15;
        emission = (float)(snapshot->observation.light & 15)/15;
    }
    return maage_renderer_set_foreground(renderer, visual->mesh, matrix, sky, emission);
}

maage_status maage_client_first_person_destroy(maage_client_first_person *visual,
    maage_renderer *renderer)
{
    maage_status result, destroyed;
    if (!visual) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    result = renderer ? maage_renderer_set_foreground(renderer, NULL, NULL, 0, 0) :
        maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    destroyed = maage_render_mesh_destroy(visual->mesh);
    memset(visual, 0, sizeof(*visual));
    return maage_status_failed(result) ? result : destroyed;
}

maage_status maage_client_interaction_init(maage_client_interaction_visual *visual,
    const maage_client_resources *resources)
{
    if (!visual || !resources) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE, !visual ? 0 : 1);
    memset(visual, 0, sizeof(*visual)); visual->stage = -1;
    memcpy(visual->breaking_uv, resources->breaking_uv, sizeof(visual->breaking_uv));
    memcpy(visual->white_uv, resources->white_uv, sizeof(visual->white_uv));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}

maage_status maage_client_interaction_update(maage_client_interaction_visual *visual,
    maage_renderer *renderer, maage_render_texture *atlas,
    const maage_block_registry *blocks, const maage_session_snapshot *snapshot,
    const maage_client_prediction *prediction, maage_status_trace *trace)
{
    maage_mesh_vertex vertices[13 * 24];
    unsigned int indices[13 * 36];
    maage_mesh_data data;
    const maage_block_state_definition *state;
    maage_render_mesh *replacement = NULL;
    maage_status result;
    double minimum[3], maximum[3], low[3], high[3];
    int stage = -1, axis, first, second, a, b, component;
    size_t faces = 0;
    if (!visual || !renderer || !atlas || !blocks || !snapshot ||
        snapshot->protocol_version != MAAGE_SESSION_PROTOCOL_VERSION)
        return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE, MAAGE_DETAIL_ARGUMENT_UNKNOWN), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE);
    if (!snapshot->player.has_target || (prediction && maage_client_prediction_hides(prediction,
        snapshot->player.target.x,snapshot->player.target.y,snapshot->player.target.z))) {
        result = maage_status_make(visual->visible ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
        maage_render_mesh_destroy(visual->mesh); visual->mesh = NULL; visual->visible = 0;
        return result;
    }
    if (snapshot->player.breaking_active && snapshot->player.break_progress > 0) {
        stage = (int)(snapshot->player.break_progress * MAAGE_CLIENT_BREAK_STAGES) - 1;
        if (stage >= MAAGE_CLIENT_BREAK_STAGES) stage = MAAGE_CLIENT_BREAK_STAGES - 1;
    }
    if (visual->visible && visual->session_epoch == snapshot->session_epoch &&
        visual->world_epoch == snapshot->world_epoch &&
        visual->target.x == snapshot->player.target.x && visual->target.y == snapshot->player.target.y &&
        visual->target.z == snapshot->player.target.z && visual->target.state_id == snapshot->player.target.state_id && visual->stage == stage)
        return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    state = maage_block_registry_state(blocks, snapshot->player.target.state_id);
    if (!state) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
    for (axis = 0; axis < 3; ++axis) {
        component = axis == 0 ? snapshot->player.target.x : (axis == 1 ? snapshot->player.target.y : snapshot->player.target.z);
        minimum[axis] = component + snapshot->player.target.bounds.minimum[axis];
        maximum[axis] = component + snapshot->player.target.bounds.maximum[axis];
    }
    /* Twelve thin cuboids preserve depth occlusion using the mesh pipeline. */
    for (axis = 0; axis < 3; ++axis) {
        first = (axis + 1) % 3; second = (axis + 2) % 3;
        for (a = 0; a < 2; ++a) for (b = 0; b < 2; ++b) {
            for (component = 0; component < 3; ++component) {
                low[component] = minimum[component] - 0.004;
                high[component] = maximum[component] + 0.004;
            }
            low[first] = (a ? maximum[first] : minimum[first]) - 0.004;
            high[first] = low[first] + 0.008;
            low[second] = (b ? maximum[second] : minimum[second]) - 0.004;
            high[second] = low[second] + 0.008;
            maage_interaction_box(vertices, indices, &faces, low, high,
                visual->white_uv, 0, 0.4f, MAAGE_MESH_MATERIAL_TRANSLUCENT);
        }
    }
    if (stage >= 0) {
        for (axis = 0; axis < 3; ++axis) { low[axis] = minimum[axis] - 0.001; high[axis] = maximum[axis] + 0.001; }
        /* Multiply the already-lit surface instead of replacing it with a
         * fullbright precomposite; the original biome tint survives unchanged. */
        maage_interaction_box(vertices, indices, &faces, low, high,
            visual->breaking_uv[stage], 1, 1, MAAGE_MESH_MATERIAL_MULTIPLY);
    }
    data.vertices = vertices; data.vertex_count = faces * 4;
    data.indices = indices; data.index_count = faces * 6;
    result = maage_renderer_upload_geometry(renderer, &data, atlas, &replacement, trace);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) {
        maage_render_mesh_destroy(replacement);
        if (maage_status_code(result) != MAAGE_STATUS_ALLOCATION_FAILED) return result;
        maage_status_trace_recover(trace, maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_PRESENT, 0),
            MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PRESENT, __FILE__, __LINE__);
        if (visual->last_result != result) {
            visual->last_result = result;
            maage_log_trace_status(MAAGE_LOG_WARNING, "maage.client.interaction", visual->last_result, trace,
                "Target %d,%d,%d: omitting the optional selection visual.",
                snapshot->player.target.x, snapshot->player.target.y, snapshot->player.target.z);
        }
        maage_render_mesh_destroy(visual->mesh); visual->mesh = NULL;
        visual->visible = 1; visual->target = snapshot->player.target; visual->stage = stage;
        visual->session_epoch = snapshot->session_epoch; visual->world_epoch = snapshot->world_epoch;
        return maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_UPDATE, 0);
    }
    maage_render_mesh_destroy(visual->mesh); visual->mesh = replacement;
    visual->visible = 1; visual->target = snapshot->player.target; visual->stage = stage;
        visual->session_epoch = snapshot->session_epoch; visual->world_epoch = snapshot->world_epoch;
    return MAAGE_STATUS_VALUE(MAAGE_STATUS_SUCCESS, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
}
