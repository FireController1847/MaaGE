#ifndef MAAGE_SOUNDS_H
#define MAAGE_SOUNDS_H
#include "maage/content_ids.h"
#include "maage/status.h"

/* Engine occurrence hooks are not registered sound identities. Modules bind
 * these mechanisms to their own numeric events, and may emit any other event
 * directly without extending this list. Zero is no hook/resource/material. */
typedef enum maage_sound_action {
    MAAGE_SOUND_FOOTSTEP = 1,
    MAAGE_SOUND_BLOCK_BREAK,
    MAAGE_SOUND_BLOCK_PLACE,
    MAAGE_SOUND_PLAYER_JUMP,
    MAAGE_SOUND_PLAYER_LAND,
    MAAGE_SOUND_PLAYER_HURT,
    MAAGE_SOUND_PLAYER_SWIM,
    MAAGE_SOUND_INVENTORY_CLICK,
    MAAGE_SOUND_GUI_CLICK,
    MAAGE_SOUND_CAVE_AMBIENT,
    MAAGE_SOUND_RAIN,
    MAAGE_SOUND_THUNDER,
    MAAGE_SOUND_WATER,
    MAAGE_SOUND_LAVA,
    MAAGE_SOUND_BLOCK_DIG,
    MAAGE_SOUND_PLAYER_SPLASH,
    MAAGE_SOUND_ITEM_PICKUP,
    MAAGE_SOUND_PLAYER_FALL_SMALL,
    MAAGE_SOUND_PLAYER_FALL_BIG,
    MAAGE_SOUND_PLAYER_DEATH,
    MAAGE_SOUND_ITEM_BREAK,
    MAAGE_SOUND_EAT,
    MAAGE_SOUND_BURP,
    MAAGE_SOUND_ACTION_COUNT
} maage_sound_action;
enum {
    MAAGE_SOUND_BUS_BLOCKS = 0,
    MAAGE_SOUND_BUS_PLAYERS,
    MAAGE_SOUND_BUS_UI,
    MAAGE_SOUND_BUS_AMBIENT,
    MAAGE_SOUND_BUS_WEATHER,
    MAAGE_SOUND_BUS_COUNT
};
typedef unsigned long maage_sound_id;
typedef unsigned long maage_sound_resource_id;
typedef unsigned long maage_sound_material_id;
typedef unsigned long maage_sound_music_id;
typedef struct maage_sound_registry maage_sound_registry;
struct maage_world;
struct maage_environment_state;

typedef struct maage_sound_resource_definition {
    maage_sound_resource_id id;
    const char *path; /* Copied normalized relative resource path; consumed at binding only. */
    int streamed;
} maage_sound_resource_definition;
typedef struct maage_sound_definition {
    maage_sound_id id;
    const maage_sound_resource_id *resources;
    size_t resource_count;
    unsigned int bus;
    int positional;
    float gain, pitch, distance;
} maage_sound_definition;
typedef struct maage_sound_material_definition {
    maage_sound_material_id id;
    maage_sound_id actions[MAAGE_SOUND_ACTION_COUNT];
} maage_sound_material_definition;
typedef struct maage_sound_music_definition {
    maage_sound_music_id id;
    const maage_sound_resource_id *resources;
    size_t resource_count;
    double minimum_delay, maximum_delay;
    double initial_delay, retry_delay, unavailable_delay;
} maage_sound_music_definition;
typedef struct maage_sound_scene_query {
    const struct maage_world *visual; /* Client-owned replica only, never authoritative state. */
    const struct maage_environment_state *environment;
    const double *listener;
    double now;
    unsigned long tick;
    unsigned int game_mode;
    int has_world;
} maage_sound_scene_query;
typedef struct maage_sound_ambient_sink {
    maage_status (*play)(void *, maage_sound_id, unsigned int, maage_sound_material_id, const double[3], double, double, maage_status_trace *);
    void *context;
} maage_sound_ambient_sink;
typedef struct maage_sound_ambient_definition {
    size_t state_bytes; /* Per-scene zeroed state, never shared between Clients. */
    double interval;
    maage_status (*update)(const void *, void *, const maage_sound_scene_query *, const maage_sound_ambient_sink *, maage_status_trace *);
    const void *context;
} maage_sound_ambient_definition;
typedef struct maage_sound_bindings {
    maage_sound_id actions[MAAGE_SOUND_ACTION_COUNT];
    maage_sound_music_id (*music)(const void *, const maage_sound_scene_query *);
    const void *context;
} maage_sound_bindings;
/* All names/paths, resource lists and descriptors are copied. Module callback
 * code and immutable context outlive the registry, Worlds and playback scenes.
 * IDs are assigned from one in distinct domains; no playback-capacity limit
 * constrains registration. Finalized registries are immutable/headless-safe.
 * Resource acquisition, decoding, audio handles and scene state are Client-owned. */
maage_status maage_sound_registry_create(maage_sound_registry **);
maage_status maage_sound_registry_destroy(maage_sound_registry *);
maage_status maage_sound_resource_register(maage_sound_registry *, const char *, maage_sound_resource_definition *);
maage_status maage_sound_register(maage_sound_registry *, const char *, maage_sound_definition *);
maage_status maage_sound_material_register(maage_sound_registry *, const char *, maage_sound_material_definition *);
maage_status maage_sound_music_register(maage_sound_registry *, const char *, maage_sound_music_definition *);
maage_status maage_sound_ambient_register(maage_sound_registry *, const char *, const maage_sound_ambient_definition *);
maage_status maage_sound_bind(maage_sound_registry *, const maage_sound_bindings *);
maage_status maage_sound_finalize(maage_sound_registry *);
int maage_sound_is_finalized(const maage_sound_registry *);
size_t maage_sound_count(const maage_sound_registry *);
size_t maage_sound_resource_count(const maage_sound_registry *);
size_t maage_sound_material_count(const maage_sound_registry *);
size_t maage_sound_music_count(const maage_sound_registry *);
size_t maage_sound_ambient_count(const maage_sound_registry *);
const maage_sound_definition *maage_sound_get(const maage_sound_registry *, maage_sound_id);
const maage_sound_resource_definition *maage_sound_resource_get(const maage_sound_registry *, maage_sound_resource_id);
const maage_sound_material_definition *maage_sound_material_get(const maage_sound_registry *, maage_sound_material_id);
const maage_sound_music_definition *maage_sound_music_get(const maage_sound_registry *, maage_sound_music_id);
const maage_sound_ambient_definition *maage_sound_ambient_at(const maage_sound_registry *, size_t);
const maage_sound_bindings *maage_sound_get_bindings(const maage_sound_registry *);
const char *maage_sound_name(const maage_sound_registry *, maage_sound_id);
maage_status maage_sound_resolve(const maage_sound_registry *, const char *, maage_sound_id *);
maage_status maage_sound_resource_resolve(const maage_sound_registry *, const char *, maage_sound_resource_id *);
maage_status maage_sound_material_resolve(const maage_sound_registry *, const char *, maage_sound_material_id *);
maage_status maage_sound_music_resolve(const maage_sound_registry *, const char *, maage_sound_music_id *);
/* Numeric O(1) occurrence dispatch; material zero uses explicit global bindings.
 * A zero selection is intentional silence. Unknown nonzero material IDs fail. */
maage_status maage_sound_select(const maage_sound_registry *, unsigned int, maage_sound_material_id, maage_sound_id *);
#endif
