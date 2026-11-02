#ifndef MAAGE_CLIENT_CONTROLS_H
#define MAAGE_CLIENT_CONTROLS_H
#include "maage/input.h"

typedef enum maage_control_category {
    MAAGE_CONTROL_GAMEPLAY,
    MAAGE_CONTROL_INVENTORY,
    MAAGE_CONTROL_MISC,
    MAAGE_CONTROL_MOVEMENT,
    MAAGE_CONTROL_MULTIPLAYER,
    MAAGE_CONTROL_INTERFACE,
    MAAGE_CONTROL_CATEGORY_COUNT
} maage_control_category;
typedef struct maage_control {
    char *identity, *name;
    maage_control_category category;
    unsigned int binding, default_binding;
} maage_control;
typedef struct maage_controls {
    maage_control *actions;
    size_t count, capacity;
    unsigned long revision;
    size_t binding_uses[MAAGE_INPUT_BINDING_COUNT];
    unsigned char blocked[MAAGE_INPUT_BINDING_COUNT];
    size_t forward, backward, left, right, jump, sneak, sprint, inventory, drop, attack, use, perspective;
    size_t hotbar[9];
} maage_controls;

/* Client owner thread only. Registration copies strings and grows without an
 * action-count ceiling; numeric handles remain stable across reallocations.
 * Call before loading options for additional modules' saved entries to resolve. */
maage_status maage_controls_create(maage_controls **output);
maage_status maage_controls_destroy(maage_controls *controls);
maage_status maage_control_register(maage_controls *controls, const char *identity, const char *name, maage_control_category category, unsigned int binding, size_t *handle);
maage_status maage_control_set(maage_controls *controls, size_t handle, unsigned int binding);
int maage_control_binding_valid(unsigned int binding);
int maage_control_conflict(const maage_controls *controls, size_t handle);
int maage_controls_changed(const maage_controls *controls);
const char *maage_control_category_name(maage_control_category category);
/* Shared display path; buffer must have at least 40 bytes. */
void maage_control_label(unsigned int binding, char *buffer);
/* INI boundary; unknown identities/invalid values leave current values intact. */
maage_status maage_controls_parse(maage_controls *controls, const char *identity, const char *value);
/* Owned immutable INI section for asynchronous save. Caller frees it. */
maage_status maage_controls_serialize(const maage_controls *controls, char **output);
/* Observe releases every frame. Block all held/pressed inputs on modal frames
 * and transitions, so rebinding/navigation never leaks held state into play. */
void maage_controls_observe(maage_controls *controls, const maage_input_snapshot *input);
void maage_controls_block(maage_controls *controls, const maage_input_snapshot *input);
int maage_control_down(const maage_controls *controls, size_t handle, const maage_input_snapshot *input);
int maage_control_pressed(const maage_controls *controls, size_t handle, const maage_input_snapshot *input);
/* GUI inventory closure uses fresh raw presses while gameplay is blocked. */
int maage_control_gui_pressed(const maage_controls *controls, size_t handle, const maage_input_snapshot *input);
#endif
