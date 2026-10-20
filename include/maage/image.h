#ifndef MAAGE_IMAGE_H
#define MAAGE_IMAGE_H
#include <stddef.h>
/* Owned top-to-bottom RGBA8 image. PNG decoding is an initialization/resource
 * operation, never a block/mesh lookup. Destroy even after a failed load. */
typedef struct maage_image {
    unsigned char *pixels;
    int width, height;
    size_t stride;
} maage_image;
/* Output must be zero-initialized and empty. Rejects images above 2048x2048.
 * UTF-8 file path; returns zero on missing, malformed or oversized input. */
int maage_image_load_png(const char *path, maage_image *image);
void maage_image_destroy(maage_image *image);
#endif
