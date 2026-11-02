#include "controls.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int maage_control_binding_valid(unsigned int binding) {
    return !binding || (binding >= 4U && binding <= 164U) || (binding >= 176U && binding <= 221U) || (binding >= 224U && binding <= 231U) || (binding >= MAAGE_INPUT_MOUSE_BASE && binding < MAAGE_INPUT_BINDING_COUNT);
}
static char *control_copy(const char *text) {
    size_t length = strlen(text);
    char *copy;
    if (length == (size_t) - 1) return NULL;
    copy = (char *) malloc(length + 1);
    if (copy) memcpy(copy, text, length + 1);
    return copy;
}
maage_status maage_control_register(maage_controls *controls, const char *identity, const char *name, maage_control_category category, unsigned int binding, size_t *handle) {
    maage_control entry, *grown;
    size_t i, capacity;
    if (!controls || !identity || !*identity || !name || !*name || *identity == '#' || *identity == ';' || !handle || category < 0 || category >= MAAGE_CONTROL_CATEGORY_COUNT || !maage_control_binding_valid(binding) || strpbrk(identity, "=\r\n[] \t")) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_REGISTER, 0);
    for (i = 0; i < controls->count; ++i)
        if (!strcmp(identity, controls->actions[i].identity)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_REGISTER, 1);
    if (controls->count == controls->capacity) {
        capacity = controls->capacity ? controls->capacity * 2 : 16;
        if (capacity <= controls->capacity || capacity > ((size_t) - 1) / sizeof(*grown)) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_REGISTER, 0);
        grown = (maage_control *) realloc(controls->actions, capacity * sizeof(*grown));
        if (!grown) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_REGISTER, 0);
        controls->actions = grown;
        controls->capacity = capacity;
    }
    entry.identity = control_copy(identity);
    entry.name = control_copy(name);
    if (!entry.identity || !entry.name) {
        free(entry.identity);
        free(entry.name);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_REGISTER, 0);
    }
    entry.category = category;
    entry.binding = entry.default_binding = binding;
    *handle = controls->count;
    controls->actions[controls->count++] = entry;
    ++controls->binding_uses[binding];
    ++controls->revision;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_REGISTER, 0);
}
maage_status maage_control_set(maage_controls *controls, size_t handle, unsigned int binding) {
    if (!controls || handle >= controls->count || !maage_control_binding_valid(binding)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SET, 0);
    if (controls->actions[handle].binding == binding) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_SET, 0);
    --controls->binding_uses[controls->actions[handle].binding];
    ++controls->binding_uses[binding];
    controls->actions[handle].binding = binding;
    ++controls->revision;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SET, 0);
}
maage_status maage_controls_destroy(maage_controls *controls) {
    size_t i;
    if (!controls) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_DESTROY, 0);
    for (i = 0; i < controls->count; ++i) {
        free(controls->actions[i].identity);
        free(controls->actions[i].name);
    }
    free(controls->actions);
    free(controls);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
maage_status maage_controls_create(maage_controls **output) {
    maage_controls *controls;
    maage_status result;
    unsigned int i;
    char identity[40], name[40];
    if (!output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
    *output = NULL;
    controls = (maage_controls *) calloc(1, sizeof(*controls));
    if (!controls) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_CREATE, 0);
#define CONTROL(field, identity, name, category, binding) \
    result = maage_control_register(controls, identity, name, category, binding, &controls->field); \
    if (maage_status_failed(result)) { (void) maage_controls_destroy(controls); return result; }
    CONTROL(forward, "minecraft:forward", "Walk Forwards", MAAGE_CONTROL_MOVEMENT, MAAGE_INPUT_KEY_W)
    CONTROL(backward, "minecraft:back", "Walk Backwards", MAAGE_CONTROL_MOVEMENT, MAAGE_INPUT_KEY_S)
    CONTROL(left, "minecraft:left", "Strafe Left", MAAGE_CONTROL_MOVEMENT, MAAGE_INPUT_KEY_A)
    CONTROL(right, "minecraft:right", "Strafe Right", MAAGE_CONTROL_MOVEMENT, MAAGE_INPUT_KEY_D)
    CONTROL(jump, "minecraft:jump", "Jump", MAAGE_CONTROL_MOVEMENT, MAAGE_INPUT_KEY_SPACE)
    CONTROL(sneak, "minecraft:sneak", "Sneak", MAAGE_CONTROL_MOVEMENT, MAAGE_INPUT_KEY_LEFT_SHIFT)
    CONTROL(sprint, "minecraft:sprint", "Sprint", MAAGE_CONTROL_MOVEMENT, MAAGE_INPUT_KEY_LEFT_CONTROL)
    CONTROL(inventory, "minecraft:inventory", "Inventory", MAAGE_CONTROL_INVENTORY, MAAGE_INPUT_KEY_E)
    CONTROL(drop, "minecraft:drop", "Drop", MAAGE_CONTROL_GAMEPLAY, MAAGE_INPUT_KEY_Q)
    CONTROL(attack, "minecraft:attack", "Attack/Destroy", MAAGE_CONTROL_GAMEPLAY, MAAGE_INPUT_MOUSE_BASE)
    CONTROL(use, "minecraft:use", "Use Item/Place Block", MAAGE_CONTROL_GAMEPLAY, MAAGE_INPUT_MOUSE_BASE + 1U)
    CONTROL(perspective, "minecraft:perspective", "Toggle Perspective", MAAGE_CONTROL_MISC, MAAGE_INPUT_KEY_F5)
#undef CONTROL
    for (i = 0; i < 9; ++i) {
        sprintf(identity, "minecraft:hotbar.%u", i + 1U);
        sprintf(name, "Hotbar Slot %u", i + 1U);
        result = maage_control_register(controls, identity, name, MAAGE_CONTROL_INVENTORY, MAAGE_INPUT_KEY_1 + i, &controls->hotbar[i]);
        if (maage_status_failed(result)) {
            (void) maage_controls_destroy(controls);
            return result;
        }
    }
    *output = controls;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CREATE, 0);
}
int maage_control_conflict(const maage_controls *controls, size_t handle) {
    unsigned int binding;
    if (!controls || handle >= controls->count) return 0;
    binding = controls->actions[handle].binding;
    return binding && controls->binding_uses[binding] > 1;
}
int maage_controls_changed(const maage_controls *controls) {
    size_t i;
    for (i = 0; controls && i < controls->count; ++i)
        if (controls->actions[i].binding != controls->actions[i].default_binding) return 1;
    return 0;
}
const char *maage_control_category_name(maage_control_category category) {
    static const char *const names[] = {"Gameplay", "Inventory", "Miscellaneous", "Movement", "Multiplayer", "Game Interface"};
    return category >= 0 && category < MAAGE_CONTROL_CATEGORY_COUNT ? names[category] : "";
}
void maage_control_label(unsigned int binding, char *buffer) {
    static const char *const punctuation[] = {"Enter", "Escape", "Backspace", "Tab", "Space", "-", "=", "[", "]", "Backslash", "Non-US #", ";", "'", "`", ",", ".", "/", "Caps Lock"};
    static const char *const navigation[] = {"Print Screen", "Scroll Lock", "Pause", "Insert", "Home", "Page Up", "Delete", "End", "Page Down", "Right", "Left", "Down", "Up", "Num Lock", "Keypad /", "Keypad *", "Keypad -", "Keypad +", "Keypad Enter"};
    static const char *const modifiers[] = {"Left Ctrl", "Left Shift", "Left Alt", "Left Super", "Right Ctrl", "Right Shift", "Right Alt", "Right Super"};
    if (!binding) strcpy(buffer, "Unassigned");
    else if (binding >= MAAGE_INPUT_MOUSE_BASE) {
        if (binding == MAAGE_INPUT_MOUSE_BASE + 2U) strcpy(buffer, "Middle Mouse Button");
        else sprintf(buffer, "Mouse Button %u", binding - MAAGE_INPUT_MOUSE_BASE + 1U);
    } else if (binding >= 4U && binding <= 29U) {
        buffer[0] = (char) ('A' + binding - 4U);
        buffer[1] = 0;
    } else if (binding >= 30U && binding <= 39U) {
        buffer[0] = (char) ('0' + (binding - 29U) % 10U);
        buffer[1] = 0;
    } else if (binding >= 40U && binding <= 57U) strcpy(buffer, punctuation[binding - 40U]);
    else if (binding >= 58U && binding <= 69U) sprintf(buffer, "F%u", binding - 57U);
    else if (binding >= 70U && binding <= 88U) strcpy(buffer, navigation[binding - 70U]);
    else if (binding >= 89U && binding <= 98U) sprintf(buffer, "Keypad %u", (binding - 88U) % 10U);
    else if (binding == 99U) strcpy(buffer, "Keypad .");
    else if (binding == 100U) strcpy(buffer, "Non-US Backslash");
    else if (binding == 101U) strcpy(buffer, "Menu");
    else if (binding == 103U) strcpy(buffer, "Keypad =");
    else if (binding >= 104U && binding <= 115U) sprintf(buffer, "F%u", binding - 91U);
    else if (binding >= 224U && binding <= 231U) strcpy(buffer, modifiers[binding - 224U]);
    else sprintf(buffer, "Key %u", binding);
}
maage_status maage_controls_parse(maage_controls *controls, const char *identity, const char *value) {
    char *end;
    unsigned long number;
    unsigned int binding;
    size_t i;
    int mouse;
    maage_status invalid = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
    if (!controls || !identity || !value) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_PARSE, 0);
    if (!strcmp(value, "none")) binding = 0;
    else {
        mouse = !strncmp(value, "mouse:", 6);
        if (!mouse && strncmp(value, "key:", 4)) return invalid;
        value += mouse ? 6 : 4;
        if (*value < '0' || *value > '9') return invalid;
        errno = 0;
        number = strtoul(value, &end, 10);
        if (errno || *end || (mouse ? (!number || number > MAAGE_INPUT_MOUSE_COUNT) : number > 255UL)) return invalid;
        binding = mouse ? MAAGE_INPUT_MOUSE_BASE + (unsigned int) number - 1U : (unsigned int) number;
        if (!maage_control_binding_valid(binding)) return invalid;
    }
    for (i = 0; i < controls->count; ++i)
        if (!strcmp(controls->actions[i].identity, identity)) {
            return maage_control_set(controls, i, binding);
        }
    return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_PARSE, 0);
}

maage_status maage_controls_serialize(const maage_controls *controls, char **output) {
    size_t i, length = 14, extra;
    char *text, *cursor;
    unsigned int binding;
    if (!controls || !output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SAVE, 0);
    *output = NULL;
    for (i = 0; i < controls->count; ++i) {
        extra = strlen(controls->actions[i].identity);
        if (extra > (size_t) - 1 - 32 || length > (size_t) - 1 - extra - 32) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SAVE, 0);
        length += extra + 32;
    }
    text = (char *) malloc(length);
    if (!text) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SAVE, 0);
    cursor = text + sprintf(text, "\n[controls]\n");
    for (i = 0; i < controls->count; ++i) {
        binding = controls->actions[i].binding;
        extra = strlen(controls->actions[i].identity);
        memcpy(cursor, controls->actions[i].identity, extra);
        cursor += extra;
        *cursor++ = '=';
        if (!binding) cursor += sprintf(cursor, "none\n");
        else if (binding >= MAAGE_INPUT_MOUSE_BASE) cursor += sprintf(cursor, "mouse:%u\n", binding - MAAGE_INPUT_MOUSE_BASE + 1U);
        else cursor += sprintf(cursor, "key:%u\n", binding);
    }
    *output = text;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SAVE, 0);
}
static int control_raw(const maage_input_snapshot *input, unsigned int binding, int pressed) {
    if (!binding || binding >= MAAGE_INPUT_BINDING_COUNT || !input || !input->focused) return 0;
    if (binding >= MAAGE_INPUT_MOUSE_BASE) return pressed ? input->mouse_pressed[binding - MAAGE_INPUT_MOUSE_BASE] : input->mouse_down[binding - MAAGE_INPUT_MOUSE_BASE];
    return pressed ? input->pressed[binding] : input->keys[binding];
}
void maage_controls_observe(maage_controls *controls, const maage_input_snapshot *input) {
    unsigned int i;
    for (i = 1; i < MAAGE_INPUT_BINDING_COUNT; ++i)
        if (!control_raw(input, i, 0) || control_raw(input, i, 1)) controls->blocked[i] = 0;
}
void maage_controls_block(maage_controls *controls, const maage_input_snapshot *input) {
    unsigned int i;
    for (i = 1; i < MAAGE_INPUT_BINDING_COUNT; ++i)
        if (control_raw(input, i, 0) || control_raw(input, i, 1)) controls->blocked[i] = 1;
}
int maage_control_down(const maage_controls *controls, size_t handle, const maage_input_snapshot *input) {
    unsigned int binding;
    if (!controls || handle >= controls->count) return 0;
    binding = controls->actions[handle].binding;
    return !controls->blocked[binding] && control_raw(input, binding, 0);
}
int maage_control_pressed(const maage_controls *controls, size_t handle, const maage_input_snapshot *input) {
    unsigned int binding;
    if (!controls || handle >= controls->count) return 0;
    binding = controls->actions[handle].binding;
    return !controls->blocked[binding] && control_raw(input, binding, 1);
}
int maage_control_gui_pressed(const maage_controls *controls, size_t handle, const maage_input_snapshot *input) {
    return controls && handle < controls->count && control_raw(input, controls->actions[handle].binding, 1);
}
