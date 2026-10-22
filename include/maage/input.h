#ifndef MAAGE_INPUT_H
#define MAAGE_INPUT_H
#include "maage/window.h"

/* Physical movement-key positions, independent of the current text layout. */
typedef enum maage_input_key {
    MAAGE_INPUT_KEY_W = 0,
    MAAGE_INPUT_KEY_A,
    MAAGE_INPUT_KEY_S,
    MAAGE_INPUT_KEY_D,
    MAAGE_INPUT_KEY_SPACE,
    MAAGE_INPUT_KEY_ESCAPE,
    MAAGE_INPUT_KEY_F3,
    MAAGE_INPUT_KEY_ENTER,
    MAAGE_INPUT_KEY_TAB,
    MAAGE_INPUT_KEY_UP,
    MAAGE_INPUT_KEY_DOWN,
    MAAGE_INPUT_KEY_LEFT,
    MAAGE_INPUT_KEY_RIGHT,
    MAAGE_INPUT_KEY_E,
    MAAGE_INPUT_KEY_1,
    MAAGE_INPUT_KEY_2,
    MAAGE_INPUT_KEY_3,
    MAAGE_INPUT_KEY_4,
    MAAGE_INPUT_KEY_5,
    MAAGE_INPUT_KEY_6,
    MAAGE_INPUT_KEY_7,
    MAAGE_INPUT_KEY_8,
    MAAGE_INPUT_KEY_9,
    MAAGE_INPUT_KEY_SHIFT,
    MAAGE_INPUT_KEY_F11,
    MAAGE_INPUT_KEY_COUNT
} maage_input_key;

typedef struct maage_input_snapshot {
    int keys[MAAGE_INPUT_KEY_COUNT];
    int pressed[MAAGE_INPUT_KEY_COUNT];
    int primary_pressed, primary_down;
    int secondary_pressed, secondary_down;
    double scroll_y; /* Positive scroll selects the preceding hotbar slot. */
    double mouse_x, mouse_y; /* Top-left-origin window logical coordinates. */
    double mouse_delta_x;
    double mouse_delta_y;
    /* Ordered ASCII text/backspace events, consumed with press edges. */
    char text[128];
    int focused;
    int captured;
} maage_input_snapshot;

/* Call on the window thread after event polling. Consumes press edges and mouse
 * deltas; held keys persist. Mouse deltas are native relative units, positive
 * right/down, and accumulate only while captured. Focus loss releases capture
 * and clears held keys. This is raw input; gameplay maps it to actions.
 * UNSUPPORTED means this adapter cannot supply gameplay input. */
maage_status maage_window_get_input(maage_window *window,
    maage_input_snapshot *input);
/* Capture requires focus, hides the cursor and permits unbounded relative motion.
 * Release also clears pending motion. Escape is an input edge; the application
 * chooses when to release or reacquire capture. Native adapters never recapture
 * automatically. A compositor/backend without capture returns UNSUPPORTED. */
maage_status maage_window_capture_mouse(maage_window *window, int capture);

#endif
