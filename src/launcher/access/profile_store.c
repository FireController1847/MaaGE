#include "profile_store.h"
#include "verify.h"
#include "maage/storage.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAAGE_PROFILE_LIMIT 24576
#define MAAGE_PROFILE_HEADER 36
static int maage_profile_path(const char *root, const char *name, char path[MAAGE_PATH_CAPACITY])
{
    char directory[MAAGE_PATH_CAPACITY];
    return maage_storage_join(directory, sizeof(directory), root, "config/accounts") &&
        maage_storage_directories(directory) && maage_storage_join(path, MAAGE_PATH_CAPACITY, directory, name);
}
static int maage_profile_write(const char *path, const unsigned char *data, size_t size)
{
    char temporary[MAAGE_PATH_CAPACITY];
    FILE *file;
    int ok;
    if (strlen(path) + 6 >= sizeof(temporary)) return 0;
    strcpy(temporary, path); strcat(temporary, ".part");
    if (!maage_storage_remove(temporary)) return 0;
    file = maage_storage_open(temporary, "wb"); if (!file) return 0;
    ok = maage_storage_private_file(temporary) && fwrite(data, 1, size, file) == size;
    if (fclose(file)) ok = 0;
    if (ok) ok = maage_storage_replace(temporary, path);
    if (!ok) maage_storage_remove(temporary);
    return ok;
}
static int maage_profile_key(const char *root, unsigned char key[32], int create)
{
    char path[MAAGE_PATH_CAPACITY], fingerprint[128];
    double size;
    FILE *file;
    int ok;
    if (!maage_profile_path(root, "key.bin", path)) return 0;
    if (maage_storage_stat(path, &size, fingerprint)) {
        if (size != 32 || !maage_storage_private_file(path)) return 0;
        file = maage_storage_open(path, "rb"); if (!file) return 0;
        ok = fread(key, 1, 32, file) == 32 && fgetc(file) == EOF && !ferror(file);
        fclose(file); return ok;
    }
    return create && RAND_bytes(key, 32) == 1 && maage_profile_write(path, key, 32);
}
static int maage_profile_crypt(int encrypt, unsigned char *plain, int size,
    unsigned char *sealed, const unsigned char key[32], const char *uuid)
{
    EVP_CIPHER_CTX *cipher = EVP_CIPHER_CTX_new();
    int count = 0, final_count = 0, ok = 0;
    if (!cipher) return 0;
    if (encrypt) {
        memcpy(sealed, "MAAGEAC1", 8);
        if (RAND_bytes(sealed + 8, 12) != 1 ||
            EVP_EncryptInit_ex(cipher, EVP_aes_256_gcm(), NULL, key, sealed + 8) != 1 ||
            EVP_EncryptUpdate(cipher, NULL, &count, sealed, 8) != 1 ||
            EVP_EncryptUpdate(cipher, NULL, &count, (const unsigned char *)uuid, 32) != 1 ||
            EVP_EncryptUpdate(cipher, sealed + MAAGE_PROFILE_HEADER, &count, plain, size) != 1 || count != size ||
            EVP_EncryptFinal_ex(cipher, sealed + MAAGE_PROFILE_HEADER + count, &final_count) != 1 || final_count != 0 ||
            EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_GET_TAG, 16, sealed + 20) != 1) goto done;
    } else {
        if (memcmp(sealed, "MAAGEAC1", 8) ||
            EVP_DecryptInit_ex(cipher, EVP_aes_256_gcm(), NULL, key, sealed + 8) != 1 ||
            EVP_DecryptUpdate(cipher, NULL, &count, sealed, 8) != 1 ||
            EVP_DecryptUpdate(cipher, NULL, &count, (const unsigned char *)uuid, 32) != 1 ||
            EVP_DecryptUpdate(cipher, plain, &count, sealed + MAAGE_PROFILE_HEADER, size) != 1 || count != size ||
            EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_TAG, 16, sealed + 20) != 1 ||
            EVP_DecryptFinal_ex(cipher, plain + count, &final_count) != 1 || final_count != 0) goto done;
    }
    ok = 1;
done:
    EVP_CIPHER_CTX_free(cipher); return ok;
}
static double maage_profile_number(struct json_object *object, const char *key)
{
    struct json_object *value;
    double number;
    if (!json_object_object_get_ex(object, key, &value) || (!json_object_is_type(value, json_type_double) && !json_object_is_type(value, json_type_int))) return -1;
    number = json_object_get_double(value); return number >= 0 && number < 1.0e12 ? number : -1;
}
static void maage_profile_wipe_json(struct json_object *object)
{
    struct json_object *token;
    if (object && json_object_object_get_ex(object, "minecraft_access_token", &token) && json_object_is_type(token, json_type_string))
        OPENSSL_cleanse((char *)json_object_get_string(token), (size_t)json_object_get_string_len(token));
}
/* One directory lock serializes key creation, replacement and account removal. */
static maage_storage_lock *maage_profile_lock(const char *root)
{
    char path[MAAGE_PATH_CAPACITY];
    if (!maage_profile_path(root, "store.lock", path)) return NULL;
    return maage_storage_acquire(path);
}
int maage_profile_store_load(const char *root, const char *uuid, maage_profile_session *session)
{
    unsigned char key[32], *sealed = NULL, *plain = NULL;
    char name[48], normalized[33], path[MAAGE_PATH_CAPACITY];
    const char *token, *player;
    maage_storage_lock *lock = NULL;
    struct json_object *object = NULL, *version;
    FILE *file;
    size_t size;
    int ok = 0;
    memset(session, 0, sizeof(*session));
    if (!maage_access_uuid(uuid, normalized) || strcmp(uuid, normalized)) return 0;
    sprintf(name, "%s.account", uuid);
    lock = maage_profile_lock(root);
    if (!lock || !maage_profile_key(root, key, 0) || !maage_profile_path(root, name, path) || !maage_storage_private_file(path)) goto done;
    sealed = (unsigned char *)malloc(MAAGE_PROFILE_LIMIT + MAAGE_PROFILE_HEADER + 1);
    plain = (unsigned char *)calloc(MAAGE_PROFILE_LIMIT + 1, 1);
    if (!sealed || !plain) goto done;
    file = maage_storage_open(path, "rb"); if (!file) goto done;
    size = fread(sealed, 1, MAAGE_PROFILE_LIMIT + MAAGE_PROFILE_HEADER + 1, file);
    ok = !ferror(file); fclose(file);
    if (!ok || size <= MAAGE_PROFILE_HEADER || size > MAAGE_PROFILE_LIMIT + MAAGE_PROFILE_HEADER) { ok = 0; goto done; }
    size -= MAAGE_PROFILE_HEADER;
    ok = 0;
    if (!maage_profile_crypt(0, plain, (int)size, sealed, key, uuid)) goto done;
    object = maage_access_json((const char *)plain, size); if (!object) goto done;
    if (!json_object_object_get_ex(object, "version", &version) || !json_object_is_type(version, json_type_int) || json_object_get_int(version) != 1 ||
        strcmp(maage_access_json_string(object, "provider"), "minecraft-session-import") || strcmp(maage_access_json_string(object, "uuid"), uuid)) goto done;
    token = maage_access_json_string(object, "minecraft_access_token"); player = maage_access_json_string(object, "name");
    if (!*token || strlen(token) >= sizeof(session->minecraft_token) || !maage_access_username(player)) goto done;
    session->expires_at = maage_profile_number(object, "expires_at");
    session->verified_at = maage_profile_number(object, "verified_at");
    session->verified_until = maage_profile_number(object, "verified_until");
    if (session->expires_at <= 0 || session->verified_at < 0 || session->verified_until < 0 ||
        session->verified_until > session->expires_at || session->verified_until > session->verified_at + 3600.0) goto done;
    strcpy(session->uuid, uuid); strcpy(session->name, player); strcpy(session->minecraft_token, token); ok = 1;
done:
    maage_profile_wipe_json(object); if (object) json_object_put(object);
    if (plain) { OPENSSL_cleanse(plain, MAAGE_PROFILE_LIMIT + 1); free(plain); }
    if (sealed) { OPENSSL_cleanse(sealed, MAAGE_PROFILE_LIMIT + MAAGE_PROFILE_HEADER + 1); free(sealed); }
    OPENSSL_cleanse(key, sizeof(key)); maage_storage_release(lock);
    if (!ok) OPENSSL_cleanse(session, sizeof(*session));
    return ok;
}
int maage_profile_store_save(const char *root, const maage_profile_session *session)
{
    unsigned char key[32], *sealed = NULL;
    char normalized[33], name[48], path[MAAGE_PATH_CAPACITY];
    const char *plain = NULL;
    maage_storage_lock *lock = NULL;
    struct json_object *object = NULL;
    size_t size = 0;
    int ok = 0;
    if (!maage_access_uuid(session->uuid, normalized) || strcmp(session->uuid, normalized) ||
        !maage_access_username(session->name) || !*session->minecraft_token || strlen(session->minecraft_token) >= MAAGE_ACCESS_TOKEN_CAPACITY ||
        !(session->expires_at > 0 && session->expires_at < 1.0e12) || !(session->verified_at >= 0 && session->verified_at < 1.0e12) ||
        !(session->verified_until >= 0 && session->verified_until <= session->expires_at && session->verified_until <= session->verified_at + 3600.0)) return 0;
    lock = maage_profile_lock(root);
    if (!lock || !maage_profile_key(root, key, 1)) goto done;
    object = json_object_new_object(); if (!object) goto done;
    json_object_object_add(object, "version", json_object_new_int(1));
    json_object_object_add(object, "provider", json_object_new_string("minecraft-session-import"));
    json_object_object_add(object, "uuid", json_object_new_string(session->uuid));
    json_object_object_add(object, "name", json_object_new_string(session->name));
    json_object_object_add(object, "minecraft_access_token", json_object_new_string(session->minecraft_token));
    json_object_object_add(object, "expires_at", json_object_new_double(session->expires_at));
    json_object_object_add(object, "verified_at", json_object_new_double(session->verified_at));
    json_object_object_add(object, "verified_until", json_object_new_double(session->verified_until));
    if (maage_profile_number(object, "version") != 1 ||
        strcmp(maage_access_json_string(object, "provider"), "minecraft-session-import") ||
        strcmp(maage_access_json_string(object, "name"), session->name) ||
        strcmp(maage_access_json_string(object, "minecraft_access_token"), session->minecraft_token) ||
        strcmp(maage_access_json_string(object, "uuid"), session->uuid) ||
        maage_profile_number(object, "expires_at") != session->expires_at ||
        maage_profile_number(object, "verified_at") != session->verified_at ||
        maage_profile_number(object, "verified_until") != session->verified_until) goto done;
    plain = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN); if (!plain) goto done;
    size = strlen(plain); if (size > MAAGE_PROFILE_LIMIT) goto done;
    sealed = (unsigned char *)malloc(size + MAAGE_PROFILE_HEADER + EVP_MAX_BLOCK_LENGTH); if (!sealed) goto done;
    sprintf(name, "%s.account", session->uuid);
    ok = maage_profile_crypt(1, (unsigned char *)plain, (int)size, sealed, key, session->uuid) &&
        maage_profile_path(root, name, path) && maage_profile_write(path, sealed, size + MAAGE_PROFILE_HEADER);
done:
    if (plain) OPENSSL_cleanse((char *)plain, size);
    maage_profile_wipe_json(object); if (object) json_object_put(object);
    if (sealed) { OPENSSL_cleanse(sealed, size + MAAGE_PROFILE_HEADER + EVP_MAX_BLOCK_LENGTH); free(sealed); }
    OPENSSL_cleanse(key, sizeof(key)); maage_storage_release(lock); return ok;
}
int maage_profile_store_remove(const char *root, const char *uuid)
{
    char name[48], normalized[33], path[MAAGE_PATH_CAPACITY];
    maage_storage_lock *lock;
    int ok;
    if (!maage_access_uuid(uuid, normalized) || strcmp(uuid, normalized)) return 0;
    lock = maage_profile_lock(root); if (!lock) return 0;
    sprintf(name, "%s.account", uuid);
    ok = maage_profile_path(root, name, path) && maage_storage_remove(path);
    maage_storage_release(lock); return ok;
}
