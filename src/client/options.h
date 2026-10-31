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
/* These stable IDs are shared by the Options UI and Client audio service.
 * Names are only used when reading/writing config/options.ini.
 * Percentage values are integers in [0, 100], inclusive. */
typedef enum maage_client_audio_bus {
    MAAGE_CLIENT_AUDIO_BUS_MASTER = 0,
    MAAGE_CLIENT_AUDIO_BUS_BLOCKS = 1,
    MAAGE_CLIENT_AUDIO_BUS_PLAYER = 2,
    MAAGE_CLIENT_AUDIO_BUS_AMBIENT = 3,
    MAAGE_CLIENT_AUDIO_BUS_WEATHER = 4,
    MAAGE_CLIENT_AUDIO_BUS_MUSIC = 5,
    MAAGE_CLIENT_AUDIO_BUS_UI = 6,
    MAAGE_CLIENT_AUDIO_BUS_COUNT = 7
} maage_client_audio_bus;
#define MAAGE_CLIENT_AUDIO_VOLUME_MIN 0
#define MAAGE_CLIENT_AUDIO_VOLUME_MAX 100
#define MAAGE_CLIENT_AUDIO_DEFAULT_MASTER 100
#define MAAGE_CLIENT_AUDIO_DEFAULT_BLOCKS 100
#define MAAGE_CLIENT_AUDIO_DEFAULT_PLAYER 100
#define MAAGE_CLIENT_AUDIO_DEFAULT_AMBIENT 75
#define MAAGE_CLIENT_AUDIO_DEFAULT_WEATHER 75
#define MAAGE_CLIENT_AUDIO_DEFAULT_MUSIC 50
#define MAAGE_CLIENT_AUDIO_DEFAULT_UI 100
/* INI boundary only: exact names in [audio]. */
#define MAAGE_CLIENT_AUDIO_KEY_MASTER "master"
#define MAAGE_CLIENT_AUDIO_KEY_BLOCKS "blocks"
#define MAAGE_CLIENT_AUDIO_KEY_PLAYER "player"
#define MAAGE_CLIENT_AUDIO_KEY_AMBIENT "ambient"
#define MAAGE_CLIENT_AUDIO_KEY_WEATHER "weather"
#define MAAGE_CLIENT_AUDIO_KEY_MUSIC "music"
#define MAAGE_CLIENT_AUDIO_KEY_UI "ui"
typedef struct maage_client_options {
    int audio_volume[MAAGE_CLIENT_AUDIO_BUS_COUNT]; /* Fixed numeric bus order; 0..100. */
    int fov_degrees;                                /* Vertical field of view, 30..110, default 70. */
    int max_fps;                                    /* 0 unlimited, otherwise 10..260 in increments of 10. */
    int vsync;
    int view_bobbing;
    int gui_scale; /* 0 auto; 1..4 explicit. */
    maage_leaves_mode leaves;
    maage_biome_blend biome_blend;
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
