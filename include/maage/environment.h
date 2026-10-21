#ifndef MAAGE_ENVIRONMENT_H
#define MAAGE_ENVIRONMENT_H
#include "maage/world.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MAAGE_WORLD_DAY_TICKS 24000UL
enum { MAAGE_WEATHER_CLEAR = 0, MAAGE_WEATHER_RAIN = 1, MAAGE_WEATHER_THUNDER = 2 };
enum { MAAGE_BLOCK_UPDATE_FLUID = 1U };
enum { MAAGE_BLOCK_ENTITY_CONTAINER = 1UL, MAAGE_BLOCK_ENTITY_FURNACE = 2UL,
    MAAGE_BLOCK_ENTITY_SIGN = 3UL };
typedef struct maage_environment_state {
    unsigned long ticks_low, ticks_high, day_tick, day_low, day_high;
    unsigned long random, rain_ticks, thunder_ticks;
    unsigned int raining, thundering;
    double rain_intensity, thunder_intensity;
} maage_environment_state;
typedef struct maage_scheduled_update {
    unsigned int kind, cell; /* cell=(y*16+local_z)*16+local_x */
    unsigned long due_low, due_high;
} maage_scheduled_update;
typedef struct maage_block_entity {
    unsigned int cell;
    unsigned long type, version;
    unsigned char *data;
    size_t length;
} maage_block_entity;
typedef struct maage_environment maage_environment;

void maage_environment_default(maage_world_seed seed, maage_environment_state *state);
int maage_environment_state_valid(const maage_environment_state *state);
maage_environment *maage_environment_create(maage_world_seed seed,
    const maage_environment_state *state);
void maage_environment_destroy(maage_environment *environment);
/* Exactly one owner-thread 20 TPS phase. Consumes at most update_budget queued
 * updates through World mutation, without waiting for chunks, jobs or I/O. */
void maage_environment_tick(maage_environment *environment, maage_world *world,
    unsigned int update_budget);
void maage_environment_snapshot(const maage_environment *environment,
    maage_environment_state *state);
unsigned int maage_environment_weather(const maage_environment_state *state);
double maage_environment_sunlight(const maage_environment_state *state);
/* Owner mutation hook schedules this cell and its six neighbors when fluid
 * behavior could change. Missing columns are ignored; boundary work retries
 * in a single deduplicated pending entry rather than growing the queue. */
void maage_environment_changed(maage_environment *environment, maage_world *world,
    int x, int y, int z);

/* World provides these owner-thread, snapshot-safe queue operations. A due
 * update transfers out once; the fluid handler explicitly reschedules waits. */
int maage_world_schedule_update(maage_world *world, int x, int y, int z,
    unsigned int kind, unsigned long due_low, unsigned long due_high);
int maage_world_take_update(maage_world *world, unsigned long now_low,
    unsigned long now_high, int *column_x, int *column_z, maage_scheduled_update *update);
/* World exposes copied owner state; no scheduler/World lock or allocation. */
void maage_world_environment(const maage_world *world, maage_environment_state *state);
/* Owner-thread numeric block-entity foundation. Reads copy into caller storage;
 * length reports the required bytes, and short buffers fail without borrowing
 * a worker-visible payload. Set/remove use the same immutable publication path
 * as blocks; changing a block type removes its associated entity. */
int maage_world_block_entity_set(maage_world *world, int x, int y, int z,
    unsigned long type, unsigned long version, const void *data, size_t length);
int maage_world_block_entity_get(const maage_world *world, int x, int y, int z,
    unsigned long *type, unsigned long *version, void *data, size_t capacity, size_t *length);
int maage_world_block_entity_remove(maage_world *world, int x, int y, int z);
#ifdef __cplusplus
}
#endif
#endif
