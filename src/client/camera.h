#ifndef MAAGE_CLIENT_CAMERA_H
#define MAAGE_CLIENT_CAMERA_H
#include "maage/session_protocol.h"
enum {
    MAAGE_CAMERA_FIRST = 0,
    MAAGE_CAMERA_BACK,
    MAAGE_CAMERA_FRONT
};
#define MAAGE_CLIENT_CAMERA_SAMPLES 8U
typedef struct maage_client_camera_step {
    unsigned long tick;
    int valid;
    double distance, stride, previous_amount, amount, previous_pitch, pitch;
    double previous_eye_drop, eye_drop;
} maage_client_camera_step;
typedef struct maage_client_camera_effects {
    unsigned long session, world, life, tick;
    int initialized, rendered;
    maage_client_camera_step steps[MAAGE_CLIENT_CAMERA_SAMPLES];
    double phase, amount, pitch, eye_drop, hurt_age, death_age;
    double fov_scale;
    float matrix[16];
} maage_client_camera_effects;
/* Feed FIFO raw owner observations before evaluating a frame. Duplicate/stale
 * ticks are NOOP; a missing interval rebases explicitly with PARTIAL. No renderer
 * or live World access is needed. The small ring tolerates events published
 * ahead of the latest snapshot during owner catch-up. */
maage_status maage_client_camera_observe(maage_client_camera_effects *effects, unsigned long session, unsigned long world, unsigned long tick, const maage_session_player_motion *motion);
maage_status maage_client_camera_effects_update(maage_client_camera_effects *effects, const maage_session_snapshot *snapshot, double interpolation, int paused, int bobbing);
/* Pure presentation over a copied collision volume and immutable definitions.
 * Returns the resolved boom length; zero is conservative without a publication. */
double maage_client_camera_position(unsigned int mode, const double eye[3], double yaw, double pitch, int sneaking, double first_person_eye_drop, double aspect, double fov, const maage_world_camera_sample *sample, const maage_block_registry *blocks, double position[3], double *camera_yaw, double *camera_pitch);
#endif
