#ifndef MAAGE_INPUT_H
#define MAAGE_INPUT_H
#include "maage/window.h"

/* USB HID keyboard/keypad page (0x07) usage IDs are layout-independent physical
 * positions. The 256-entry hardware domain does not limit registered actions.
 * Zero is unassigned. Aggregate modifiers are GUI-only, never persisted. */
typedef unsigned int maage_input_key;
#define MAAGE_INPUT_KEY_W 26U
#define MAAGE_INPUT_KEY_A 4U
#define MAAGE_INPUT_KEY_S 22U
#define MAAGE_INPUT_KEY_D 7U
#define MAAGE_INPUT_KEY_E 8U
#define MAAGE_INPUT_KEY_Q 20U
#define MAAGE_INPUT_KEY_T 23U
#define MAAGE_INPUT_KEY_SPACE 44U
#define MAAGE_INPUT_KEY_ESCAPE 41U
#define MAAGE_INPUT_KEY_F3 60U
#define MAAGE_INPUT_KEY_F5 62U
#define MAAGE_INPUT_KEY_F11 68U
#define MAAGE_INPUT_KEY_ENTER 40U
#define MAAGE_INPUT_KEY_TAB 43U
#define MAAGE_INPUT_KEY_HOME 74U
#define MAAGE_INPUT_KEY_DELETE 76U
#define MAAGE_INPUT_KEY_END 77U
#define MAAGE_INPUT_KEY_UP 82U
#define MAAGE_INPUT_KEY_DOWN 81U
#define MAAGE_INPUT_KEY_LEFT 80U
#define MAAGE_INPUT_KEY_RIGHT 79U
#define MAAGE_INPUT_KEY_1 30U
#define MAAGE_INPUT_KEY_LEFT_CONTROL 224U
#define MAAGE_INPUT_KEY_LEFT_SHIFT 225U
#define MAAGE_INPUT_KEY_RIGHT_CONTROL 228U
#define MAAGE_INPUT_KEY_RIGHT_SHIFT 229U
#define MAAGE_INPUT_KEY_SHIFT 256U
#define MAAGE_INPUT_KEY_CONTROL 257U
#define MAAGE_INPUT_KEY_COUNT 258U
#define MAAGE_INPUT_MOUSE_COUNT 8U
#define MAAGE_INPUT_MOUSE_BASE 256U
#define MAAGE_INPUT_BINDING_COUNT 264U

typedef struct maage_input_snapshot {
    int keys[MAAGE_INPUT_KEY_COUNT];
    int pressed[MAAGE_INPUT_KEY_COUNT];
    int mouse_down[MAAGE_INPUT_MOUSE_COUNT], mouse_pressed[MAAGE_INPUT_MOUSE_COUNT];
    unsigned int first_pressed; /* HID usage or MOUSE_BASE + zero-based button. */
    int primary_pressed, primary_down;
    int secondary_pressed, secondary_down;
    double scroll_y;         /* Positive scroll selects the preceding hotbar slot. */
    double mouse_x, mouse_y; /* Top-left-origin window logical coordinates. */
    double mouse_delta_x;
    double mouse_delta_y;
    /* Ordered ASCII text/backspace events, consumed with press edges. */
    char text[128];
    int focused;
    int focus_lost; /* Latched through a loss/regain within one polling interval. */
    int captured;
} maage_input_snapshot;

/* Call on the window thread after event polling. Consumes press edges and mouse
 * deltas; held keys persist. Mouse deltas are native relative units, positive
 * right/down, and accumulate only while captured. Focus loss releases capture
 * and clears held keys. This is raw input; gameplay maps it to actions.
 * UNSUPPORTED means this adapter cannot supply gameplay input. */
maage_status maage_window_get_input(maage_window *window, maage_input_snapshot *input);
/* Capture requires focus, hides the cursor and permits unbounded relative motion.
 * Release also clears pending motion. Escape is an input edge; the application
 * chooses when to release or reacquire capture. Native adapters never recapture
 * automatically. A compositor/backend without capture returns UNSUPPORTED. */
maage_status maage_window_capture_mouse(maage_window *window, int capture);

#endif
