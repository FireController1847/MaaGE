#ifndef MAAGE_GAMEPLAY_H
#define MAAGE_GAMEPLAY_H
#include "maage/items.h"
#include "maage/player.h"

/* Forward declaration avoids a cycle: session_protocol.h includes gameplay.h. */
struct maage_sound_event;

enum { MAAGE_GAME_MODE_SURVIVAL = 0, MAAGE_GAME_MODE_CREATIVE = 1 };
/* Small inline storage avoids heap traffic in the common case.
 * Additional drops use a growable, owned overflow array: 64 is not a limit. */
#define MAAGE_GAMEPLAY_PICKUPS 64U
typedef struct maage_gameplay_pickup {
    maage_item_stack stack;
    double position[3];
} maage_gameplay_pickup;
/* Owned player checkpoint; storage resolves item identities at the I/O boundary. */
typedef struct maage_gameplay_save {
    maage_inventory inventory;
    unsigned int mode, health, hunger;
    double saturation, exhaustion, fall_distance;
    int flying;
    maage_gameplay_pickup pickups[MAAGE_GAMEPLAY_PICKUPS];
    maage_gameplay_pickup *overflow_pickups;
    size_t overflow_count;
} maage_gameplay_save;
typedef struct maage_gameplay {
    maage_inventory inventory;
    unsigned int mode, health, hunger;
    double saturation, exhaustion, fall_distance;
    int flying;
    maage_gameplay_pickup pickups[MAAGE_GAMEPLAY_PICKUPS];
    maage_gameplay_pickup *overflow_pickups;
    size_t overflow_count, overflow_capacity;
    maage_world_target target, breaking;
    int has_target, breaking_active;
    double break_progress, swing, reach;
    unsigned int use_delay, break_delay, attack_miss_delay, jump_window, hunger_ticks;
    unsigned long ticks, blocks_broken, blocks_placed;
    /* Ephemeral event identity and gait: deliberately excluded from saves. */
    unsigned long sound_sequence, sound_random;
    double footstep_distance;
    unsigned int dig_ticks;
    maage_item_id breaking_item;
    unsigned int breaking_metadata;
    int attack_held, use_held, audio_initialized, in_water;
} maage_gameplay;
typedef struct maage_gameplay_intent {
    maage_player_input movement;
    double pitch;
    int attack, use, jump_pressed, descend;
    int attack_pressed, use_pressed; /* Latched input edges, consumed once. */
} maage_gameplay_intent;

maage_status maage_gameplay_init(maage_gameplay *game, const maage_item_registry *items);
maage_status maage_gameplay_set_mode(maage_gameplay *game, unsigned int mode);
/* Foundation respawn returns to the session's start and retains inventory;
 * persistent spawn points and full death-drop rules are deferred. */
maage_status maage_gameplay_respawn(maage_gameplay *game, maage_player *player,
    const maage_world *world);
/* Call destroy/clear when an initialized game/save is no longer used. An
 * open world_store owns its record's save; returned record copies borrow it.
 * Snapshot allocates a deep copy and reports allocation failure. */
maage_status maage_gameplay_destroy(maage_gameplay *game);
maage_status maage_gameplay_save_clear(maage_gameplay_save *save);
maage_status maage_gameplay_snapshot(const maage_gameplay *game, maage_gameplay_save *save);
maage_status maage_gameplay_restore(maage_gameplay *game, const maage_item_registry *items,
    const maage_gameplay_save *save);
/* Owner-thread targeting may run every frame. Reach zero uses classic 5/4.5
 * Creative/Survival blocks; positive configured reach is bounded to 64. */
maage_status maage_gameplay_target(maage_gameplay *game, const maage_player *player,
    const maage_world *world, double yaw, double pitch);
/* Numeric event construction for Server-owned gameplay/environment occurrences.
 * No audio device, transport, allocation, or string identity is involved. */
maage_status maage_gameplay_build_sound(maage_gameplay *game, struct maage_sound_event *event,
    unsigned int kind, unsigned int material, const double position[3],
    double intensity, double pitch);

/* One fixed-step owner tick. The caller owns the bounded event array.
 * (NULL, 0, NULL) disables event generation. Otherwise
 * written is required, and capacity > 0 requires a non-NULL events array.
 * Audio-event overflow returns PARTIAL without undoing gameplay mutations;
 * gameplay failures take precedence over this publication warning. */
maage_status maage_gameplay_tick(maage_gameplay *game, maage_player *player,
    maage_world *world, const maage_item_registry *items,
    const maage_gameplay_intent *intent, struct maage_sound_event *events,
    size_t capacity, size_t *written, maage_status_trace *trace);
#endif
