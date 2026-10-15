#ifndef MAAGE_PIXELS_H
#define MAAGE_PIXELS_H
#include <stddef.h>

/* Borrowed top-to-bottom RGBA8 bytes, straight alpha, 8-bit channels. Stride is
 * in bytes and includes any row padding. No graphics API owns this memory. */
typedef struct maage_pixel_buffer {
    const unsigned char *pixels;
    int width;
    int height;
    size_t stride;
} maage_pixel_buffer;
#endif
