#ifndef MAAGE_CLIENT_RESOURCES_H
#define MAAGE_CLIENT_RESOURCES_H
#include "maage/image.h"
#include "maage/pixels.h"
/* Resolved numeric atlas slots, consumed by registration only. */
#define MAAGE_CLIENT_TEXTURE_STONE 0
#define MAAGE_CLIENT_TEXTURE_DIRT 1
#define MAAGE_CLIENT_TEXTURE_GRASS_TOP 2
#define MAAGE_CLIENT_TEXTURE_GRASS_SIDE 3
#define MAAGE_CLIENT_TEXTURE_COUNT 4
typedef struct maage_client_resources {
    maage_image image;
    maage_pixel_buffer atlas;
    float uv[MAAGE_CLIENT_TEXTURE_COUNT][4];
} maage_client_resources;
int maage_client_resources_load(const char *application_data,
    maage_client_resources *resources);
void maage_client_resources_destroy(maage_client_resources *resources);
#endif
