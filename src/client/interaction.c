#include "interaction.h"
#include "maage/log.h"
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
    const maage_block_state_definition *state, const float uv[MAAGE_CONTENT_TEXTURE_COUNT][4],
    float brightness)
{
    static const unsigned int offsets[6] = {0,1,2,0,2,3};
    static const float shade[6] = {0.6f,0.6f,0.5f,1,0.8f,0.8f};
    maage_mesh_vertex *vertex;
    const float *rectangle;
    unsigned int face, corner, axis, base;
    for (face = 0; face < 6; ++face) {
        rectangle = uv ? uv[state->faces[face].texture_id] : state->faces[face].uv;
        base = (unsigned int)(*faces * 4);
        for (corner = 0; corner < 4; ++corner) {
            vertex = &vertices[base + corner];
            memset(vertex, 0, sizeof(*vertex));
            for (axis = 0; axis < 3; ++axis)
                vertex->position[axis] = (float)(maage_interaction_corners[face][corner][axis] ? maximum[axis] : minimum[axis]);
            vertex->uv[0] = rectangle[corner == 1 || corner == 2 ? 2 : 0];
            vertex->uv[1] = rectangle[corner < 2 ? 3 : 1];
            for (axis = 0; axis < 3; ++axis)
                vertex->color[axis] = brightness * shade[face] * state->faces[face].tint[axis];
            vertex->color[3] = 1;
            vertex->light[1] = 1;
        }
        for (corner = 0; corner < 6; ++corner) indices[*faces * 6 + corner] = base + offsets[corner];
        ++*faces;
    }
}

maage_status maage_client_interaction_init(maage_client_interaction_visual *visual,
    const maage_client_resources *resources)
{
    if (!visual || !resources) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_INITIALIZE, !visual ? 0 : 1);
    memset(visual, 0, sizeof(*visual)); visual->stage = -1;
    memcpy(visual->breaking_uv, resources->breaking_uv, sizeof(visual->breaking_uv));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}

maage_status maage_client_interaction_update(maage_client_interaction_visual *visual,
    maage_renderer *renderer, maage_render_texture *atlas,
    const maage_block_registry *blocks, const maage_session_snapshot *snapshot,
    maage_status_trace *trace)
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
    if (!snapshot->player.has_target) {
        result = maage_status_make(visual->visible ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
        maage_render_mesh_destroy(visual->mesh); visual->mesh = NULL; visual->visible = 0;
        return result;
    }
    if (snapshot->player.breaking_active && snapshot->player.break_progress > 0) {
        stage = (int)(snapshot->player.break_progress * MAAGE_CLIENT_BREAK_STAGES);
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
            maage_interaction_box(vertices, indices, &faces, low, high, state, NULL, 0);
        }
    }
    if (stage >= 0) {
        for (axis = 0; axis < MAAGE_FACE_COUNT; ++axis)
            if (state->faces[axis].texture_id >= MAAGE_CONTENT_TEXTURE_COUNT) return MAAGE_STATUS_VALUE(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RENDER, MAAGE_OPERATION_INITIALIZE, 0);
        for (axis = 0; axis < 3; ++axis) { low[axis] = minimum[axis] - 0.001; high[axis] = maximum[axis] + 0.001; }
        /* v1 observes light at the player, not the targeted block. A consistent
         * fullbright crack is preferable to an unsafe World query or using
         * unrelated light. Target light is a future copied snapshot field. */
        maage_interaction_box(vertices, indices, &faces, low, high, state,
            visual->breaking_uv[stage], 1.0f);
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
