#include "interaction.h"
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
    const maage_block_state_definition *state, const float uv[MAAGE_CLIENT_TEXTURE_COUNT][4],
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
        }
        for (corner = 0; corner < 6; ++corner) indices[*faces * 6 + corner] = base + offsets[corner];
        ++*faces;
    }
}

void maage_client_interaction_init(maage_client_interaction_visual *visual,
    const maage_client_resources *resources)
{
    memset(visual, 0, sizeof(*visual)); visual->stage = -1;
    memcpy(visual->breaking_uv, resources->breaking_uv, sizeof(visual->breaking_uv));
}

maage_render_result maage_client_interaction_update(maage_client_interaction_visual *visual,
    maage_renderer *renderer, maage_render_texture *atlas,
    const maage_world *world, const maage_gameplay *game)
{
    maage_mesh_vertex vertices[13 * 24];
    unsigned int indices[13 * 36];
    maage_mesh_data data;
    maage_world_observation observation;
    const maage_block_state_definition *state;
    maage_render_mesh *replacement = NULL;
    maage_render_result result;
    double minimum[3], maximum[3], low[3], high[3];
    int stage = -1, axis, first, second, a, b, component;
    unsigned int light;
    size_t faces = 0;
    if (!game->has_target) {
        maage_render_mesh_destroy(visual->mesh); visual->mesh = NULL; visual->visible = 0;
        return MAAGE_RENDER_OK;
    }
    if (game->breaking_active && game->break_progress > 0) {
        stage = (int)(game->break_progress * MAAGE_CLIENT_BREAK_STAGES);
        if (stage >= MAAGE_CLIENT_BREAK_STAGES) stage = MAAGE_CLIENT_BREAK_STAGES - 1;
    }
    if (visual->visible && visual->target.x == game->target.x && visual->target.y == game->target.y &&
        visual->target.z == game->target.z && visual->target.state_id == game->target.state_id && visual->stage == stage)
        return MAAGE_RENDER_OK;
    state = maage_block_registry_state(maage_world_registry(world), game->target.state_id);
    if (!state) return MAAGE_RENDER_INVALID_ARGUMENT;
    for (axis = 0; axis < 3; ++axis) {
        component = axis == 0 ? game->target.x : (axis == 1 ? game->target.y : game->target.z);
        minimum[axis] = component + game->target.bounds.minimum[axis];
        maximum[axis] = component + game->target.bounds.maximum[axis];
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
            if (state->faces[axis].texture_id >= MAAGE_CLIENT_TEXTURE_COUNT) return MAAGE_RENDER_INVALID_ARGUMENT;
        for (axis = 0; axis < 3; ++axis) { low[axis] = minimum[axis] - 0.001; high[axis] = maximum[axis] + 0.001; }
        light = 15;
        if (maage_world_observe(world, game->target.x, game->target.y, game->target.z, &observation) && observation.light_available) {
            light = observation.light >> 4;
            if ((observation.light & 15U) > light) light = observation.light & 15U;
        }
        maage_interaction_box(vertices, indices, &faces, low, high, state, visual->breaking_uv[stage],
            0.12f + 0.88f * (float)light / 15.0f);
    }
    data.vertices = vertices; data.vertex_count = faces * 4;
    data.indices = indices; data.index_count = faces * 6;
    result = maage_renderer_upload_geometry(renderer, &data, atlas, &replacement);
    if (result != MAAGE_RENDER_OK) return result;
    maage_render_mesh_destroy(visual->mesh); visual->mesh = replacement;
    visual->visible = 1; visual->target = game->target; visual->stage = stage;
    return MAAGE_RENDER_OK;
}
