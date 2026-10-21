#ifndef MAAGE_CLIENT_OPTIONS_H
#define MAAGE_CLIENT_OPTIONS_H
#include "maage/instance.h"
#include "maage/world.h"

/* Numeric runtime choices; names are resolved only at the INI boundary. */
typedef enum maage_client_window_choice {
    MAAGE_CLIENT_WINDOWED = 0,
    MAAGE_CLIENT_BORDERLESS = 1,
    MAAGE_CLIENT_FULLSCREEN = 2
} maage_client_window_choice;
typedef struct maage_client_options {
    int max_fps; /* 0 unlimited, otherwise 10..260 in increments of 10. */
    int vsync;
    int gui_scale; /* 0 auto; 1..4 explicit. */
    maage_leaves_mode leaves;
    maage_client_window_choice window_mode;
    maage_client_window_choice fullscreen_style; /* Borderless or Fullscreen. */
} maage_client_options;

void maage_client_options_defaults(maage_client_options *options);
int maage_client_options_equal(const maage_client_options *a, const maage_client_options *b);
/* Missing file and invalid individual values retain defaults. */
maage_status maage_client_options_load(const maage_instance *instance, maage_client_options *options);
/* Stage, sync and atomically replace config/options.ini. */
maage_status maage_client_options_save(const maage_instance *instance, const maage_client_options *options);
#endif
