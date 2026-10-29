#ifndef MAAGE_IMAGE_H
#define MAAGE_IMAGE_H
#include <stddef.h>
#include "maage/status.h"
/* Owned top-to-bottom RGBA8 image. PNG decoding is an initialization/resource
 * operation, never a block/mesh lookup. Destroy even after a failed load. */
typedef struct maage_image {
    unsigned char *pixels;
    int width, height;
    size_t stride;
} maage_image;
/* Output must be zero-initialized and empty. Rejects images above 2048x2048.
 * UTF-8 file path; optional caller-owned trace preserves file/decode causes.
 * Failure leaves an empty output and never retains decoder-owned storage. */
maage_status maage_image_load_png(const char *path, maage_image *image, maage_status_trace *trace);
/* Original, asset-independent magenta/black checkerboard. Caller owns pixels;
 * nearest sampling is provided by the existing renderer texture contract. */
maage_status maage_image_make_missing(maage_image *image, int width, int height);
maage_status maage_image_destroy(maage_image *image);
#endif
