#ifndef MAAGE_BASE_SOUNDS_INTERNAL_H
#define MAAGE_BASE_SOUNDS_INTERNAL_H
#include "base_internal.h"
#include "maage/sounds.h"

typedef struct maage_base_sound_data {
    const maage_base_ids *ids;
    maage_sound_id rain, water, lava, lava_pop, cave;
    maage_sound_music_id menu_music, game_music, creative_music;
} maage_base_sound_data;
maage_status maage_base_sound_ambience_register(maage_content_context *, const maage_base_sound_data *);
#endif
