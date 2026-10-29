#include "maage/storage.h"
#include "maage/crypto.h"
#include <errno.h>

maage_status maage_storage_sha1(const char *path, char hash[41], int (*cancel)(void *), void *context) {
    FILE *file;
    maage_crypto_hash *digest = NULL;
    unsigned char buffer[32768], bytes[20];
    unsigned int i;
    size_t count;
    maage_status result;
    if (hash) hash[0] = 0;
    if (!path || !hash) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_HASH, !path ? 0 : 1);
    if (cancel && cancel(context)) return maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_HASH, 0);
    file = maage_storage_open(path, "rb");
    if (!file) return maage_status_from_errno(errno, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN);
    result = maage_crypto_hash_open(MAAGE_CRYPTO_SHA1, &digest);
    while (!maage_status_failed(result) && (count = fread(buffer, 1, sizeof(buffer), file)) != 0) {
        if (cancel && cancel(context)) result = maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_HASH, 0);
        else
            result = maage_crypto_hash_update(digest, buffer, count);
    }
    if (!maage_status_failed(result) && ferror(file)) result = maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_READ, 0);
    if (!maage_status_failed(result)) result = maage_crypto_hash_finish(digest, bytes, sizeof(bytes));
    if (fclose(file) && !maage_status_failed(result)) result = maage_status_from_errno(errno, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_CLOSE);
    maage_crypto_hash_destroy(digest);
    if (maage_status_failed(result)) return result;
    for (i = 0; i < 20; ++i)
        sprintf(hash + i * 2, "%02x", bytes[i]);
    hash[40] = 0;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_HASH, 0);
}
