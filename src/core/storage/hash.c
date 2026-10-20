#include "maage/storage.h"
#include <openssl/evp.h>

int maage_storage_sha1(const char *path, char hash[41], int (*cancel)(void *), void *context)
{
    FILE *file = maage_storage_open(path, "rb");
    EVP_MD_CTX *digest;
    unsigned char buffer[32768], bytes[EVP_MAX_MD_SIZE];
    unsigned int length, i;
    size_t n;
    int ok = 1;
    if (!file) return 0;
    digest = EVP_MD_CTX_new();
    if (!digest || EVP_DigestInit_ex(digest, EVP_sha1(), NULL) != 1) ok = 0;
    while (ok && (n = fread(buffer, 1, sizeof(buffer), file)) != 0) {
        if ((cancel && cancel(context)) || EVP_DigestUpdate(digest, buffer, n) != 1) ok = 0;
    }
    if (ferror(file)) ok = 0;
    if (ok && (EVP_DigestFinal_ex(digest, bytes, &length) != 1 || length != 20)) ok = 0;
    fclose(file); EVP_MD_CTX_free(digest);
    if (!ok) return 0;
    for (i = 0; i < 20; ++i) sprintf(hash + i * 2, "%02x", bytes[i]);
    hash[40] = 0; return 1;
}
