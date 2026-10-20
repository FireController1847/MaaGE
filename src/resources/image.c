#include "maage/image.h"
#include "maage/storage.h"
#include <png.h>
#include <stdlib.h>
#include <string.h>

int maage_image_load_png(const char *path, maage_image *output)
{
    png_image image;
    FILE *file;
    unsigned char *pixels;
    int ok;
    if (!path || !output || output->pixels) return 0;
    file = maage_storage_open(path, "rb");
    if (!file) return 0;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_stdio(&image, file)) {
        png_image_free(&image); fclose(file); return 0;
    }
    if (!image.width || !image.height || image.width > 2048 || image.height > 2048) {
        png_image_free(&image); fclose(file); return 0;
    }
    image.format = PNG_FORMAT_RGBA;
    pixels = (unsigned char *)malloc(PNG_IMAGE_SIZE(image));
    ok = pixels && png_image_finish_read(&image, NULL, pixels, 0, NULL);
    if (ok) {
        output->pixels = pixels;
        output->width = (int)image.width;
        output->height = (int)image.height;
        output->stride = (size_t)image.width * 4;
    } else free(pixels);
    png_image_free(&image);
    fclose(file);
    return ok;
}

void maage_image_destroy(maage_image *image)
{
    if (!image) return;
    free(image->pixels);
    memset(image, 0, sizeof(*image));
}
