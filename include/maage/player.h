#ifndef MAAGE_PLAYER_H
#define MAAGE_PLAYER_H

#include <maage/world.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Centralized Phase 1 movement parameters. These approximate classic movement
 * and require programmer fidelity verification against Java Edition 1.7.10;
 * they do not claim complete vanilla movement compatibility. Units are blocks,
 * seconds, and radians; acceleration/drag below are applied per 20 TPS tick. */
#define MAAGE_PLAYER_TICK_SECONDS 0.05
#define MAAGE_PLAYER_WIDTH 0.6
#define MAAGE_PLAYER_HEIGHT 1.8
#define MAAGE_PLAYER_EYE_HEIGHT 1.62
#define MAAGE_PLAYER_GRAVITY_PER_TICK 0.08
#define MAAGE_PLAYER_VERTICAL_DRAG 0.98
#define MAAGE_PLAYER_JUMP_PER_TICK 0.42
#define MAAGE_PLAYER_GROUND_ACCELERATION 0.1
#define MAAGE_PLAYER_AIR_ACCELERATION 0.02
#define MAAGE_PLAYER_GROUND_DRAG 0.546
#define MAAGE_PLAYER_AIR_DRAG 0.91
#define MAAGE_PLAYER_INPUT_DAMPING 0.98

typedef struct maage_player {
    double position[3];
    /* Velocity is blocks per simulation tick, following classic tick semantics. */
    double velocity[3];
    int grounded;
} maage_player;

typedef struct maage_player_input {
    double forward;
    double strafe;
    /* yaw=0 faces -Z; forward=(sin(yaw),0,-cos(yaw)). */
    double yaw;
    int jump;
    int flying, descend;
} maage_player_input;

void maage_player_spawn(maage_player *player, const maage_world *world);
/* Exactly one 20 TPS tick. Caller accumulates wall time and caps catch-up work.
 * Raw platform input and camera interpolation remain outside simulation. The
 * AABB sweeps each crossed voxel plane (Y, X, Z), preventing normal tunneling. */
void maage_player_tick(maage_player *player, const maage_world *world,
    const maage_player_input *input);

#ifdef __cplusplus
}
#endif
#endif
