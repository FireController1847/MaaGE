#ifndef MAAGE_CLIENT_CAMERA_H
#define MAAGE_CLIENT_CAMERA_H
#include "maage/session_protocol.h"
enum {
    MAAGE_CAMERA_FIRST = 0,
    MAAGE_CAMERA_BACK,
    MAAGE_CAMERA_FRONT
};
/* Pure presentation over a copied collision volume and immutable definitions.
 * Returns the resolved boom length; zero is conservative without a publication. */
double maage_client_camera_position(unsigned int mode, const double eye[3], double yaw, double pitch, int sneaking, double aspect, double fov, const maage_world_camera_sample *sample, const maage_block_registry *blocks, double position[3], double *camera_yaw, double *camera_pitch);
#endif
