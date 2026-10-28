#ifndef MAAGE_SKIN_H
#define MAAGE_SKIN_H
#include "maage/image.h"
#include "maage/storage.h"
#define MAAGE_SKIN_CLASSIC 0U
#define MAAGE_SKIN_SLIM 1U
#define MAAGE_SKIN_ARGUMENT_CAPACITY 160
#define MAAGE_SKIN_MAX_BYTES 262144UL
/* Nonsecret launch reference, not an identity registry or authorization grant.
 * UUID is the authenticated lowercase 32-digit Java profile ID. Empty texture
 * selects the original default appearance. Texture is an opaque 64-hex ID. */
typedef struct maage_skin_reference {
    char uuid[33], name[17], texture[65];
    unsigned int model;
} maage_skin_reference;
int maage_skin_reference_valid(const maage_skin_reference *reference);
maage_status maage_skin_reference_parse(const char *text, maage_skin_reference *reference);
maage_status maage_skin_reference_format(const maage_skin_reference *reference,
    char text[MAAGE_SKIN_ARGUMENT_CAPACITY]);
maage_status maage_skin_path(const char *root, const maage_skin_reference *reference,
    char path[MAAGE_PATH_CAPACITY]);
/* Initialization only. Bounds file/IHDR before decoding through MaaGE::Image.
 * Produces one owned 64x64 RGBA skin; legacy left limbs are mirrored into their
 * modern regions, base faces made opaque, and optional outer alpha preserved.
 * Legacy skins require classic metadata. Destroy output even after failure. */
maage_status maage_skin_load(const char *path, unsigned int model,
    maage_image *image, maage_status_trace *trace);
#endif
