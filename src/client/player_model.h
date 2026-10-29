#ifndef MAAGE_CLIENT_PLAYER_MODEL_H
#define MAAGE_CLIENT_PLAYER_MODEL_H
#include "maage/skin.h"
#include "maage/geometry.h"
#define MAAGE_PLAYER_BOX_COUNT 12U
#define MAAGE_PLAYER_HEAD 0U
#define MAAGE_PLAYER_RIGHT_ARM 2U
#define MAAGE_PLAYER_PARTS 6U
/* Six base cuboids followed by their outer layers. Positions use model pixels,
 * Y up from the feet; UVs always address the normalized 64x64 skin. */
typedef struct maage_player_box {
    double minimum[3], size[3], inflate;
    int texture_x, texture_y;
    unsigned int part;
} maage_player_box;
typedef struct maage_player_pose {
    double pivot[MAAGE_PLAYER_PARTS][3], rotation[MAAGE_PLAYER_PARTS][3];
} maage_player_pose;
/* Y-up anatomical basis used by world instances. GUI projection shares the
 * cuboids/UVs and can reflect X back into its front-facing screen basis. */
void maage_player_pose_calculate(unsigned int model, double phase, double amount, double age, double head_yaw, double pitch, double swing, int held, int sneaking, maage_player_pose *pose);
void maage_player_part_geometry(unsigned int model, unsigned int part, const float skin_uv[4], maage_mesh_vertex vertices[48], unsigned int indices[72], maage_mesh_data *data);
void maage_player_matrix_identity(float matrix[16]);
void maage_player_matrix_multiply(float output[16], const float a[16], const float b[16]);
void maage_player_matrix_translate(float matrix[16], double x, double y, double z);
void maage_player_matrix_rotate(float matrix[16], unsigned int axis, double radians);
void maage_player_matrix_scale(float matrix[16], double x, double y, double z);
void maage_player_part_matrix(float matrix[16], const maage_player_pose *pose, unsigned int part);
void maage_player_model_boxes(unsigned int model, maage_player_box boxes[MAAGE_PLAYER_BOX_COUNT]);
void maage_player_box_face(const maage_player_box *box, unsigned int face, double points[4][3], float pixels[4][2]);
void maage_player_arm_geometry(unsigned int model, const float skin_uv[4], maage_mesh_vertex vertices[48], unsigned int indices[72], maage_mesh_data *data);
#endif
