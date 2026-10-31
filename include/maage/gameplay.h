#ifndef MAAGE_GAMEPLAY_H
#define MAAGE_GAMEPLAY_H
#include "maage/items.h"
#include "maage/crafting.h"
#include "maage/player.h"

/* Forward declaration avoids a cycle: session_protocol.h includes gameplay.h. */
struct maage_sound_event;

enum {
    MAAGE_GAME_MODE_SURVIVAL = 0,
    MAAGE_GAME_MODE_CREATIVE = 1
};
/* Owned player checkpoint; storage resolves item identities at the I/O boundary. */
typedef struct maage_gameplay_save {
    maage_inventory inventory;
    maage_item_stack crafting_inputs[MAAGE_CRAFTING_CELLS]; /* Unreturned inputs only; no saved container. */
    unsigned int mode, health, hunger;
    int air; /* -19..300 owner ticks; negative phase precedes drowning damage. */
    double saturation, exhaustion, fall_distance;
    int flying;
} maage_gameplay_save;
typedef struct maage_gameplay {
    maage_inventory inventory;
    maage_crafting_container crafting;
    unsigned int mode, health, hunger;
    int air; /* -19..300 owner ticks; negative phase precedes drowning damage. */
    double saturation, exhaustion, fall_distance;
    int flying;
    unsigned int sprint_window, sprint_ticks;
    int forward_down;
    maage_world_target target, breaking, last_break, last_place;
    maage_block_state_id last_placed_state;
    int has_target, breaking_active;
    double break_progress, swing, reach;
    unsigned long swing_tick;
    unsigned int swing_kind; /* 1 attack, 2 successful placement */
    unsigned int use_delay, break_delay, attack_miss_delay, jump_window, hunger_ticks;
    unsigned int jump_repeat_ticks; /* Held grounded jumps; release rearms immediately. */
    unsigned long ticks, blocks_broken, blocks_placed;
    /* Raw travel and gameplay occurrence markers, never camera animations.
     * Ephemeral within a life; excluded from the authoritative checkpoint. */
    double walk_distance, damage_yaw;
    unsigned long life, damage_sequence, damage_tick, death_tick;
    /* Ephemeral event identity and gait: deliberately excluded from saves. */
    unsigned long sound_sequence, sound_random;
    double footstep_distance;
    unsigned int dig_ticks;
    maage_item_id breaking_item;
    unsigned int breaking_metadata, breaking_damage;
    int attack_held, use_held, audio_initialized, in_water;
} maage_gameplay;
typedef struct maage_gameplay_intent {
    maage_player_input movement;
    double pitch;
    int attack, use, jump_pressed, descend;
    int forward_pressed;             /* Latched forward rising edge; Server owns tap timing. */
    int attack_pressed, use_pressed; /* Latched input edges, consumed once. */
} maage_gameplay_intent;

/* Read-only ordinary block placement rules, shared by Server authority and
 * Client presentation. SUCCESS returns the expected destination and new state;
 * NOOP defers unavailable/invalid placement or non-block interaction. The caller
 * resolves block activation first, including the held-item crouch bypass. */
maage_status maage_gameplay_placement(const maage_player *player, const maage_world *world, const maage_item_registry *items, const maage_item_stack *held, const maage_world_target *target, double yaw, maage_world_target *destination, maage_block_state_id *placed);
maage_status maage_gameplay_init(maage_gameplay *game, const maage_item_registry *items);
maage_status maage_gameplay_set_mode(maage_gameplay *game, unsigned int mode);
/* Foundation respawn returns to the session's start and retains inventory;
 * persistent spawn points and full death-drop rules are deferred. */
maage_status maage_gameplay_respawn(maage_gameplay *game, maage_player *player, const maage_world *world);
/* Player-only checkpoint; World owns and checkpoints entities independently. */
maage_status maage_gameplay_destroy(maage_gameplay *game);
maage_status maage_gameplay_save_clear(maage_gameplay_save *save);
maage_status maage_gameplay_snapshot(const maage_gameplay *game, maage_gameplay_save *save);
maage_status maage_gameplay_restore(maage_gameplay *game, const maage_item_registry *items, const maage_gameplay_save *save);
/* Slot == MAAGE_INVENTORY_SLOTS selects the inventory cursor. */
maage_status maage_gameplay_drop(maage_gameplay *game, const maage_player *player, maage_world *world, const maage_item_registry *items, double yaw, double pitch, unsigned int slot, int whole);
/* Owner-thread targeting may run every frame. Reach zero uses classic 5/4.5
 * Creative/Survival blocks; positive configured reach is bounded to 64. */
maage_status maage_gameplay_target(maage_gameplay *game, const maage_player *player, const maage_world *world, double yaw, double pitch);
/* Numeric event construction for Server-owned gameplay/environment occurrences.
 * No audio device, transport, allocation, or string identity is involved. */
maage_status maage_gameplay_build_sound(maage_gameplay *game, struct maage_sound_event *event, unsigned int kind, unsigned int material, const double position[3], double intensity, double pitch);

/* One fixed-step owner tick. The caller owns the bounded event array.
 * (NULL, 0, NULL) disables event generation. Otherwise
 * written is required, and capacity > 0 requires a non-NULL events array.
 * Audio-event overflow returns PARTIAL without undoing gameplay mutations;
 * gameplay failures take precedence over this publication warning. */
maage_status maage_gameplay_tick(maage_gameplay *game, maage_player *player, maage_world *world, const maage_item_registry *items, const maage_gameplay_intent *intent, struct maage_sound_event *events, size_t capacity, size_t *written, maage_status_trace *trace);
#endif
