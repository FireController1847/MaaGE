#include "maage/storage.h"
#include <openssl/evp.h>
#include <errno.h>

maage_status maage_storage_sha1(const char *path, char hash[41], int (*cancel)(void *), void *context)
{
    FILE *file;
    EVP_MD_CTX *digest;
    unsigned char buffer[32768], bytes[EVP_MAX_MD_SIZE];
    unsigned int length, i;
    size_t count;
    maage_status result;
    if (hash) hash[0] = 0;
    if (!path || !hash) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_HASH, !path ? 0 : 1);
    if (cancel && cancel(context)) return maage_status_make(MAAGE_STATUS_INTERRUPTED,
        MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_HASH, 0);
    file = maage_storage_open(path, "rb");
    if (!file) return maage_status_from_errno(errno, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_OPEN);
    digest = EVP_MD_CTX_new();
    result = maage_status_make(!digest ? MAAGE_STATUS_ALLOCATION_FAILED :
        (EVP_DigestInit_ex(digest, EVP_sha1(), NULL) == 1 ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_DEPENDENCY_FAILURE),
        MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_HASH, 0);
    while (!maage_status_failed(result) && (count = fread(buffer, 1, sizeof(buffer), file)) != 0) {
        if (cancel && cancel(context)) result = maage_status_make(MAAGE_STATUS_INTERRUPTED,
            MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_HASH, 0);
        else if (EVP_DigestUpdate(digest, buffer, count) != 1) result = maage_status_make(MAAGE_STATUS_DEPENDENCY_FAILURE,
            MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_HASH, 0);
    }
    if (!maage_status_failed(result) && ferror(file)) result = maage_status_make(MAAGE_STATUS_IO_ERROR,
        MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_READ, 0);
    if (!maage_status_failed(result) && (EVP_DigestFinal_ex(digest, bytes, &length) != 1 || length != 20))
        result = maage_status_make(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_HASH, 0);
    if (fclose(file) && !maage_status_failed(result)) result = maage_status_from_errno(errno,
        MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_CLOSE);
    EVP_MD_CTX_free(digest);
    if (maage_status_failed(result)) return result;
    for (i = 0; i < 20; ++i) sprintf(hash + i * 2, "%02x", bytes[i]);
    hash[40] = 0;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_HASH, 0);
}
