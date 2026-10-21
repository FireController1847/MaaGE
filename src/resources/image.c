#include "maage/image.h"
#include "maage/storage.h"
#include <png.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

maage_status maage_image_load_png_status(const char *path, maage_image *output)
{
    png_image image;
    FILE *file;
    unsigned char *pixels;
    unsigned char signature[8];
    unsigned char code;
    int ok;
    if (!path || !output || output->pixels)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE,
            MAAGE_OPERATION_DECODE, (unsigned char)(!path ? 0 : 1));
    errno = 0;
    file = maage_storage_open(path, "rb");
    if (!file) {
        code = errno == ENOENT || errno == ENOTDIR ? MAAGE_STATUS_NOT_FOUND :
            (errno == EACCES || errno == EPERM ? MAAGE_STATUS_ACCESS_DENIED : MAAGE_STATUS_IO_ERROR);
        return maage_status_make(code, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_LOAD_TEXTURE, 0);
    }
    code = MAAGE_STATUS_MALFORMED_INPUT;
    if (fread(signature, 1, sizeof(signature), file) != sizeof(signature)) {
        if (ferror(file)) code = MAAGE_STATUS_IO_ERROR;
        fclose(file);
        return maage_status_make(code, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_DECODE, 0);
    }
    if (png_sig_cmp(signature, 0, sizeof(signature))) {
        fclose(file);
        return maage_status_make(MAAGE_STATUS_NOT_IMPLEMENTED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_DECODE, 0);
    }
    rewind(file);
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_stdio(&image, file)) {
        png_image_free(&image); fclose(file);
        return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_DECODE, 0);
    }
    if (!image.width || !image.height || image.width > 2048 || image.height > 2048) {
        png_image_free(&image); fclose(file);
        return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_DECODE, 0);
    }
    image.format = PNG_FORMAT_RGBA;
    pixels = (unsigned char *)malloc(PNG_IMAGE_SIZE(image));
    ok = pixels && png_image_finish_read(&image, NULL, pixels, 0, NULL);
    if (ok) {
        output->pixels = pixels;
        output->width = (int)image.width;
        output->height = (int)image.height;
        output->stride = (size_t)image.width * 4;
    } else {
        code = !pixels ? MAAGE_STATUS_ALLOCATION_FAILED :
            (ferror(file) ? MAAGE_STATUS_IO_ERROR : MAAGE_STATUS_MALFORMED_INPUT);
        free(pixels);
    }
    png_image_free(&image);
    fclose(file);
    return maage_status_make(ok ? MAAGE_STATUS_SUCCESS : code,
        MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_DECODE, 0);
}

int maage_image_load_png(const char *path, maage_image *output)
{
    return !maage_status_failed(maage_image_load_png_status(path, output));
}

maage_status maage_image_make_missing(maage_image *image, int width, int height)
{
    int x, y, magenta, half_x, half_y;
    unsigned char *pixel;
    if (!image || image->pixels || width < 1 || height < 1 || width > 2048 || height > 2048)
        return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS, 0);
    if ((size_t)width > (size_t)-1 / 4 ||
        (size_t)height > (size_t)-1 / ((size_t)width * 4))
        return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS, 0);
    image->stride = (size_t)width * 4;
    image->pixels = (unsigned char *)malloc(image->stride * (size_t)height);
    if (!image->pixels)
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_ATLAS, 0);
    image->width = width; image->height = height;
    half_x = width / 2; half_y = height / 2;
    if (!half_x) half_x = 1;
    if (!half_y) half_y = 1;
    for (y = 0; y < height; ++y) for (x = 0; x < width; ++x) {
        magenta = ((x / half_x) + (y / half_y)) % 2;
        pixel = image->pixels + (size_t)y * image->stride + (size_t)x * 4;
        pixel[0] = pixel[2] = (unsigned char)(magenta ? 255 : 0);
        pixel[1] = 0; pixel[3] = 255;
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_ATLAS, 0);
}

void maage_image_destroy(maage_image *image)
{
    if (!image) return;
    free(image->pixels);
    memset(image, 0, sizeof(*image));
}
