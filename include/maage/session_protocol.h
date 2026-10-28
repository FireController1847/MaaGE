#ifndef MAAGE_SESSION_PROTOCOL_H
#define MAAGE_SESSION_PROTOCOL_H

#include "maage/environment.h"
#include "maage/gameplay.h"
#include "maage/entities.h"
#include "maage/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Numeric local-session contract. Future network encoding must serialize fields
 * explicitly, not memcpy C layouts (including unsigned long and padding). */
#define MAAGE_SESSION_PROTOCOL_VERSION 8U

typedef enum maage_session_command_kind {
    MAAGE_CMD_INPUT = 1, MAAGE_CMD_HOTBAR = 2,
    MAAGE_CMD_CREATIVE_PICK = 4, MAAGE_CMD_GAME_MODE = 5,
    MAAGE_CMD_RESPAWN = 6, MAAGE_CMD_PAUSE = 7, MAAGE_CMD_VIEW = 8,
    MAAGE_CMD_DISCONNECT = 9, MAAGE_CMD_DROP = 10,
    MAAGE_CMD_CONTAINER_OPEN = 11, MAAGE_CMD_CONTAINER_CLOSE = 12,
    MAAGE_CMD_CONTAINER_SLOT = 13, MAAGE_CMD_BREAK_PREVIEW = 14
} maage_session_command_kind;

typedef enum maage_session_sound_id {
    MAAGE_SOUND_FOOTSTEP = 1, MAAGE_SOUND_BLOCK_BREAK = 2,
    MAAGE_SOUND_BLOCK_PLACE = 3, MAAGE_SOUND_PLAYER_JUMP = 4,
    MAAGE_SOUND_PLAYER_LAND = 5, MAAGE_SOUND_PLAYER_HURT = 6,
    MAAGE_SOUND_PLAYER_SWIM = 7, MAAGE_SOUND_INVENTORY_CLICK = 8,
    MAAGE_SOUND_GUI_CLICK = 9, MAAGE_SOUND_CAVE_AMBIENT = 10,
    MAAGE_SOUND_RAIN = 11, MAAGE_SOUND_THUNDER = 12,
    MAAGE_SOUND_WATER = 13, MAAGE_SOUND_LAVA = 14,
    MAAGE_SOUND_BLOCK_DIG = 15, MAAGE_SOUND_PLAYER_SPLASH = 16,
    MAAGE_SOUND_ITEM_PICKUP = 17, MAAGE_SOUND_PLAYER_FALL_SMALL = 18,
    MAAGE_SOUND_PLAYER_FALL_BIG = 19, MAAGE_SOUND_PLAYER_DEATH = 20,
    MAAGE_SOUND_ITEM_BREAK = 21
} maage_session_sound_id;
#define MAAGE_SOUND_MAX MAAGE_SOUND_ITEM_BREAK

typedef enum maage_session_material_id {
    MAAGE_MATERIAL_DEFAULT = 0, MAAGE_MATERIAL_STONE = 1,
    MAAGE_MATERIAL_GRASS = 2, MAAGE_MATERIAL_DIRT = 3,
    MAAGE_MATERIAL_WOOD = 4, MAAGE_MATERIAL_SAND = 5,
    MAAGE_MATERIAL_GRAVEL = 6, MAAGE_MATERIAL_SNOW = 7,
    MAAGE_MATERIAL_METAL = 8, MAAGE_MATERIAL_GLASS = 9,
    MAAGE_MATERIAL_WATER = 10, MAAGE_MATERIAL_LAVA = 11,
    MAAGE_MATERIAL_FOLIAGE = 12, MAAGE_MATERIAL_CLOTH = 13,
    MAAGE_MATERIAL_LADDER = 14, MAAGE_MATERIAL_ANVIL = 15
} maage_session_material_id;
#define MAAGE_MATERIAL_MAX MAAGE_MATERIAL_ANVIL

typedef struct maage_session_command {
    unsigned int protocol_version;
    unsigned int kind;
    unsigned long sequence; /* Nonzero and increasing within the session. */
    maage_gameplay_intent intent;
    unsigned int hotbar_slot;
    unsigned int inventory_slot;
    /* OPEN requests the player container only; tables open from authoritative
     * use/raycast. CLOSE/SLOT require the current nonzero container identity.
     * RESULT also requires the published revision. No Client stacks/recipes. */
    unsigned long container_identity, container_revision;
    unsigned int slot_group, container_slot, slot_action;
    unsigned long prediction;
    maage_world_target predicted_target; /* Request only; never a claimed mutation. */
    int secondary_click;
    maage_item_id item_id;
    unsigned int item_metadata;
    unsigned int game_mode; /* Request; server validates and applies rules. */
    int paused; /* Request, not a write into authoritative simulation. */
    int render_radius;
    unsigned int leaves_mode;
    double view_yaw;
} maage_session_command;

typedef struct maage_sound_event {
    unsigned int event_id;
    unsigned int material;
    unsigned long tick;
    unsigned long sequence;
    double position[3];
    double intensity;
    double pitch;
} maage_sound_event;

typedef struct maage_session_player_view {
    maage_player player;
    double yaw, pitch;
    maage_inventory inventory;
    maage_crafting_container crafting; /* Copied authoritative temporary state. */
    unsigned int game_mode, health, hunger;
    double saturation;
    int flying, sneaking;
    int has_target, breaking_active, submerged;
    double reach;
    unsigned int break_delay, attack_miss_delay;
    maage_world_neighborhood mining;
    maage_world_target target, breaking;
    double break_progress;
    double swing; /* Authoritative remaining six-tick arm swing, normalized 0..1. */
} maage_session_player_view;

/* Owner-sampled soundscape candidates. No Client reads of live World data. */
typedef struct maage_session_audio_observation {
    int rain_available, rain_above;
    double rain_position[3];
    int flowing_water_available, lava_available;
    unsigned int water_samples, lava_samples; /* Matching trials among 1000 display samples. */
    double water_position[3], lava_position[3];
} maage_session_audio_observation;

typedef struct maage_session_snapshot {
    unsigned int protocol_version;
    unsigned long session_epoch, world_epoch;
    unsigned long tick, sequence; /* Increasing within a session/world epoch. */
    unsigned long applied_command_sequence; /* Acknowledges accepted or rejected FIFO actions. */
    int paused; /* Integrated owner pause; presentation may continue. */
    maage_session_player_view player;
    maage_environment_state environment;
    int has_observation;
    maage_world_observation observation;
    maage_session_audio_observation audio_observation;
    maage_world_stats world_stats;
} maage_session_snapshot;

/* Complete interest-set publication. Absence removes an entity. Independent of
 * the player snapshot; immutable array ownership transfers through the local
 * mailbox. Remote transport can fragment this same revision atomically. */
typedef struct maage_session_entities {
    unsigned int protocol_version;
    unsigned long session_epoch, world_epoch, tick, sequence;
    maage_entity *entities;
    size_t count;
} maage_session_entities;

/* Latest collision publication replaces its predecessor only when the sampled
 * cell or footprint column revisions change, independently of camera mode. */
typedef struct maage_session_collision {
    unsigned int protocol_version;
    unsigned long session_epoch, world_epoch, sequence;
    maage_world_camera_sample sample;
} maage_session_collision;

typedef enum maage_session_event_kind {
    MAAGE_EVENT_SOUND = 1, MAAGE_EVENT_STATUS = 2,
    MAAGE_EVENT_MESH_REMOVE = 3, MAAGE_EVENT_DISCONNECTED = 4,
    MAAGE_EVENT_PICKUP = 5, MAAGE_EVENT_BLOCK_ACTION = 6
} maage_session_event_kind;

typedef struct maage_session_event {
    unsigned int protocol_version;
    unsigned int kind;
    unsigned long session_epoch, world_epoch;
    unsigned long tick, sequence;
    double emitted_at; /* Local monotonic seconds; remote clocks need translation. */
    maage_sound_event sound; /* SOUND/PICKUP. */
    unsigned long prediction;
    int accepted;
    maage_block_position block;
    unsigned long column_revisions[9];
    unsigned int pickup_count;
    maage_entity_pickup pickups[32]; /* PICKUP: one bounded batch per owner step. */
    maage_status status; /* STATUS or DISCONNECTED outcome. */
    int has_trace;
    maage_status_trace trace; /* Copied by value; frame file names are static. */
    int column_x, column_z; /* MESH_REMOVE column, without CPU mesh pointer. */
    unsigned long mesh_revision;
} maage_session_event;

#ifdef __cplusplus
}
#endif
#endif
