#include "world_internal.h"
#include "maage/entities.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAAGE_ENVIRONMENT_WORD 4294967295UL
#define MAAGE_BLOCK_ENTITY_DATA_MAX 65536UL
struct maage_environment {
    maage_environment_state state;
};

static unsigned long maage_environment_random(maage_environment_state *state) {
    unsigned long value = state->random;
    value ^= (value << 13) & MAAGE_ENVIRONMENT_WORD;
    value ^= value >> 17;
    value ^= (value << 5) & MAAGE_ENVIRONMENT_WORD;
    state->random = value & MAAGE_ENVIRONMENT_WORD;
    return state->random;
}
static void maage_environment_increment(unsigned long *low, unsigned long *high) {
    *low = (*low + 1UL) & MAAGE_ENVIRONMENT_WORD;
    if (!*low) *high = (*high + 1UL) & MAAGE_ENVIRONMENT_WORD;
}
maage_status maage_environment_default(maage_world_seed seed, maage_environment_state *state) {
    if (!state) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 1);
    memset(state, 0, sizeof(*state));
    state->random = (seed.low ^ seed.high ^ 2654435761UL) & MAAGE_ENVIRONMENT_WORD;
    if (!state->random) state->random = 1;
    state->rain_ticks = 12000UL + maage_environment_random(state) % 168001UL;
    state->thunder_ticks = 12000UL + maage_environment_random(state) % 168001UL;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}
int maage_environment_state_valid(const maage_environment_state *state) {
    return state && state->ticks_low <= MAAGE_ENVIRONMENT_WORD && state->ticks_high <= MAAGE_ENVIRONMENT_WORD &&
           state->day_low <= MAAGE_ENVIRONMENT_WORD && state->day_high <= MAAGE_ENVIRONMENT_WORD &&
           state->day_tick < MAAGE_WORLD_DAY_TICKS && state->random && state->random <= MAAGE_ENVIRONMENT_WORD &&
           state->rain_ticks <= 180000UL && state->thunder_ticks <= 180000UL &&
           state->raining <= 1 && state->thundering <= 1 &&
           state->rain_intensity >= 0 && state->rain_intensity <= 1 &&
           state->thunder_intensity >= 0 && state->thunder_intensity <= 1;
}
maage_status maage_environment_create(maage_world_seed seed, const maage_environment_state *saved, maage_environment **output) {
    maage_environment *environment;
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 2);
    *output = NULL;
    if (saved && !maage_environment_state_valid(saved)) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0);
    environment = (maage_environment *) malloc(sizeof(*environment));
    if (!environment) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_CREATE, 0);
    if (saved) {
        environment->state = *saved;
    } else
        maage_environment_default(seed, &environment->state);
    *output = environment;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
}
maage_status maage_environment_destroy(maage_environment *environment) {
    if (!environment) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    free(environment);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
void maage_environment_snapshot(const maage_environment *environment, maage_environment_state *state) {
    if (environment && state) *state = environment->state;
}
unsigned int maage_environment_weather(const maage_environment_state *state) {
    if (!state || (!state->raining && state->rain_intensity <= 0)) return MAAGE_WEATHER_CLEAR;
    return state->thundering || state->thunder_intensity > 0 ? MAAGE_WEATHER_THUNDER : MAAGE_WEATHER_RAIN;
}
double maage_environment_celestial_angle(const maage_environment_state *state) {
    double phase, eased;
    if (!state) return 0;
    phase = (double) state->day_tick / MAAGE_WORLD_DAY_TICKS - 0.25;
    if (phase < 0) phase += 1;
    eased = (1.0 - cos(phase * 3.141592653589793)) * 0.5;
    return phase + (eased - phase) / 3.0;
}
double maage_environment_sunlight(const maage_environment_state *state) {
    double light;
    if (!state) return 1;
    light = cos(maage_environment_celestial_angle(state) * 6.283185307179586) * 2.0 + 0.2;
    if (light < 0) light = 0;
    if (light > 1) light = 1;
    return 0.2 + 0.8 * light * (1.0 - state->rain_intensity * 5.0 / 16.0) * (1.0 - state->thunder_intensity * state->rain_intensity * 5.0 / 16.0);
}
static int maage_update_before(const maage_scheduled_update *a, const maage_scheduled_update *b) {
    if (a->due_high != b->due_high) return a->due_high < b->due_high;
    if (a->due_low != b->due_low) return a->due_low < b->due_low;
    if (a->cell != b->cell) return a->cell < b->cell;
    return a->kind < b->kind;
}
static size_t maage_update_bucket(const maage_chunk *chunk, unsigned int kind, unsigned int cell, int *found) {
    size_t bucket = ((unsigned long) cell * 2654435761UL ^ (unsigned long) kind * 65599UL) % chunk->update_index_capacity;
    size_t tombstone = (size_t) - 1;
    const maage_update_index *entry;
    while (1) {
        entry = &chunk->update_index[bucket];
        if (!entry->heap_index) {
            *found = 0;
            return tombstone == (size_t) - 1 ? bucket : tombstone;
        }
        if (entry->heap_index == (size_t) - 1) {
            if (tombstone == (size_t) - 1) tombstone = bucket;
        } else if (entry->cell == cell && entry->kind == kind) {
            *found = 1;
            return bucket;
        }
        bucket = (bucket + 1) % chunk->update_index_capacity;
    }
}
static maage_status maage_update_rehash(maage_chunk *chunk, size_t capacity) {
    maage_update_index *index;
    size_t i, bucket;
    int found;
    if (capacity > (size_t) - 1 / sizeof(*index)) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_HASH, 0);
    index = (maage_update_index *) calloc(capacity, sizeof(*index));
    if (!index) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_HASH, 0);
    free(chunk->update_index);
    chunk->update_index = index;
    chunk->update_index_capacity = capacity;
    chunk->update_index_used = 0;
    for (i = 0; i < chunk->update_count; ++i) {
        bucket = maage_update_bucket(chunk, chunk->updates[i].kind, chunk->updates[i].cell, &found);
        index[bucket].kind = chunk->updates[i].kind;
        index[bucket].cell = chunk->updates[i].cell;
        index[bucket].heap_index = i + 1;
        ++chunk->update_index_used;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_HASH, 0);
}
static void maage_update_position(maage_chunk *chunk, size_t position) {
    int found;
    size_t bucket = maage_update_bucket(chunk, chunk->updates[position].kind, chunk->updates[position].cell, &found);
    chunk->update_index[bucket].heap_index = position + 1;
}
static void maage_update_swap(maage_chunk *chunk, size_t a, size_t b) {
    maage_scheduled_update temporary = chunk->updates[a];
    chunk->updates[a] = chunk->updates[b];
    chunk->updates[b] = temporary;
    maage_update_position(chunk, a);
    maage_update_position(chunk, b);
}
maage_status maage_chunk_schedule_update(maage_chunk *chunk, const maage_scheduled_update *update) {
    maage_scheduled_update *grown;
    size_t capacity, bucket, position, parent;
    int found;
    if (!chunk || !update || (update->kind != MAAGE_BLOCK_UPDATE_FLUID && update->kind != MAAGE_BLOCK_UPDATE_SUPPORT) || update->cell > 65535U || update->due_low > MAAGE_ENVIRONMENT_WORD || update->due_high > MAAGE_ENVIRONMENT_WORD) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SUBMIT, 0);
    found = 0;
    if (chunk->update_index_capacity) maage_update_bucket(chunk, update->kind, update->cell, &found);
    if (!found && (!chunk->update_index_capacity || chunk->update_index_used + 1 >= chunk->update_index_capacity / 2)) {
        capacity = chunk->update_index_capacity ? chunk->update_index_capacity : 16;
        if (chunk->update_count >= capacity / 4) {
            if (capacity > (size_t) - 1 / 2) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SUBMIT, 0);
            capacity *= 2;
        }
        if (maage_status_failed(maage_update_rehash(chunk, capacity))) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SUBMIT, 0);
    }
    bucket = maage_update_bucket(chunk, update->kind, update->cell, &found);
    if (found) {
        position = chunk->update_index[bucket].heap_index - 1;
        if (!maage_update_before(update, &chunk->updates[position])) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SUBMIT, 0);
        chunk->updates[position] = *update;
    } else {
        if (chunk->update_count == chunk->update_capacity) {
            capacity = chunk->update_capacity ? chunk->update_capacity * 2 : 8;
            if (capacity < chunk->update_capacity || capacity > (size_t) - 1 / sizeof(*grown)) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SUBMIT, 0);
            grown = (maage_scheduled_update *) realloc(chunk->updates, capacity * sizeof(*grown));
            if (!grown) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SUBMIT, 0);
            chunk->updates = grown;
            chunk->update_capacity = capacity;
        }
        position = chunk->update_count++;
        chunk->updates[position] = *update;
        if (!chunk->update_index[bucket].heap_index) ++chunk->update_index_used;
        chunk->update_index[bucket].kind = update->kind;
        chunk->update_index[bucket].cell = update->cell;
        chunk->update_index[bucket].heap_index = position + 1;
        if (update->kind == MAAGE_BLOCK_UPDATE_FLUID) ++chunk->fluid_update_count;
    }
    while (position) {
        parent = (position - 1) / 2;
        if (!maage_update_before(&chunk->updates[position], &chunk->updates[parent])) break;
        maage_update_swap(chunk, position, parent);
        position = parent;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SUBMIT, 0);
}
maage_status maage_chunk_take_update(maage_chunk *chunk, unsigned long low, unsigned long high, maage_scheduled_update *update) {
    size_t position = 0, child, bucket;
    int found;
    if (!chunk || !update) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_POLL, 0);
    if (!chunk->update_count) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_POLL, 0);
    if (chunk->updates[0].due_high > high || (chunk->updates[0].due_high == high && chunk->updates[0].due_low > low)) return maage_status_make(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_POLL, 0);
    *update = chunk->updates[0];
    bucket = maage_update_bucket(chunk, update->kind, update->cell, &found);
    chunk->update_index[bucket].heap_index = (size_t) - 1;
    --chunk->update_count;
    if (update->kind == MAAGE_BLOCK_UPDATE_FLUID) --chunk->fluid_update_count;
    if (!chunk->update_count) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_POLL, 0);
    chunk->updates[0] = chunk->updates[chunk->update_count];
    maage_update_position(chunk, 0);
    while (position < chunk->update_count / 2) {
        child = position * 2 + 1;
        if (child + 1 < chunk->update_count && maage_update_before(&chunk->updates[child + 1], &chunk->updates[child])) ++child;
        if (!maage_update_before(&chunk->updates[child], &chunk->updates[position])) break;
        maage_update_swap(chunk, position, child);
        position = child;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_POLL, 0);
}
maage_status maage_chunk_block_entity_remove(maage_chunk *chunk, unsigned int cell) {
    size_t i;
    if (!chunk) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_REMOVE, 0);
    for (i = 0; i < chunk->entity_count; ++i)
        if (chunk->entities[i].cell == cell) {
            free(chunk->entities[i].data);
            --chunk->entity_count;
            chunk->entities[i] = chunk->entities[chunk->entity_count];
            return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REMOVE, 0);
        }
    return maage_status_make(MAAGE_STATUS_NOOP, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_REMOVE, 0);
}
maage_status maage_chunk_block_entity_set(maage_chunk *chunk, unsigned int cell, unsigned long type, unsigned long version, const void *data, size_t length) {
    maage_block_entity *grown, *entity;
    unsigned char *copy = NULL;
    size_t i, capacity;
    if (!chunk || cell > 65535U || !type || type > MAAGE_ENVIRONMENT_WORD || !version || version > MAAGE_ENVIRONMENT_WORD || length > MAAGE_BLOCK_ENTITY_DATA_MAX || (length && !data)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
    if (length) {
        copy = (unsigned char *) malloc(length);
        if (!copy) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
        memcpy(copy, data, length);
    }
    for (i = 0; i < chunk->entity_count; ++i)
        if (chunk->entities[i].cell == cell) break;
    if (i == chunk->entity_count) {
        if (chunk->entity_count == chunk->entity_capacity) {
            capacity = chunk->entity_capacity ? chunk->entity_capacity * 2 : 4;
            if (capacity < chunk->entity_capacity || capacity > (size_t) - 1 / sizeof(*grown)) {
                free(copy);
                return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
            }
            grown = (maage_block_entity *) realloc(chunk->entities, capacity * sizeof(*grown));
            if (!grown) {
                free(copy);
                return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_SET, 0);
            }
            chunk->entities = grown;
            chunk->entity_capacity = capacity;
        }
        entity = &chunk->entities[chunk->entity_count++];
        memset(entity, 0, sizeof(*entity));
    } else
        entity = &chunk->entities[i];
    free(entity->data);
    entity->cell = cell;
    entity->type = type;
    entity->version = version;
    entity->data = copy;
    entity->length = length;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
maage_status maage_chunk_copy_environment(maage_chunk *destination, const maage_chunk *source) {
    size_t i, index_capacity;
    if (!destination || !source || destination->update_count || destination->entity_count) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
    destination->environment_initialized = source->environment_initialized;
    destination->updates_relative = source->updates_relative;
    destination->generation_stages = source->generation_stages;
    memcpy(destination->generation_features, source->generation_features, sizeof(source->generation_features));
    if (source->update_count) {
        if (source->update_count > (size_t) - 1 / sizeof(*destination->updates)) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
        destination->updates = (maage_scheduled_update *) malloc(source->update_count * sizeof(*destination->updates));
        if (!destination->updates) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
        memcpy(destination->updates, source->updates, source->update_count * sizeof(*destination->updates));
        destination->update_count = destination->update_capacity = source->update_count;
        destination->fluid_update_count = source->fluid_update_count;
        index_capacity = 16;
        while (source->update_count >= index_capacity / 2) {
            if (index_capacity > (size_t) - 1 / 2) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
            index_capacity *= 2;
        }
        if (maage_status_failed(maage_update_rehash(destination, index_capacity))) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
    }
    if (source->entity_count) {
        if (source->entity_count > (size_t) - 1 / sizeof(*destination->entities)) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
        destination->entities = (maage_block_entity *) calloc(source->entity_count, sizeof(*destination->entities));
        if (!destination->entities) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
        destination->entity_capacity = source->entity_count;
    }
    for (i = 0; i < source->entity_count; ++i) {
        const maage_block_entity *entity = &source->entities[i];
        maage_block_entity *copy = &destination->entities[destination->entity_count++];
        *copy = *entity;
        copy->data = NULL;
        if (entity->length) {
            copy->data = (unsigned char *) malloc(entity->length);
            if (!copy->data) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
            memcpy(copy->data, entity->data, entity->length);
        }
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_COPY, 0);
}
static void maage_chunk_clear_environment(maage_chunk *chunk) {
    size_t i;
    for (i = 0; i < chunk->entity_count; ++i)
        free(chunk->entities[i].data);
    free(chunk->entities);
    free(chunk->updates);
    free(chunk->update_index);
    chunk->entities = NULL;
    chunk->updates = NULL;
    chunk->update_index = NULL;
    chunk->entity_count = chunk->entity_capacity = 0;
    chunk->update_count = chunk->update_capacity = chunk->fluid_update_count = 0;
    chunk->update_index_capacity = chunk->update_index_used = 0;
}
maage_status maage_chunk_replace_environment(maage_chunk *destination, const maage_chunk *source) {
    maage_chunk temporary;
    maage_status result;
    int section;
    if (!destination || !source) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
    for (section = 0; section < 16; ++section)
        if (source->sections[section] && !destination->sections[section])
            return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_COPY, 0);
    memset(&temporary, 0, sizeof(temporary));
    result = maage_chunk_copy_environment(&temporary, source);
    if (maage_status_failed(result)) {
        maage_chunk_clear_environment(&temporary);
        return result;
    }
    maage_chunk_clear_environment(destination);
    destination->updates = temporary.updates;
    destination->update_count = temporary.update_count;
    destination->update_capacity = temporary.update_capacity;
    destination->fluid_update_count = temporary.fluid_update_count;
    destination->update_index = temporary.update_index;
    destination->update_index_capacity = temporary.update_index_capacity;
    destination->update_index_used = temporary.update_index_used;
    destination->entities = temporary.entities;
    destination->entity_count = temporary.entity_count;
    destination->entity_capacity = temporary.entity_capacity;
    destination->environment_initialized = temporary.environment_initialized;
    destination->updates_relative = temporary.updates_relative;
    destination->generation_stages = temporary.generation_stages;
    memcpy(destination->generation_features, temporary.generation_features, sizeof(temporary.generation_features));
    /* The owner already verified matching geometry epochs. Keep current leaf
     * flags along with queues/entities; only the worker's light bytes replace
     * the published lighting. Flag-only edits never rebuild GPU geometry. */
    for (section = 0; section < 16; ++section)
        if (source->sections[section])
            memcpy(destination->sections[section]->states, source->sections[section]->states, sizeof(destination->sections[section]->states));
    memcpy(destination->random_tick_counts, source->random_tick_counts, sizeof(destination->random_tick_counts));
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_COPY, 0);
}
static void maage_environment_due(const maage_environment_state *state, unsigned int delay, unsigned long *low, unsigned long *high) {
    *low = (state->ticks_low + delay) & MAAGE_ENVIRONMENT_WORD;
    *high = (state->ticks_high + (*low < state->ticks_low ? 1UL : 0UL)) & MAAGE_ENVIRONMENT_WORD;
}
static const maage_block_state_definition *maage_environment_fluid(const maage_world *world, int x, int y, int z) {
    const maage_block_state_definition *state;
    state = maage_block_registry_state(maage_world_registry(world), maage_world_get(world, x, y, z));
    return state && state->fluid_kind != MAAGE_FLUID_NONE ? state : NULL;
}
static int maage_environment_loaded(maage_world *world, int x, int y, int z) {
    return y >= 0 && y < MAAGE_WORLD_HEIGHT && x > -MAAGE_WORLD_LIMIT && x < MAAGE_WORLD_LIMIT && z > -MAAGE_WORLD_LIMIT && z < MAAGE_WORLD_LIMIT && maage_world_chunk_loaded(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z));
}
static maage_status maage_environment_schedule(maage_environment *environment, maage_world *world, int x, int y, int z, unsigned int delay) {
    unsigned long low, high;
    if (!maage_environment_loaded(world, x, y, z)) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SUBMIT, 0);
    maage_environment_due(&environment->state, delay, &low, &high);
    return maage_world_schedule_update(world, x, y, z, MAAGE_BLOCK_UPDATE_FLUID, low, high);
}
maage_status maage_environment_changed(maage_environment *environment, maage_world *world, int x, int y, int z) {
    static const int directions[7][3] = {{0, 0, 0}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    const maage_block_state_definition *state;
    unsigned int i;
    int nx, ny, nz;
    maage_status result;
    if (!environment || !world) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
    for (i = 0; i < 7; ++i) {
        nx = x + directions[i][0];
        ny = y + directions[i][1];
        nz = z + directions[i][2];
        if (!maage_environment_loaded(world, nx, ny, nz)) continue;
        state = maage_environment_fluid(world, nx, ny, nz);
        if (state) {
            result = maage_environment_schedule(environment, world, nx, ny, nz, state->fluid_kind == MAAGE_FLUID_LAVA ? 30U : 5U);
            if (maage_status_failed(result)) return result;
        }
    }
    return maage_world_failed(world) ? maage_world_status(world) : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}
/* Routing observes material barriers, independently of body collision shapes.
 * Unavailable terrain stops the search and retains one deduplicated retry. */
static int maage_environment_barrier(const maage_world *world, int x, int y, int z, unsigned int kind, int exclude_source, int *waiting) {
    const maage_block_state_definition *state;
    const maage_block_definition *block;
    if (y < 0 || y >= MAAGE_WORLD_HEIGHT || x <= -MAAGE_WORLD_LIMIT || x >= MAAGE_WORLD_LIMIT || z <= -MAAGE_WORLD_LIMIT || z >= MAAGE_WORLD_LIMIT) return 1;
    if (!maage_world_chunk_loaded(world, maage_world_chunk_coordinate(x), maage_world_chunk_coordinate(z))) {
        *waiting = 1;
        return 1;
    }
    state = maage_block_registry_state(maage_world_registry(world), maage_world_get(world, x, y, z));
    block = state ? maage_block_registry_block(maage_world_registry(world), state->block_id) : NULL;
    return !block || block->material_solid || (!state->fluid_kind && !block->replaceable) || (exclude_source && state->fluid_kind == kind && !state->fluid_level);
}

/* A breadth-first local footprint ranks outlets by distance to a drop. The
 * five-cell reach is a fluid rule, not a World/cache/queue capacity limit. */
static unsigned int maage_environment_outlet(const maage_world *world, int x, int y, int z, unsigned int kind, unsigned int direction, int *waiting) {
    static const int dx[4] = {-1, 1, 0, 0}, dz[4] = {0, 0, -1, 1};
    unsigned char visited[11][11];
    int qx[121], qz[121];
    unsigned int distance[121], head = 0, tail = 0, side, depth;
    int nx, nz, cx, cz;
    memset(visited, 0, sizeof(visited));
    nx = dx[direction];
    nz = dz[direction];
    if (maage_environment_barrier(world, x + nx, y, z + nz, kind, 1, waiting)) return 6;
    qx[tail] = nx;
    qz[tail] = nz;
    distance[tail++] = 0;
    visited[nz + 5][nx + 5] = 1;
    visited[5][5] = 1;
    while (head < tail) {
        cx = qx[head];
        cz = qz[head];
        depth = distance[head++];
        if (!maage_environment_barrier(world, x + cx, y - 1, z + cz, kind, 0, waiting)) return depth;
        if (depth == 4) continue;
        for (side = 0; side < 4; ++side) {
            nx = cx + dx[side];
            nz = cz + dz[side];
            if (visited[nz + 5][nx + 5]) continue;
            visited[nz + 5][nx + 5] = 1;
            if (maage_environment_barrier(world, x + nx, y, z + nz, kind, 1, waiting)) continue;
            qx[tail] = nx;
            qz[tail] = nz;
            distance[tail++] = depth + 1;
        }
    }
    return 6;
}

static maage_status maage_environment_flow(maage_environment *environment, maage_world *world, const maage_block_state_definition *source, int x, int y, int z, unsigned int level, int downward, int *flowed) {
    const maage_block_state_definition *target;
    const maage_block_definition *block;
    const maage_item_registry *items = maage_world_item_registry(world);
    maage_entities *entities = maage_world_entities(world);
    maage_item_drops drops;
    maage_block_state_id old, state;
    unsigned long random = 0;
    unsigned int i;
    double position[3], velocity[3] = {0, 0.2, 0};
    maage_status result;
    *flowed = 0;
    if (!maage_environment_loaded(world, x, y, z)) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    old = maage_world_get(world, x, y, z);
    target = maage_block_registry_state(maage_world_registry(world), old);
    block = target ? maage_block_registry_block(maage_world_registry(world), target->block_id) : NULL;
    if (!block || !target) return maage_status_make(MAAGE_STATUS_INVARIANT_VIOLATION, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_UPDATE, 0);
    if (target->fluid_kind == source->fluid_kind || target->fluid_kind == MAAGE_FLUID_LAVA) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    if (target->fluid_kind == MAAGE_FLUID_WATER) {
        if (source->fluid_kind != MAAGE_FLUID_LAVA) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
        if (downward) {
            *flowed = 1;
            return maage_world_set(world, x, y, z, old, MAAGE_STATE_STONE);
        }
    }
    if (block->material_solid || !block->replaceable) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    memset(&drops, 0, sizeof(drops));
    if (source->fluid_kind == MAAGE_FLUID_WATER && target->block_id != MAAGE_BLOCK_AIR) {
        random = maage_entities_random(entities);
        result = maage_item_generate_drops(maage_world_registry(world), items, old, NULL, &random, &drops);
        if (maage_status_failed(result)) return result;
        result = maage_entities_reserve(entities, drops.count);
        if (maage_status_failed(result)) return result;
    }
    state = source->fluid_states[level];
    result = maage_world_set(world, x, y, z, old, state);
    if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
    *flowed = 1;
    for (i = 0; i < drops.count; ++i) {
        position[0] = x + 0.5;
        position[1] = y + 0.25;
        position[2] = z + 0.5;
        result = maage_entities_spawn(entities, items, &drops.stacks[i], position, velocity, 10);
        if (maage_status_failed(result)) return result;
    }
    if (drops.count) {
        result = maage_entities_set_random(entities, random);
        if (maage_status_failed(result)) return result;
    }
    return maage_environment_schedule(environment, world, x, y, z, source->fluid_kind == MAAGE_FLUID_LAVA ? 30U : 5U);
}
static maage_status maage_environment_fluid_update(maage_environment *environment, maage_world *world, int x, int y, int z) {
    static const int directions[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    const maage_block_state_definition *state, *neighbor, *above, *under;
    const maage_block_definition *below;
    maage_block_state_id old, replacement;
    unsigned int i, level, minimum = 8, sources = 0, delay, next, costs[4], best = 6;
    int waiting = 0, nx, nz, flowed, blocked;
    maage_status result;
    state = maage_environment_fluid(world, x, y, z);
    if (!state) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    old = state->id;
    level = state->fluid_level;
    delay = state->fluid_kind == MAAGE_FLUID_LAVA ? 30U : 5U;
    for (i = 0; i < 4; ++i) {
        nx = x + directions[i][0];
        nz = z + directions[i][1];
        if (!maage_environment_loaded(world, nx, y, nz)) {
            if (nx > -MAAGE_WORLD_LIMIT && nx < MAAGE_WORLD_LIMIT && nz > -MAAGE_WORLD_LIMIT && nz < MAAGE_WORLD_LIMIT) waiting = 1;
            continue;
        }
        neighbor = maage_environment_fluid(world, nx, y, nz);
        if (!neighbor) continue;
        if (state->fluid_kind == MAAGE_FLUID_LAVA && neighbor->fluid_kind == MAAGE_FLUID_WATER && level <= 4) return maage_world_set(world, x, y, z, old, level == 0 ? MAAGE_STATE_OBSIDIAN : MAAGE_STATE_COBBLESTONE);
        if (neighbor->fluid_kind != state->fluid_kind) continue;
        if (neighbor->fluid_level == 0) ++sources;
        next = neighbor->fluid_level >= 8 ? 0 : neighbor->fluid_level;
        if (next < minimum) minimum = next;
    }
    above = y + 1 < MAAGE_WORLD_HEIGHT ? maage_environment_fluid(world, x, y + 1, z) : NULL;
    if (state->fluid_kind == MAAGE_FLUID_LAVA && above && above->fluid_kind == MAAGE_FLUID_WATER && level <= 4) return maage_world_set(world, x, y, z, old, level == 0 ? MAAGE_STATE_OBSIDIAN : MAAGE_STATE_COBBLESTONE);
    if (level) {
        if (waiting) return maage_environment_schedule(environment, world, x, y, z, delay);
        next = minimum + (state->fluid_kind == MAAGE_FLUID_LAVA ? 2U : 1U);
        if (above && above->fluid_kind == state->fluid_kind) next = 8U + (above->fluid_level & 7U);
        /* Supporting sources regenerate even with water above. Material
         * support is independent of opaque/full-cube collision geometry. */
        if (state->fluid_kind == MAAGE_FLUID_WATER && sources >= 2) {
            under = maage_block_registry_state(maage_world_registry(world), maage_world_get(world, x, y - 1, z));
            below = under ? maage_block_registry_block(maage_world_registry(world), under->block_id) : NULL;
            if ((below && below->material_solid) || (under && under->fluid_kind == MAAGE_FLUID_WATER && !under->fluid_level)) next = 0;
        }
        if (next >= 8 && !(above && above->fluid_kind == state->fluid_kind)) return maage_world_set(world, x, y, z, old, MAAGE_STATE_AIR);
        if (next != level) {
            replacement = state->fluid_states[next];
            result = maage_world_set(world, x, y, z, old, replacement);
            if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) return result;
            state = maage_block_registry_state(maage_world_registry(world), replacement);
            level = next;
        }
    }
    if (waiting) {
        result = maage_environment_schedule(environment, world, x, y, z, delay);
        if (maage_status_failed(result)) return result;
    }
    if (y > 0) {
        result = maage_environment_flow(environment, world, state, x, y - 1, z, 8U + (level & 7U), 1, &flowed);
        if (maage_status_failed(result) || flowed) return result;
    }
    blocked = maage_environment_barrier(world, x, y - 1, z, state->fluid_kind, 0, &waiting);
    if (level && !blocked) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    next = level >= 8 ? 1U : level + (state->fluid_kind == MAAGE_FLUID_LAVA ? 2U : 1U);
    if (next >= 8) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_UPDATE, 0);
    for (i = 0; i < 4; ++i) {
        costs[i] = maage_environment_outlet(world, x, y, z, state->fluid_kind, i, &waiting);
        if (costs[i] < best) best = costs[i];
    }
    if (waiting) return maage_environment_schedule(environment, world, x, y, z, delay);
    for (i = 0; i < 4; ++i) {
        if (costs[i] != best) continue;
        result = maage_environment_flow(environment, world, state, x + directions[i][0], y, z + directions[i][1], next, 0, &flowed);
        if (maage_status_failed(result)) return result;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_UPDATE, 0);
}
maage_status maage_chunk_environment_initialize(maage_chunk *chunk, const maage_block_registry *blocks, unsigned long now_low, unsigned long now_high) {
    static const int directions[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    const maage_block_state_definition *state, *neighbor;
    const maage_block_definition *block;
    maage_scheduled_update update;
    size_t i;
    unsigned int side;
    int x, y, z, nx, ny, nz, exposed;
    maage_status result;
    if (!chunk || !blocks) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_INITIALIZE, 0);
    if (chunk->updates_relative) {
        for (i = 0; i < chunk->update_count; ++i) {
            unsigned long old = chunk->updates[i].due_low;
            chunk->updates[i].due_low = (old + now_low) & MAAGE_ENVIRONMENT_WORD;
            chunk->updates[i].due_high = (chunk->updates[i].due_high + now_high + (chunk->updates[i].due_low < old ? 1UL : 0UL)) & MAAGE_ENVIRONMENT_WORD;
        }
        chunk->updates_relative = 0;
    }
    for (y = 0; y < MAAGE_WORLD_HEIGHT; ++y)
        for (z = 0; z < 16; ++z)
            for (x = 0; x < 16; ++x) {
                state = maage_block_registry_state(blocks, maage_chunk_get(chunk, x, y, z));
                if (!state || !state->fluid_kind) continue;
                exposed = state->fluid_level != 0;
                for (side = 0; side < 6 && !exposed; ++side) {
                    nx = x + directions[side][0];
                    ny = y + directions[side][1];
                    nz = z + directions[side][2];
                    if (nx < 0 || nx >= 16 || nz < 0 || nz >= 16) {
                        exposed = 1;
                        break;
                    }
                    if (ny < 0 || ny >= MAAGE_WORLD_HEIGHT) continue;
                    neighbor = maage_block_registry_state(blocks, maage_chunk_get(chunk, nx, ny, nz));
                    block = neighbor ? maage_block_registry_block(blocks, neighbor->block_id) : NULL;
                    if (neighbor && ((neighbor->fluid_kind && neighbor->fluid_kind != state->fluid_kind) || (!neighbor->fluid_kind && block && block->replaceable && !block->material_solid))) exposed = 1;
                }
                if (!exposed) continue;
                update.kind = MAAGE_BLOCK_UPDATE_FLUID;
                update.cell = (unsigned int) ((y * 16 + z) * 16 + x);
                update.due_low = (now_low + (state->fluid_kind == MAAGE_FLUID_LAVA ? 30UL : 5UL)) & MAAGE_ENVIRONMENT_WORD;
                update.due_high = (now_high + (update.due_low < now_low ? 1UL : 0UL)) & MAAGE_ENVIRONMENT_WORD;
                result = maage_chunk_schedule_update(chunk, &update);
                if (maage_status_failed(result)) return result;
            }
    chunk->environment_initialized = 1;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}
maage_status maage_environment_tick(maage_environment *environment, maage_world *world, unsigned int budget) {
    maage_environment_state *state;
    maage_scheduled_update update;
    unsigned int count;
    int x, z;
    maage_status result;
    if (!environment || !world) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_TICK, 0);
    state = &environment->state;
    maage_environment_increment(&state->ticks_low, &state->ticks_high);
    if (++state->day_tick == MAAGE_WORLD_DAY_TICKS) {
        state->day_tick = 0;
        maage_environment_increment(&state->day_low, &state->day_high);
    }
    if (state->rain_ticks) --state->rain_ticks;
    if (!state->rain_ticks) {
        state->raining = !state->raining;
        state->rain_ticks = state->raining ? 12000UL + maage_environment_random(state) % 12001UL : 12000UL + maage_environment_random(state) % 168001UL;
    }
    if (state->thunder_ticks) --state->thunder_ticks;
    if (!state->thunder_ticks) {
        state->thundering = !state->thundering;
        state->thunder_ticks = state->thundering ? 3600UL + maage_environment_random(state) % 12001UL : 12000UL + maage_environment_random(state) % 168001UL;
    }
    state->rain_intensity += state->raining ? 0.01 : -0.01;
    /* Rain weights the observed thunder strength at presentation time. Its
     * independent transition must not be faded a second time when rain starts. */
    state->thunder_intensity += state->thundering ? 0.01 : -0.01;
    if (state->rain_intensity < 0) state->rain_intensity = 0;
    if (state->rain_intensity > 1) state->rain_intensity = 1;
    if (state->thunder_intensity < 0) state->thunder_intensity = 0;
    if (state->thunder_intensity > 1) state->thunder_intensity = 1;
    for (count = 0; count < budget; ++count) {
        result = maage_world_take_update(world, state->ticks_low, state->ticks_high, &x, &z, &update);
        if (maage_status_failed(result)) return result;
        if (maage_status_code(result) != MAAGE_STATUS_SUCCESS) break;
        if (update.kind == MAAGE_BLOCK_UPDATE_SUPPORT) result = maage_support_update(world, x * 16 + (int) (update.cell % 16), (int) (update.cell / 256), z * 16 + (int) ((update.cell / 16) % 16));
        else
            result = maage_environment_fluid_update(environment, world, x * 16 + (int) (update.cell % 16), (int) (update.cell / 256), z * 16 + (int) ((update.cell / 16) % 16));
        if (maage_status_failed(result)) return result;
    }
    return maage_world_failed(world) ? maage_world_status(world) : maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_TICK, 0);
}
