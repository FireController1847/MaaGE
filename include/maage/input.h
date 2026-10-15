#ifndef MAAGE_INPUT_H
#define MAAGE_INPUT_H
#include "maage/window.h"
typedef enum maage_input_key {
    MAAGE_KEY_NONE, MAAGE_KEY_TAB, MAAGE_KEY_ENTER, MAAGE_KEY_ESCAPE,
    MAAGE_KEY_BACKSPACE, MAAGE_KEY_LEFT, MAAGE_KEY_RIGHT
} maage_input_key;
typedef struct maage_input_state {
    float x, y, scroll;
    int down, pressed, released, shift, cancelled;
    maage_input_key key;
    char text[64];
} maage_input_state;
/* Initialization-thread snapshot in logical top-left window coordinates.
 * Poll after window event dispatch. Edges/text/scroll are consumed once.
 * Text is bounded UTF-8 insertion, not an IME/clipboard editing contract.
 * Native input adapter currently exists on Cocoa; others return UNSUPPORTED. */
maage_window_result maage_input_poll(maage_window *window, maage_input_state *state);
#endif
