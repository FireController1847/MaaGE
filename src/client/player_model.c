#include "player_model.h"
#include <string.h>
#include <math.h>

static const int maage_player_corners[6][4][3] = {
    {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}};
void maage_player_model_boxes(unsigned int model, maage_player_box boxes[MAAGE_PLAYER_BOX_COUNT]) {
    static const double minima[6][3] = {{-4, 24, -4}, {-4, 12, -2}, {-8, 12, -2}, {4, 12, -2}, {-4, 0, -2}, {0, 0, -2}};
    static const double sizes[6][3] = {{8, 8, 8}, {8, 12, 4}, {4, 12, 4}, {4, 12, 4}, {4, 12, 4}, {4, 12, 4}};
    static const int textures[12][2] = {{0, 0}, {16, 16}, {40, 16}, {32, 48}, {0, 16}, {16, 48}, {32, 0}, {16, 32}, {40, 32}, {48, 48}, {0, 32}, {0, 48}};
    unsigned int i, part;
    for (i = 0; i < MAAGE_PLAYER_BOX_COUNT; ++i) {
        part = i % 6;
        memcpy(boxes[i].minimum, minima[part], sizeof(minima[part]));
        memcpy(boxes[i].size, sizes[part], sizeof(sizes[part]));
        boxes[i].part = part;
        boxes[i].inflate = i >= 6 ? (part == MAAGE_PLAYER_HEAD ? 0.5 : 0.25) : 0;
        boxes[i].texture_x = textures[i][0];
        boxes[i].texture_y = textures[i][1];
        if (model == MAAGE_SKIN_SLIM && (part == 2 || part == 3)) {
            boxes[i].size[0] = 3;
            boxes[i].minimum[1] -= 0.5;
            if (part == 2) boxes[i].minimum[0] += 1;
        }
    }
}
void maage_player_box_face(const maage_player_box *box, unsigned int face, double points[4][3], float pixels[4][2]) {
    double u0, u1, v0 = box->texture_y + box->size[2], v1 = v0 + box->size[1];
    double x = box->texture_x, w = box->size[0], d = box->size[2];
    unsigned int corner, axis;
    /* Vertical faces reverse U in this Y-up winding. Top and bottom have
     * independent rectangular regions, including 3-pixel slim-arm ends. */
    if (face == 0) {
        u0 = x + d;
        u1 = x;
    } else if (face == 1) {
        u0 = x + d + w + d;
        u1 = x + d + w;
    } else if (face == 4) {
        u0 = x + d + w;
        u1 = x + d;
    } else if (face == 5) {
        u0 = x + d + w + d + w;
        u1 = x + d + w + d;
    } else if (face == 3) {
        u0 = x + d;
        u1 = u0 + w;
        v0 = box->texture_y + d;
        v1 = box->texture_y;
    } else {
        u0 = x + d + w;
        u1 = u0 + w;
        v0 = box->texture_y;
        v1 = box->texture_y + d;
    }
    for (corner = 0; corner < 4; ++corner) {
        for (axis = 0; axis < 3; ++axis)
            points[corner][axis] = box->minimum[axis] - box->inflate + maage_player_corners[face][corner][axis] * (box->size[axis] + 2 * box->inflate);
        pixels[corner][0] = (float) (corner == 1 || corner == 2 ? u1 : u0);
        pixels[corner][1] = (float) (corner < 2 ? v1 : v0);
    }
}
void maage_player_arm_geometry(unsigned int model, const float skin_uv[4], maage_mesh_vertex vertices[48], unsigned int indices[72], maage_mesh_data *data) {
    static const unsigned int offsets[6] = {0, 1, 2, 0, 2, 3};
    maage_player_box boxes[MAAGE_PLAYER_BOX_COUNT];
    double points[4][3], shoulder = model == MAAGE_SKIN_SLIM ? 21.5 : 22;
    float pixels[4][2];
    unsigned int layer, face, corner, source, base, axis;
    maage_mesh_vertex *vertex;
    maage_player_model_boxes(model, boxes);
    for (layer = 0; layer < 2; ++layer)
        for (face = 0; face < 6; ++face) {
            maage_player_box_face(&boxes[MAAGE_PLAYER_RIGHT_ARM + layer * 6], face, points, pixels);
            base = (layer * 6 + face) * 4;
            for (corner = 0; corner < 4; ++corner) {
                source = 3 - corner;
                vertex = &vertices[base + corner];
                memset(vertex, 0, sizeof(*vertex));
                vertex->position[0] = (float) ((points[source][0] + 5) / 16);
                vertex->position[1] = (float) ((shoulder - points[source][1]) / 16);
                vertex->position[2] = (float) (points[source][2] / 16);
                for (axis = 0; axis < 2; ++axis)
                    vertex->uv[axis] = skin_uv[axis] + pixels[source][axis] / 64 * (skin_uv[axis + 2] - skin_uv[axis]);
                for (axis = 0; axis < 4; ++axis)
                    vertex->color[axis] = 1;
                vertex->normal[face / 2] = (face & 1 ? 1.0f : -1.0f) * (face / 2 == 1 ? -1.0f : 1.0f);
                vertex->material = layer ? MAAGE_MESH_MATERIAL_TRANSLUCENT : MAAGE_MESH_MATERIAL_OPAQUE;
                vertex->light[1] = 1;
            }
            for (corner = 0; corner < 6; ++corner)
                indices[(layer * 6 + face) * 6 + corner] = base + offsets[corner];
        }
    data->vertices = vertices;
    data->indices = indices;
    data->vertex_count = 48;
    data->index_count = 72;
}

void maage_player_matrix_identity(float matrix[16]) {
    unsigned int i;
    memset(matrix, 0, 16 * sizeof(float));
    for (i = 0; i < 4; ++i)
        matrix[i * 5] = 1;
}
void maage_player_matrix_multiply(float output[16], const float a[16], const float b[16]) {
    float value[16];
    unsigned int row, column, k;
    for (column = 0; column < 4; ++column)
        for (row = 0; row < 4; ++row) {
            value[column * 4 + row] = 0;
            for (k = 0; k < 4; ++k)
                value[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
        }
    memcpy(output, value, sizeof(value));
}
void maage_player_matrix_translate(float matrix[16], double x, double y, double z) {
    float value[16];
    maage_player_matrix_identity(value);
    value[12] = (float) x;
    value[13] = (float) y;
    value[14] = (float) z;
    maage_player_matrix_multiply(matrix, matrix, value);
}
void maage_player_matrix_scale(float matrix[16], double x, double y, double z) {
    unsigned int i;
    for (i = 0; i < 4; ++i) {
        matrix[i] *= (float) x;
        matrix[4 + i] *= (float) y;
        matrix[8 + i] *= (float) z;
    }
}
void maage_player_matrix_rotate(float matrix[16], unsigned int axis, double radians) {
    float value[16];
    unsigned int a = (axis + 1) % 3, b = (axis + 2) % 3;
    maage_player_matrix_identity(value);
    value[a * 4 + a] = value[b * 4 + b] = (float) cos(radians);
    value[a * 4 + b] = (float) sin(radians);
    value[b * 4 + a] = (float) -sin(radians);
    maage_player_matrix_multiply(matrix, matrix, value);
}
void maage_player_part_matrix(float matrix[16], const maage_player_pose *pose, unsigned int part) {
    maage_player_matrix_identity(matrix);
    maage_player_matrix_translate(matrix, pose->pivot[part][0], pose->pivot[part][1], pose->pivot[part][2]);
    maage_player_matrix_rotate(matrix, 2, pose->rotation[part][2]);
    maage_player_matrix_rotate(matrix, 1, pose->rotation[part][1]);
    maage_player_matrix_rotate(matrix, 0, pose->rotation[part][0]);
}
void maage_player_pose_calculate(unsigned int model, double phase, double amount, double age, double head_yaw, double pitch, double swing, int held, int sneaking, maage_player_pose *pose) {
    static const double pivots[6][3] = {{0, 24, 0}, {0, 24, 0}, {5, 22, 0}, {-5, 22, 0}, {1.9, 12, 0.1}, {-1.9, 12, 0.1}};
    double torso, eased, wave, head_wave;
    unsigned int part;
    memset(pose, 0, sizeof(*pose));
    memcpy(pose->pivot, pivots, sizeof(pivots));
    if (model == MAAGE_SKIN_SLIM) {
        pose->pivot[2][1] -= 0.5;
        pose->pivot[3][1] -= 0.5;
    }
    pose->rotation[0][0] = -pitch;
    pose->rotation[0][1] = -head_yaw;
    pose->rotation[2][0] = -cos(phase * 0.6662 + 3.141592653589793) * amount;
    pose->rotation[3][0] = -cos(phase * 0.6662) * amount;
    pose->rotation[4][0] = -cos(phase * 0.6662) * 1.4 * amount;
    pose->rotation[5][0] = -cos(phase * 0.6662 + 3.141592653589793) * 1.4 * amount;
    if (held) pose->rotation[2][0] = pose->rotation[2][0] * 0.5 + 3.141592653589793 / 10;
    if (swing > 0 && swing < 1) {
        torso = sin(sqrt(swing) * 6.283185307179586) * 0.2;
        pose->rotation[1][1] = -torso;
        pose->pivot[2][0] = cos(torso) * 5;
        pose->pivot[2][2] = sin(torso) * 5;
        pose->pivot[3][0] = -cos(torso) * 5;
        pose->pivot[3][2] = -sin(torso) * 5;
        pose->rotation[2][1] -= torso * 3;
        pose->rotation[3][1] -= torso;
        pose->rotation[3][0] -= torso;
        eased = 1 - swing;
        eased = 1 - eased * eased * eased * eased;
        wave = sin(eased * 3.141592653589793);
        head_wave = sin(swing * 3.141592653589793) * -(pitch - 0.7) * 0.75;
        pose->rotation[2][0] += wave * 1.2 + head_wave;
        pose->rotation[2][2] = sin(swing * 3.141592653589793) * -0.4;
    }
    if (sneaking) {
        pose->rotation[1][0] = -0.5;
        pose->rotation[2][0] -= 0.4;
        pose->rotation[3][0] -= 0.4;
        for (part = 4; part < 6; ++part) {
            pose->pivot[part][1] = 15;
            pose->pivot[part][2] = 4;
        }
        pose->pivot[0][1] -= 1;
    }
    wave = cos(age * 0.09) * 0.05 + 0.05;
    head_wave = sin(age * 0.067) * 0.05;
    pose->rotation[2][2] += wave;
    pose->rotation[3][2] -= wave;
    pose->rotation[2][0] -= head_wave;
    pose->rotation[3][0] += head_wave;
}
void maage_player_part_geometry(unsigned int model, unsigned int part, const float skin_uv[4], maage_mesh_vertex vertices[48], unsigned int indices[72], maage_mesh_data *data) {
    static const unsigned int offsets[6] = {0, 1, 2, 0, 2, 3};
    static const float shades[6] = {0.75f, 0.75f, 0.6f, 1.0f, 0.85f, 0.85f};
    maage_player_box boxes[MAAGE_PLAYER_BOX_COUNT];
    maage_player_pose pose;
    double points[4][3];
    float pixels[4][2];
    unsigned int layer, face, corner, source, base, axis;
    maage_mesh_vertex *vertex;
    maage_player_model_boxes(model, boxes);
    maage_player_pose_calculate(model, 0, 0, 0, 0, 0, 0, 0, 0, &pose);
    for (layer = 0; layer < 2; ++layer)
        for (face = 0; face < 6; ++face) {
            maage_player_box_face(&boxes[part + layer * 6], face, points, pixels);
            base = (layer * 6 + face) * 4;
            for (corner = 0; corner < 4; ++corner) {
                source = 3 - corner;
                vertex = &vertices[base + corner];
                memset(vertex, 0, sizeof(*vertex));
                vertex->position[0] = (float) (-points[source][0] - pose.pivot[part][0]);
                vertex->position[1] = (float) (points[source][1] - pose.pivot[part][1]);
                vertex->position[2] = (float) (points[source][2] - pose.pivot[part][2]);
                for (axis = 0; axis < 2; ++axis)
                    vertex->uv[axis] = skin_uv[axis] + pixels[source][axis] / 64 * (skin_uv[axis + 2] - skin_uv[axis]);
                for (axis = 0; axis < 4; ++axis)
                    vertex->color[axis] = 1;
                for (axis = 0; axis < 3; ++axis)
                    vertex->color[axis] = shades[face];
                vertex->normal[face / 2] = (face & 1 ? 1.0f : -1.0f) * (face / 2 == 0 ? -1.0f : 1.0f);
                vertex->material = layer ? MAAGE_MESH_MATERIAL_TRANSLUCENT : MAAGE_MESH_MATERIAL_OPAQUE;
                vertex->light[0] = 1;
            }
            for (corner = 0; corner < 6; ++corner)
                indices[(layer * 6 + face) * 6 + corner] = base + offsets[corner];
        }
    data->vertices = vertices;
    data->indices = indices;
    data->vertex_count = 48;
    data->index_count = 72;
}
