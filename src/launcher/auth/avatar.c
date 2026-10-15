#include "internal.h"
#include <string.h>
#include <stdlib.h>
#ifdef __APPLE__
#include <ImageIO/ImageIO.h>
#include <CoreGraphics/CoreGraphics.h>
/* Decode only standard skin dimensions, then compose the face and hat layer.
 * This optional display request never receives authentication headers. */
static int maage_auth_face(const char *bytes, size_t size, unsigned char pixels[256])
{
    CFDataRef data = CFDataCreate(NULL, (const UInt8 *)bytes, size);
    CGImageSourceRef source = data ? CGImageSourceCreateWithData(data, NULL) : NULL;
    CFDictionaryRef properties = source ? CGImageSourceCopyPropertiesAtIndex(source, 0, NULL) : NULL;
    CFNumberRef value;
    CGImageRef image = NULL, face = NULL, hat = NULL;
    CGColorSpaceRef colors = NULL;
    CGContextRef context = NULL;
    int width = 0, height = 0, result = 0;
    size_t index;
    if (!properties) goto cleanup;
    value = CFDictionaryGetValue(properties, kCGImagePropertyPixelWidth);
    if (!value || CFGetTypeID(value) != CFNumberGetTypeID() || !CFNumberGetValue(value, kCFNumberIntType, &width)) goto cleanup;
    value = CFDictionaryGetValue(properties, kCGImagePropertyPixelHeight);
    if (!value || CFGetTypeID(value) != CFNumberGetTypeID() || !CFNumberGetValue(value, kCFNumberIntType, &height)) goto cleanup;
    if (width != 64 || (height != 32 && height != 64)) goto cleanup;
    image = CGImageSourceCreateImageAtIndex(source, 0, NULL);
    if (!image || CGImageGetWidth(image) != 64 || CGImageGetHeight(image) != (size_t)height) goto cleanup;
    face = CGImageCreateWithImageInRect(image, CGRectMake(8, 8, 8, 8));
    hat = CGImageCreateWithImageInRect(image, CGRectMake(40, 8, 8, 8));
    colors = CGColorSpaceCreateDeviceRGB();
    if (!face || !hat || !colors) goto cleanup;
    memset(pixels, 0, 256);
    context = CGBitmapContextCreate(pixels, 8, 8, 8, 32, colors, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    if (!context) goto cleanup;
    CGContextDrawImage(context, CGRectMake(0, 0, 8, 8), face);
    CGContextDrawImage(context, CGRectMake(0, 0, 8, 8), hat);
    CGContextFlush(context);
    for (index = 0; index < 256; index += 4) if (pixels[index + 3]) {
        unsigned int alpha = pixels[index + 3];
        pixels[index] = (unsigned char)((unsigned int)pixels[index] * 255 / alpha);
        pixels[index + 1] = (unsigned char)((unsigned int)pixels[index + 1] * 255 / alpha);
        pixels[index + 2] = (unsigned char)((unsigned int)pixels[index + 2] * 255 / alpha);
    }
    result = 1;
cleanup:
    if (context) CGContextRelease(context);
    if (colors) CGColorSpaceRelease(colors);
    if (hat) CGImageRelease(hat);
    if (face) CGImageRelease(face);
    if (image) CGImageRelease(image);
    if (properties) CFRelease(properties);
    if (source) CFRelease(source);
    if (data) CFRelease(data);
    return result;
}
#endif
void maage_auth_avatar(maage_auth *auth, maage_auth_record *record, maage_auth_json *profile)
{
    record->avatar_valid = 0;
#ifdef __APPLE__
    maage_auth_json *skins = maage_auth_json_get(profile, "skins"), *skin;
    const char *url = NULL, *state, *hash;
    char secure_url[128], *bytes = NULL;
    size_t index, length = 0;
    long status;
    for (index = 0; index < maage_auth_json_count(skins); ++index) {
        skin = maage_auth_json_at(skins, index); state = maage_auth_json_text(skin, "state");
        if (state && !strcmp(state, "ACTIVE")) { url = maage_auth_json_text(skin, "url"); break; }
    }
    if (!url) return;
    hash = !strncmp(url, "https://textures.minecraft.net/texture/", sizeof("https://textures.minecraft.net/texture/") - 1) ? url + sizeof("https://textures.minecraft.net/texture/") - 1 :
        !strncmp(url, "http://textures.minecraft.net/texture/", sizeof("http://textures.minecraft.net/texture/") - 1) ? url + sizeof("http://textures.minecraft.net/texture/") - 1 : NULL;
    if (!hash || strlen(hash) != 64) return;
    for (index = 0; index < 64; ++index) if (!((hash[index] >= '0' && hash[index] <= '9') || (hash[index] >= 'a' && hash[index] <= 'f'))) return;
    strcpy(secure_url, "https://textures.minecraft.net/texture/"); strcat(secure_url, hash);
    maage_auth_progress(auth, "Loading Minecraft profile picture...");
    if (maage_auth_http_bytes(auth, secure_url, "image/png", NULL, NULL, &status, &bytes, &length) == MAAGE_AUTH_OK && status == 200)
        record->avatar_valid = maage_auth_face(bytes, length, record->avatar);
    free(bytes);
#else
    (void)auth; (void)profile;
#endif
}
