#include "maage/skin.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static maage_status maage_skin_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_RESOURCE, operation, 0);
}
static int maage_skin_hex(const char *text, size_t count) {
    return strlen(text) == count && strspn(text, "0123456789abcdef") == count;
}
int maage_skin_reference_valid(const maage_skin_reference *reference) {
    size_t length;
    if (!reference || !memchr(reference->uuid, 0, sizeof(reference->uuid)) ||
        !memchr(reference->name, 0, sizeof(reference->name)) || !memchr(reference->texture, 0, sizeof(reference->texture))) return 0;
    length = strlen(reference->name);
    return maage_skin_hex(reference->uuid, 32) && strspn(reference->uuid, "0") != 32 &&
           length >= 1 && length <= 16 && strspn(reference->name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") == length &&
           reference->model <= MAAGE_SKIN_SLIM &&
           (*reference->texture ? maage_skin_hex(reference->texture, 64) : reference->model == MAAGE_SKIN_CLASSIC);
}
maage_status maage_skin_reference_parse(const char *text, maage_skin_reference *reference) {
    maage_skin_reference parsed;
    const char *name, *model, *texture;
    size_t length;
    if (!text || !reference) return maage_skin_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_PARSE);
    memset(&parsed, 0, sizeof(parsed));
    length = strlen(text);
    if (length >= MAAGE_SKIN_ARGUMENT_CAPACITY || length < 40 || strncmp(text, "1:", 2) || text[34] != ':') goto invalid;
    memcpy(parsed.uuid, text + 2, 32);
    name = text + 35;
    model = strchr(name, ':');
    if (!model || model == name || model - name > 16) goto invalid;
    memcpy(parsed.name, name, (size_t) (model - name));
    ++model;
    texture = strchr(model, ':');
    if (!texture) goto invalid;
    if (texture - model == 4 && !strncmp(model, "slim", 4))
        parsed.model = MAAGE_SKIN_SLIM;
    else if (texture - model != 7 || strncmp(model, "classic", 7))
        goto invalid;
    ++texture;
    if (strcmp(texture, "-")) {
        if (strlen(texture) != 64) goto invalid;
        strcpy(parsed.texture, texture);
    }
    if (!maage_skin_reference_valid(&parsed)) goto invalid;
    *reference = parsed;
    return maage_skin_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_PARSE);
invalid:
    return maage_skin_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_PARSE);
}
maage_status maage_skin_reference_format(const maage_skin_reference *reference,
                                         char text[MAAGE_SKIN_ARGUMENT_CAPACITY]) {
    if (!text || !maage_skin_reference_valid(reference)) return maage_skin_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_FORMAT);
    sprintf(text, "1:%s:%s:%s:%s", reference->uuid, reference->name,
            reference->model == MAAGE_SKIN_SLIM ? "slim" : "classic", *reference->texture ? reference->texture : "-");
    return maage_skin_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FORMAT);
}
maage_status maage_skin_path(const char *root, const maage_skin_reference *reference,
                             char path[MAAGE_PATH_CAPACITY]) {
    char relative[160], directory[MAAGE_PATH_CAPACITY];
    static const char *const parents[2] = {"minecraft", "minecraft/skins"};
    unsigned int i;
    int kind;
    maage_status result;
    if (!root || !path || !maage_skin_reference_valid(reference) || !*reference->texture)
        return maage_skin_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    for (i = 0; i < 3; ++i) {
        if (i < 2)
            result = maage_storage_join(directory, sizeof(directory), root, parents[i]);
        else {
            sprintf(relative, "minecraft/skins/%s", reference->uuid);
            result = maage_storage_join(directory, sizeof(directory), root, relative);
        }
        if (maage_status_failed(result)) return result;
        kind = maage_storage_status(directory);
        if (kind != 2) return maage_skin_status(kind == 0 ? MAAGE_STATUS_NOT_FOUND : MAAGE_STATUS_ACCESS_DENIED, MAAGE_OPERATION_RESOLVE);
    }
    sprintf(relative, "minecraft/skins/%s/%s.png", reference->uuid, reference->texture);
    return maage_storage_join(path, MAAGE_PATH_CAPACITY, root, relative);
}
static void maage_skin_alpha(maage_image *image, int x0, int y0, int x1, int y1, unsigned char alpha) {
    int x, y;
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x)
            image->pixels[(size_t) y * image->stride + (size_t) x * 4 + 3] = alpha;
}
static void maage_skin_mirror(maage_image *image, int sx, int sy, int dx, int dy, int width, int height) {
    int x, y;
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x)
            memcpy(image->pixels + (size_t) (dy + y) * image->stride + (size_t) (dx + x) * 4,
                   image->pixels + (size_t) (sy + y) * image->stride + (size_t) (sx + width - 1 - x) * 4, 4);
}
maage_status maage_skin_load(const char *path, unsigned int model, maage_image *image, maage_status_trace *trace) {
    static const unsigned char signature[16] = {137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82};
    unsigned char header[24], *pixels;
    char fingerprint[128];
    double size;
    maage_status result;
    FILE *file;
    int legacy, x, y, transparent = 0;
    if (!path || !image || image->pixels || model > MAAGE_SKIN_SLIM)
        return maage_skin_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_DECODE);
    result = maage_storage_stat(path, &size, fingerprint);
    if (maage_status_failed(result)) goto done;
    if (size < 33 || size > MAAGE_SKIN_MAX_BYTES) {
        result = maage_skin_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_DECODE);
        goto done;
    }
    file = maage_storage_open(path, "rb");
    if (!file) {
        result = maage_status_from_errno(errno, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN);
        goto done;
    }
    x = fread(header, 1, sizeof(header), file) == sizeof(header);
    y = fclose(file);
    if (!x || y || memcmp(header, signature, sizeof(signature)) || header[16] || header[17] || header[18] || header[19] != 64 ||
        header[20] || header[21] || header[22] || (header[23] != 32 && header[23] != 64) || (header[23] == 32 && model != MAAGE_SKIN_CLASSIC)) {
        result = maage_skin_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_DECODE);
        goto done;
    }
    result = maage_image_load_png(path, image, trace);
    if (maage_status_failed(result)) goto done;
    if (image->width != 64 || (image->height != 32 && image->height != 64)) {
        maage_image_destroy(image);
        result = maage_skin_status(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_DECODE);
        goto done;
    }
    legacy = image->height == 32;
    if (legacy) {
        pixels = (unsigned char *) calloc(64 * 64, 4);
        if (!pixels) {
            maage_image_destroy(image);
            result = maage_skin_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_DECODE);
            goto done;
        }
        for (y = 0; y < 32; ++y)
            memcpy(pixels + (size_t) y * 256, image->pixels + (size_t) y * image->stride, 256);
        free(image->pixels);
        image->pixels = pixels;
        image->height = 64;
        image->stride = 256;
        /* Mirror paired classic limbs, exchanging their side faces. */
        for (x = 0; x < 2; ++x) {
            int source = x ? 40 : 0, destination = x ? 32 : 16;
            maage_skin_mirror(image, source + 4, 16, destination + 4, 48, 4, 4);
            maage_skin_mirror(image, source + 8, 16, destination + 8, 48, 4, 4);
            maage_skin_mirror(image, source + 8, 20, destination, 52, 4, 12);
            maage_skin_mirror(image, source + 4, 20, destination + 4, 52, 4, 12);
            maage_skin_mirror(image, source, 20, destination + 8, 52, 4, 12);
            maage_skin_mirror(image, source + 12, 20, destination + 12, 52, 4, 12);
        }
        for (y = 0; y < 32; ++y)
            for (x = 32; x < 64; ++x)
                if (image->pixels[(size_t) y * 256 + (size_t) x * 4 + 3] < 128) transparent = 1;
        if (!transparent) maage_skin_alpha(image, 32, 0, 64, 16, 0);
    }
    maage_skin_alpha(image, 0, 0, 32, 16, 255);
    maage_skin_alpha(image, 0, 16, 64, 32, 255);
    maage_skin_alpha(image, 16, 48, 48, 64, 255);
done:
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_RESOURCE, MAAGE_OPERATION_DECODE);
}
