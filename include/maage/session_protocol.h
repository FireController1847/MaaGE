#ifndef MAAGE_SESSION_PROTOCOL_H
#define MAAGE_SESSION_PROTOCOL_H

#include "maage/environment.h"
#include "maage/gameplay.h"
#include "maage/entities.h"
#include "maage/status.h"
#include "maage/sounds.h"
#include "maage/creative.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Numeric local-session contract. Future network encoding must serialize fields
 * explicitly, not memcpy C layouts (including unsigned long and padding).
 * Local hosting validates the independently composed Server against its Client
 * content identity peer before World startup or publishing any numeric data. */
#define MAAGE_SESSION_PROTOCOL_VERSION 24U

typedef enum maage_session_command_kind {
    MAAGE_CMD_INPUT = 1,
    MAAGE_CMD_HOTBAR = 2,
    MAAGE_CMD_CREATIVE_ACTION = 4,
    MAAGE_CMD_GAME_MODE = 5,
    MAAGE_CMD_RESPAWN = 6,
    MAAGE_CMD_PAUSE = 7,
    MAAGE_CMD_VIEW = 8,
    MAAGE_CMD_DISCONNECT = 9,
    MAAGE_CMD_DROP = 10,
    MAAGE_CMD_CONTAINER_OPEN = 11,
    MAAGE_CMD_CONTAINER_CLOSE = 12,
    MAAGE_CMD_CONTAINER_SLOT = 13,
    MAAGE_CMD_BREAK_PREVIEW = 14,
    MAAGE_CMD_PLACE_PREVIEW = 15,
    MAAGE_CMD_CONTAINER_PAGE = 16
} maage_session_command_kind;

enum {
    MAAGE_CREATIVE_TAKE = 1,
    MAAGE_CREATIVE_TAKE_STACK,
    MAAGE_CREATIVE_CLONE,
    MAAGE_CREATIVE_HOTBAR,
    MAAGE_CREATIVE_DROP,
    MAAGE_CREATIVE_PLAYER_SLOT,
    MAAGE_CREATIVE_PLAYER_DROP,
    MAAGE_CREATIVE_DESTROY,
    MAAGE_CREATIVE_CLEAR,
    MAAGE_CREATIVE_OUTSIDE
};
typedef struct maage_creative_action {
    unsigned int kind, secondary, hotbar;
    maage_creative_tab_id tab;     /* Policy/entry scope only; tab selection is local. */
    maage_creative_entry_id entry; /* Zero denotes an empty catalog cell. */
    maage_inventory_operation inventory;
} maage_creative_action;

typedef struct maage_session_command {
    unsigned int protocol_version;
    unsigned int kind;
    unsigned long sequence;                /* Nonzero and increasing within the session. */
    double sampled_at, queued_at, sent_at; /* Optional local diagnostic clock; -1 when unavailable. */
    maage_gameplay_intent intent;
    unsigned int hotbar_slot;
    unsigned int inventory_slot;
    /* OPEN requests the player container only; tables open from authoritative
     * use/raycast. CLOSE/SLOT require the current nonzero container identity.
     * RESULT and Creative actions require the published revision. Creative
     * references immutable catalog entry IDs; match stacks are comparison keys,
     * never Client-owned inventory or claimed outputs. */
    unsigned long container_identity, container_revision;
    unsigned long container_view_first;
    unsigned int slot_group, container_slot, slot_action;
    unsigned long prediction;
    unsigned long prediction_revision;         /* Replica baseline before this expected-state edit. */
    unsigned long prediction_command_sequence; /* Input anchor for action/tick diagnostics. */
    maage_world_target predicted_target;       /* Request only; never a claimed mutation. */
    maage_block_state_id predicted_state;      /* PLACE_PREVIEW result to acknowledge, never execute. */
    int secondary_click;
    maage_creative_action creative;
    unsigned int game_mode; /* Request; server validates and applies rules. */
    int paused;             /* Request, not a write into authoritative simulation. */
    int render_radius;
    double view_yaw;
} maage_session_command;

typedef struct maage_sound_event {
    maage_sound_id event_id;
    maage_sound_material_id material;
    unsigned int action; /* Engine occurrence hook; zero for a directly emitted event. */
    unsigned long tick;
    unsigned long sequence;
    double position[3];
    double intensity;
    double pitch;
} maage_sound_event;

/* Raw owner-step observations. FIFO motion events preserve intermediate steps
 * when the latest-state snapshot mailbox coalesces ticks on a slower Client. */
typedef struct maage_session_player_motion {
    double velocity[3], walk_distance;
    unsigned long life;
    int grounded, alive, sneaking;
} maage_session_player_motion;
typedef struct maage_session_player_view {
    maage_player player;
    double yaw, pitch;
    maage_inventory inventory;
    maage_crafting_container crafting; /* Copied authoritative temporary state. */
    unsigned long experience;
    maage_food_use food; /* Copied active consumption; animation remains Client-owned. */
    unsigned int game_mode, health, hunger;
    int air; /* Authoritative remaining breath and drowning phase, -19..300. */
    double saturation;
    int flying, sneaking;
    maage_session_player_motion motion;
    unsigned long damage_sequence, damage_tick, death_tick;
    double damage_yaw; /* Incoming bearing relative to body yaw, radians; zero for current environmental damage. */
    int has_target, breaking_active, submerged;
    double reach;
    unsigned int break_delay, attack_miss_delay;
    maage_world_target target, breaking;
    double break_progress;
    maage_item_id breaking_item;
    unsigned int breaking_metadata, breaking_damage;
    unsigned long blocks_broken, last_break_tick;
    maage_world_target last_break;
    unsigned long swing_tick;
    unsigned int swing_kind;
    double swing; /* Authoritative remaining six-tick arm swing, normalized 0..1. */
} maage_session_player_view;

typedef struct maage_session_snapshot {
    unsigned int protocol_version;
    unsigned long session_epoch, world_epoch;
    unsigned long tick, sequence;            /* Increasing within a session/world epoch. */
    unsigned long applied_command_sequence;  /* Acknowledges accepted or rejected FIFO actions. */
    unsigned long gameplay_command_sequence; /* Commands included in the last completed gameplay step. */
    /* Next owner boundary on the local monotonic clock. A remote adapter must
     * translate it or use -1; prediction never extrapolates beyond one step. */
    double next_tick_at;
    int paused; /* Integrated owner pause; presentation may continue. */
    maage_session_player_view player;
    maage_environment_state environment;
    int has_observation;
    maage_world_observation observation;
    maage_world_stats world_stats;
} maage_session_snapshot;

/* Complete interest-set publication. Absence removes an entity. Independent of
 * the player snapshot; immutable array ownership transfers through the local
 * mailbox. Remote transport can fragment this same revision atomically. */
typedef struct maage_session_entities {
    unsigned int protocol_version;
    unsigned long session_epoch, world_epoch, tick, sequence;
    maage_entity_view *entities;
    size_t count;
    double offered_at; /* Optional local diagnostic clock, otherwise -1. */
} maage_session_entities;

typedef enum maage_session_event_kind {
    MAAGE_EVENT_SOUND = 1,
    MAAGE_EVENT_STATUS = 2,
    MAAGE_EVENT_DISCONNECTED = 4,
    MAAGE_EVENT_PICKUP = 5,
    MAAGE_EVENT_BLOCK_ACTION = 6,
    MAAGE_EVENT_PLAYER_MOTION = 7
} maage_session_event_kind;

typedef struct maage_session_event {
    unsigned int protocol_version;
    unsigned int kind;
    unsigned long session_epoch, world_epoch;
    unsigned long tick, sequence;
    double emitted_at;                  /* Local monotonic seconds; remote clocks need translation. */
    maage_sound_event sound;            /* SOUND/PICKUP. */
    maage_session_player_motion motion; /* PLAYER_MOTION, one immutable owner-step observation. */
    unsigned long prediction;
    int accepted;
    maage_block_position block;
    unsigned long column_revisions[9];
    unsigned int pickup_count;
    maage_entity_pickup pickups[32]; /* PICKUP: one bounded batch per owner step. */
    maage_status status;             /* STATUS or DISCONNECTED outcome. */
    int has_trace;
    maage_status_trace trace; /* Copied by value; frame file names are static. */
} maage_session_event;

#ifdef __cplusplus
}
#endif
#endif
