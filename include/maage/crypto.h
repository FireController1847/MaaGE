#ifndef MAAGE_CRYPTO_H
#define MAAGE_CRYPTO_H

#include "maage/status.h"
#include <stddef.h>

typedef struct maage_crypto_hash maage_crypto_hash;
enum {
    MAAGE_CRYPTO_SHA1 = 1,
    MAAGE_CRYPTO_SHA256 = 2
};

/* Hashes own their backend context until destroy; finish consumes the digest. */
maage_status maage_crypto_hash_open(unsigned algorithm, maage_crypto_hash **output);
maage_status maage_crypto_hash_update(maage_crypto_hash *hash, const void *data, size_t size);
maage_status maage_crypto_hash_finish(maage_crypto_hash *hash, unsigned char *output, size_t size);
void maage_crypto_hash_destroy(maage_crypto_hash *hash);
maage_status maage_crypto_digest(unsigned algorithm, const void *data, size_t size, unsigned char *output, size_t output_size);

/* AES-256-GCM: 12-byte nonce, 16-byte tag, output exactly size bytes.
 * Decryption publishes no usable plaintext on authentication failure. */
maage_status maage_crypto_aes_gcm(int encrypt, const unsigned char key[32], const unsigned char nonce[12], unsigned char tag[16], const void *aad, size_t aad_size, const unsigned char *input, size_t size, unsigned char *output);
maage_status maage_crypto_base64url(const unsigned char *input, size_t size, char *output, size_t capacity);

/* Security primitives: clearing cannot be optimized away; equality is constant
 * time with respect to contents (the caller may compare public lengths first). */
void maage_crypto_clear(void *data, size_t size);
int maage_crypto_equal(const void *left, const void *right, size_t size);

#endif
