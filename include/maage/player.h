#ifndef MAAGE_PLAYER_H
#define MAAGE_PLAYER_H

#include <maage/world.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Centralized movement parameters. These approximate classic movement
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
#define MAAGE_PLAYER_SPRINT_SPEED 1.3
#define MAAGE_PLAYER_SPRINT_JUMP 0.2
#define MAAGE_PLAYER_SNEAK_INPUT 0.3

typedef struct maage_player {
    double position[3];
    /* Velocity is blocks per simulation tick, following classic tick semantics. */
    double velocity[3];
    int grounded, collided_horizontally;
    int sprinting; /* Authoritative state; gameplay resolves requests/eligibility. */
} maage_player;

typedef struct maage_player_input {
    double forward;
    double strafe;
    /* yaw=0 faces -Z; forward=(sin(yaw),0,-cos(yaw)). */
    double yaw;
    int jump;
    int sprint; /* Held request, never an authoritative speed override. */
    int flying, descend;
} maage_player_input;

/* Owner-thread contracted body observation, shared by movement and gameplay. */
unsigned int maage_player_fluid(const maage_player *player, const maage_world *world);
maage_status maage_player_spawn(maage_player *player, const maage_world *world);
/* Exactly one 20 TPS tick. Caller accumulates wall time and caps catch-up work.
 * Raw platform input and camera interpolation remain outside simulation. The
 * AABB sweeps each crossed voxel plane (Y, X, Z), preventing normal tunneling. */
maage_status maage_player_tick(maage_player *player, const maage_world *world,
                               const maage_player_input *input);

#ifdef __cplusplus
}
#endif
#endif
