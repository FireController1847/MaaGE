#include "maage/crypto.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/crypto.h>
#endif

struct maage_crypto_hash {
    unsigned length;
    int finished;
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm;
    BCRYPT_HASH_HANDLE handle;
#else
    EVP_MD_CTX *handle;
#endif
};

static maage_status maage_crypto_result(unsigned char code, unsigned char operation) {
    return maage_status_make(code, MAAGE_CONTEXT_CORE, operation, 0);
}

void maage_crypto_clear(void *data, size_t size) {
    if (!size) return;
#ifdef _WIN32
    SecureZeroMemory(data, size);
#else
    OPENSSL_cleanse(data, size);
#endif
}

int maage_crypto_equal(const void *left, const void *right, size_t size) {
    const volatile unsigned char *a = (const volatile unsigned char *) left;
    const volatile unsigned char *b = (const volatile unsigned char *) right;
    unsigned difference = 0;
    size_t i;
    for (i = 0; i < size; ++i)
        difference |= a[i] ^ b[i];
    return difference == 0;
}

void maage_crypto_hash_destroy(maage_crypto_hash *hash) {
    if (!hash) return;
#ifdef _WIN32
    if (hash->handle) BCryptDestroyHash(hash->handle);
    if (hash->algorithm) BCryptCloseAlgorithmProvider(hash->algorithm, 0);
#else
    EVP_MD_CTX_free(hash->handle);
#endif
    maage_crypto_clear(hash, sizeof(*hash));
    free(hash);
}

maage_status maage_crypto_hash_open(unsigned algorithm, maage_crypto_hash **output) {
    maage_crypto_hash *hash;
    int ok;
    if (output) *output = NULL;
    if (!output || (algorithm != MAAGE_CRYPTO_SHA1 && algorithm != MAAGE_CRYPTO_SHA256)) return maage_crypto_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_HASH);
    hash = (maage_crypto_hash *) calloc(1, sizeof(*hash));
    if (!hash) return maage_crypto_result(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_HASH);
    hash->length = algorithm == MAAGE_CRYPTO_SHA1 ? 20 : 32;
#ifdef _WIN32
    ok = BCryptOpenAlgorithmProvider(&hash->algorithm, algorithm == MAAGE_CRYPTO_SHA1 ? BCRYPT_SHA1_ALGORITHM : BCRYPT_SHA256_ALGORITHM, NULL, 0) >= 0;
    if (ok) ok = BCryptCreateHash(hash->algorithm, &hash->handle, NULL, 0, NULL, 0, 0) >= 0;
#else
    hash->handle = EVP_MD_CTX_new();
    ok = hash->handle && EVP_DigestInit_ex(hash->handle, algorithm == MAAGE_CRYPTO_SHA1 ? EVP_sha1() : EVP_sha256(), NULL) == 1;
#endif
    if (!ok) {
        maage_crypto_hash_destroy(hash);
        return maage_crypto_result(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_OPERATION_HASH);
    }
    *output = hash;
    return maage_crypto_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_HASH);
}

maage_status maage_crypto_hash_update(maage_crypto_hash *hash, const void *data, size_t size) {
    int ok;
#ifdef _WIN32
    const unsigned char *cursor = (const unsigned char *) data;
    ULONG count;
#endif
    if (!hash || hash->finished || (!data && size))
        return maage_crypto_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_HASH);
#ifdef _WIN32
    ok = 1;
    while (size && ok) {
        count = size > ULONG_MAX ? ULONG_MAX : (ULONG) size;
        ok = BCryptHashData(hash->handle, (PUCHAR) cursor, count, 0) >= 0;
        cursor += count;
        size -= count;
    }
#else
    ok = !size || EVP_DigestUpdate(hash->handle, data, size) == 1;
#endif
    if (!ok) hash->finished = 1;
    return maage_crypto_result(ok ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_OPERATION_HASH);
}

maage_status maage_crypto_hash_finish(maage_crypto_hash *hash, unsigned char *output, size_t size) {
    int ok;
#ifndef _WIN32
    unsigned length = 0;
#endif
    if (!hash || hash->finished || !output || size != hash->length)
        return maage_crypto_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_HASH);
    hash->finished = 1;
#ifdef _WIN32
    ok = BCryptFinishHash(hash->handle, output, hash->length, 0) >= 0;
#else
    ok = EVP_DigestFinal_ex(hash->handle, output, &length) == 1 && length == hash->length;
#endif
    if (!ok) maage_crypto_clear(output, size);
    return maage_crypto_result(ok ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_OPERATION_HASH);
}

maage_status maage_crypto_digest(unsigned algorithm, const void *data, size_t size, unsigned char *output, size_t output_size) {
    maage_crypto_hash *hash = NULL;
    maage_status result = maage_crypto_hash_open(algorithm, &hash);
    if (!maage_status_failed(result)) result = maage_crypto_hash_update(hash, data, size);
    if (!maage_status_failed(result)) result = maage_crypto_hash_finish(hash, output, output_size);
    maage_crypto_hash_destroy(hash);
    return result;
}

maage_status maage_crypto_aes_gcm(int encrypt, const unsigned char key[32], const unsigned char nonce[12], unsigned char tag[16], const void *aad, size_t aad_size, const unsigned char *input, size_t size, unsigned char *output) {
    maage_status result = maage_crypto_result(MAAGE_STATUS_DEPENDENCY_FAILURE, MAAGE_OPERATION_UPDATE);
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_KEY_HANDLE cipher = NULL;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    NTSTATUS native;
    ULONG count = 0;
#else
    EVP_CIPHER_CTX *cipher;
    unsigned char final[16];
    int count = 0, final_count = 0;
#endif
    if (!key || !nonce || !tag || !input || !output || (!aad && aad_size) || size > INT_MAX || aad_size > INT_MAX)
        return maage_crypto_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_UPDATE);
#ifdef _WIN32
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, NULL, 0) < 0) goto done;
    if (BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, (PUCHAR) BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0) < 0) goto done;
    if (BCryptGenerateSymmetricKey(algorithm, &cipher, NULL, 0, (PUCHAR) key, 32, 0) < 0) goto done;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = (PUCHAR) nonce;
    info.cbNonce = 12;
    info.pbAuthData = (PUCHAR) aad;
    info.cbAuthData = (ULONG) aad_size;
    info.pbTag = tag;
    info.cbTag = 16;
    if (encrypt) native = BCryptEncrypt(cipher, (PUCHAR) input, (ULONG) size, &info, NULL, 0, output, (ULONG) size, &count, 0);
    else
        native = BCryptDecrypt(cipher, (PUCHAR) input, (ULONG) size, &info, NULL, 0, output, (ULONG) size, &count, 0);
    if (native >= 0 && count == size) result = maage_crypto_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
    else if (!encrypt && native == (NTSTATUS) 0xC000A002L) /* STATUS_AUTH_TAG_MISMATCH */
        result = maage_crypto_result(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_VERIFY);
done:
    if (cipher) BCryptDestroyKey(cipher);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
#else
    cipher = EVP_CIPHER_CTX_new();
    if (!cipher) return maage_crypto_result(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_UPDATE);
    if (encrypt) {
        if (EVP_EncryptInit_ex(cipher, EVP_aes_256_gcm(), NULL, key, nonce) != 1 ||
            EVP_EncryptUpdate(cipher, NULL, &count, (const unsigned char *) aad, (int) aad_size) != 1 ||
            EVP_EncryptUpdate(cipher, output, &count, input, (int) size) != 1 || count != (int) size ||
            EVP_EncryptFinal_ex(cipher, final, &final_count) != 1 || final_count != 0 ||
            EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1) goto done;
    } else {
        if (EVP_DecryptInit_ex(cipher, EVP_aes_256_gcm(), NULL, key, nonce) != 1 ||
            EVP_DecryptUpdate(cipher, NULL, &count, (const unsigned char *) aad, (int) aad_size) != 1 ||
            EVP_DecryptUpdate(cipher, output, &count, input, (int) size) != 1 || count != (int) size ||
            EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_TAG, 16, tag) != 1) goto done;
        if (EVP_DecryptFinal_ex(cipher, final, &final_count) != 1 || final_count != 0) {
            result = maage_crypto_result(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_OPERATION_VERIFY);
            goto done;
        }
    }
    result = maage_crypto_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_UPDATE);
done:
    EVP_CIPHER_CTX_free(cipher);
    maage_crypto_clear(final, sizeof(final));
#endif
    if (maage_status_failed(result)) maage_crypto_clear(output, size);
    return result;
}

maage_status maage_crypto_base64url(const unsigned char *input, size_t size, char *output, size_t capacity) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t i, used = 0, length;
    unsigned a, b, c;
    if (!output || (!input && size) || size > ((size_t) - 1 - 4) / 4 * 3) return maage_crypto_result(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_FORMAT);
    length = size / 3 * 4 + (size % 3 ? size % 3 + 1 : 0);
    if (capacity <= length) return maage_crypto_result(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_FORMAT);
    for (i = 0; i < size;) {
        a = input[i++];
        b = i < size ? input[i++] : 0;
        c = i < size ? input[i++] : 0;
        output[used++] = alphabet[a >> 2];
        output[used++] = alphabet[((a & 3) << 4) | (b >> 4)];
        if (used < length) output[used++] = alphabet[((b & 15) << 2) | (c >> 6)];
        if (used < length) output[used++] = alphabet[c & 63];
    }
    output[used] = 0;
    return maage_crypto_result(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FORMAT);
}
