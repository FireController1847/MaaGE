#include "profile_store.h"
#include "verify.h"
#include "oauth.h"
#include "maage/storage.h"
#include "maage/crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define MAAGE_PROFILE_LIMIT 100000
#define MAAGE_PROFILE_HEADER 36

static maage_status maage_profile_io(maage_status_trace *trace, unsigned char operation) {
    int native = errno;
    maage_status result = maage_status_from_errno(native ? native : EIO, MAAGE_CONTEXT_STORAGE, operation);
    return maage_status_trace_record(trace, result, MAAGE_CONTEXT_STORAGE, operation, __FILE__, (unsigned long) __LINE__, (long) native);
}
static maage_status maage_profile_path(const char *root, const char *name, char path[MAAGE_PATH_CAPACITY], maage_status_trace *trace) {
    char directory[MAAGE_PATH_CAPACITY];
    maage_status result = maage_storage_join(directory, sizeof(directory), root, "config/accounts");
    if (!maage_status_failed(result)) result = maage_storage_directories(directory);
    if (!maage_status_failed(result)) result = maage_storage_join(path, MAAGE_PATH_CAPACITY, directory, name);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_RESOLVE);
}
static maage_status maage_profile_write(const char *path, const unsigned char *data, size_t size, maage_status_trace *trace) {
    char temporary[MAAGE_PATH_CAPACITY];
    FILE *file;
    maage_status result, cleanup;
    if (strlen(path) + 6 >= sizeof(temporary)) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SAVE, 0), MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SAVE);
    strcpy(temporary, path);
    strcat(temporary, ".part");
    result = maage_storage_remove(temporary);
    if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SAVE);
    file = maage_storage_open(temporary, "wb");
    if (!file) return MAAGE_STATUS_PROPAGATE(trace, maage_profile_io(trace, MAAGE_OPERATION_OPEN), MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SAVE);
    result = maage_storage_private_file(temporary);
    if (!maage_status_failed(result) && fwrite(data, 1, size, file) != size) result = maage_profile_io(trace, MAAGE_OPERATION_WRITE);
    if (!maage_status_failed(result)) result = maage_storage_sync(file);
    if (fclose(file) && !maage_status_failed(result)) result = maage_profile_io(trace, MAAGE_OPERATION_CLOSE);
    if (!maage_status_failed(result)) result = maage_storage_replace(temporary, path);
    if (maage_status_failed(result)) {
        MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SAVE);
        cleanup = maage_storage_remove(temporary);
        MAAGE_STATUS_PROPAGATE(trace, cleanup, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_REMOVE);
    }
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SAVE);
}
static maage_status maage_profile_key(const char *root, unsigned char key[32], int create, maage_status_trace *trace) {
    char path[MAAGE_PATH_CAPACITY], fingerprint[128];
    double size;
    FILE *file;
    maage_status result;
    result = maage_profile_path(root, "key.bin", path, trace);
    if (maage_status_failed(result)) return result;
    result = maage_storage_stat(path, &size, fingerprint);
    if (!maage_status_failed(result)) {
        if (size != 32) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN, 0), MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN);
        result = maage_storage_private_file(path);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN);
        file = maage_storage_open(path, "rb");
        if (!file) return MAAGE_STATUS_PROPAGATE(trace, maage_profile_io(trace, MAAGE_OPERATION_OPEN), MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN);
        if (fread(key, 1, 32, file) != 32 || fgetc(file) != EOF) result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_READ, 0);
        if (ferror(file)) result = maage_profile_io(trace, MAAGE_OPERATION_READ);
        if (fclose(file) && !maage_status_failed(result)) result = maage_profile_io(trace, MAAGE_OPERATION_CLOSE);
        return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN);
    }
    /* Never replace an inaccessible or malformed existing encryption key. */
    if (!create || maage_status_code(result) != MAAGE_STATUS_NOT_FOUND) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN);
    result = maage_auth_random(key, 32);
    if (!maage_status_failed(result)) result = maage_profile_write(path, key, 32, trace);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_CREATE);
}
static maage_status maage_profile_crypt(int encrypt, unsigned char *plain, int size, unsigned char *sealed, const unsigned char key[32], const char *uuid, maage_status_trace *trace) {
    unsigned char aad[40];
    maage_status result;
    if (encrypt) {
        memcpy(sealed, "MAAGEAC2", 8);
        result = maage_auth_random(sealed + 8, 12);
        if (maage_status_failed(result)) goto done;
    } else {
        if (memcmp(sealed, "MAAGEAC2", 8)) {
            result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_DECODE, 0);
            goto done;
        }
    }
    memcpy(aad, sealed, 8);
    memcpy(aad + 8, uuid, 32);
    result = maage_crypto_aes_gcm(encrypt, key, sealed + 8, sealed + 20, aad, sizeof(aad),
                                  encrypt ? plain : sealed + MAAGE_PROFILE_HEADER, (size_t) size,
                                  encrypt ? sealed + MAAGE_PROFILE_HEADER : plain);
done:
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_AUTH, encrypt ? MAAGE_OPERATION_SAVE : MAAGE_OPERATION_DECODE);
}
static double maage_profile_number(struct json_object *object, const char *key) {
    struct json_object *value;
    double number;
    if (!json_object_object_get_ex(object, key, &value) || (!json_object_is_type(value, json_type_double) && !json_object_is_type(value, json_type_int))) return -1;
    number = json_object_get_double(value);
    return number >= 0 && number < 1.0e12 ? number : -1;
}
/* One directory lock serializes key creation, replacement and account removal. */
static maage_status maage_profile_lock(const char *root, maage_storage_lock **output, maage_status_trace *trace) {
    char path[MAAGE_PATH_CAPACITY];
    maage_status result;
    *output = NULL;
    result = maage_profile_path(root, "store.lock", path, trace);
    if (!maage_status_failed(result)) result = maage_storage_acquire(path, output, trace);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_ACQUIRE);
}
/* Keep the original operation outcome if cleanup also fails; successful
 * publication/load with a failed lease release is an explicit partial result. */
static maage_status maage_profile_unlock(maage_storage_lock *lock, maage_status result, unsigned char operation, maage_status_trace *trace) {
    maage_status cleanup = maage_storage_release(lock);
    if (maage_status_failed(cleanup)) {
        MAAGE_STATUS_PROPAGATE(trace, cleanup, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_RELEASE);
        if (!maage_status_failed(result)) result = maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_AUTH, operation, 0);
    }
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_AUTH, operation);
}
maage_status maage_profile_store_load(const char *root, const char *uuid, maage_profile_session *session, maage_status_trace *trace) {
    unsigned char key[32], *sealed = NULL, *plain = NULL;
    char name[48], normalized[33], path[MAAGE_PATH_CAPACITY];
    const char *token, *player;
    maage_storage_lock *lock = NULL;
    struct json_object *object = NULL, *version;
    FILE *file;
    size_t size;
    maage_status result;
    if (session) memset(session, 0, sizeof(*session));
    if (!root || !uuid || !session || !maage_access_uuid(uuid, normalized) || strcmp(uuid, normalized)) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_LOAD, MAAGE_DETAIL_ARGUMENT_UNKNOWN), MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_LOAD);
    sprintf(name, "%s.account", uuid);
    result = maage_profile_lock(root, &lock, trace);
    if (maage_status_failed(result)) goto done;
    result = maage_profile_key(root, key, 0, trace);
    if (!maage_status_failed(result)) result = maage_profile_path(root, name, path, trace);
    if (!maage_status_failed(result)) result = maage_storage_private_file(path);
    if (maage_status_failed(result)) goto done;
    sealed = (unsigned char *) malloc(MAAGE_PROFILE_LIMIT + MAAGE_PROFILE_HEADER + 1);
    plain = (unsigned char *) calloc(MAAGE_PROFILE_LIMIT + 1, 1);
    if (!sealed || !plain) {
        result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_LOAD, 0);
        goto done;
    }
    file = maage_storage_open(path, "rb");
    if (!file) {
        result = maage_profile_io(trace, MAAGE_OPERATION_OPEN);
        goto done;
    }
    size = fread(sealed, 1, MAAGE_PROFILE_LIMIT + MAAGE_PROFILE_HEADER + 1, file);
    if (ferror(file)) result = maage_profile_io(trace, MAAGE_OPERATION_READ);
    if (fclose(file) && !maage_status_failed(result)) result = maage_profile_io(trace, MAAGE_OPERATION_CLOSE);
    if (maage_status_failed(result)) goto done;
    if (size <= MAAGE_PROFILE_HEADER || size > MAAGE_PROFILE_LIMIT + MAAGE_PROFILE_HEADER) {
        result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_LOAD, 0);
        goto done;
    }
    size -= MAAGE_PROFILE_HEADER;
    result = maage_profile_crypt(0, plain, (int) size, sealed, key, uuid, trace);
    if (maage_status_failed(result)) goto done;
    result = maage_access_json((const char *) plain, size, &object);
    if (maage_status_failed(result)) goto done;
    result = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_PARSE, 0);
    if (!object || !json_object_object_get_ex(object, "version", &version) || !json_object_is_type(version, json_type_int) || json_object_get_int(version) != 2 || strcmp(maage_access_json_string(object, "provider"), "microsoft-oauth") || strcmp(maage_access_json_string(object, "uuid"), uuid)) goto done;
    token = maage_access_json_string(object, "minecraft_access_token");
    player = maage_access_json_string(object, "name");
    if (!*token || strlen(token) >= sizeof(session->minecraft_token) || !maage_access_username(player)) goto done;
    session->expires_at = maage_profile_number(object, "expires_at");
    session->verified_at = maage_profile_number(object, "verified_at");
    session->verified_until = maage_profile_number(object, "verified_until");
    if (session->expires_at <= 0 || session->verified_at < 0 || session->verified_until < 0 ||
        session->verified_until > session->expires_at || session->verified_until > session->verified_at + 3600.0 ||
        strcmp(maage_access_json_string(object, "client_id"), MAAGE_MICROSOFT_CLIENT_ID)) goto done;
    result = maage_auth_token(session->microsoft_refresh, sizeof(session->microsoft_refresh), object, "microsoft_refresh_token");
    if (maage_status_failed(result)) goto done;
    strcpy(session->uuid, uuid);
    strcpy(session->name, player);
    strcpy(session->minecraft_token, token);
    result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_LOAD, 0);
done:
    MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_LOAD);
    maage_auth_json_free(object);
    if (plain) {
        maage_crypto_clear(plain, MAAGE_PROFILE_LIMIT + 1);
        free(plain);
    }
    if (sealed) {
        maage_crypto_clear(sealed, MAAGE_PROFILE_LIMIT + MAAGE_PROFILE_HEADER + 1);
        free(sealed);
    }
    maage_crypto_clear(key, sizeof(key));
    if (maage_status_failed(result)) maage_crypto_clear(session, sizeof(*session));
    return maage_profile_unlock(lock, result, MAAGE_OPERATION_LOAD, trace);
}
static maage_status maage_profile_member(struct json_object *object, const char *key, struct json_object *value) {
    if (!value) return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SAVE, 0);
    if (json_object_object_add(object, key, value)) {
        maage_auth_json_free(value);
        return maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SAVE, 0);
    }
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_SAVE, 0);
}
maage_status maage_profile_store_save(const char *root, const maage_profile_session *session, maage_status_trace *trace) {
    unsigned char key[32], *sealed = NULL;
    char normalized[33], name[48], path[MAAGE_PATH_CAPACITY];
    const char *plain = NULL;
    maage_storage_lock *lock = NULL;
    struct json_object *object = NULL;
    size_t size = 0;
    maage_status result;
    if (!root || !session || !maage_access_uuid(session->uuid, normalized) || strcmp(session->uuid, normalized) ||
        !maage_access_username(session->name) || !*session->microsoft_refresh || !*session->minecraft_token || strlen(session->minecraft_token) >= MAAGE_ACCESS_TOKEN_CAPACITY ||
        !(session->expires_at > 0 && session->expires_at < 1.0e12) || !(session->verified_at >= 0 && session->verified_at < 1.0e12) ||
        !(session->verified_until >= 0 && session->verified_until <= session->expires_at && session->verified_until <= session->verified_at + 3600.0))
        return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SAVE, MAAGE_DETAIL_ARGUMENT_UNKNOWN), MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SAVE);
    result = maage_profile_lock(root, &lock, trace);
    if (maage_status_failed(result)) goto done;
    result = maage_profile_key(root, key, 1, trace);
    if (maage_status_failed(result)) goto done;
    object = json_object_new_object();
    if (!object) {
        result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SAVE, 0);
        goto done;
    }
    result = maage_profile_member(object, "version", json_object_new_int(2));
    if (!maage_status_failed(result)) result = maage_profile_member(object, "provider", json_object_new_string("microsoft-oauth"));
    if (!maage_status_failed(result)) result = maage_profile_member(object, "uuid", json_object_new_string(session->uuid));
    if (!maage_status_failed(result)) result = maage_profile_member(object, "name", json_object_new_string(session->name));
    if (!maage_status_failed(result)) result = maage_profile_member(object, "client_id", json_object_new_string(MAAGE_MICROSOFT_CLIENT_ID));
    if (!maage_status_failed(result)) result = maage_profile_member(object, "microsoft_refresh_token", json_object_new_string(session->microsoft_refresh));
    if (!maage_status_failed(result)) result = maage_profile_member(object, "minecraft_access_token", json_object_new_string(session->minecraft_token));
    if (!maage_status_failed(result)) result = maage_profile_member(object, "expires_at", json_object_new_double(session->expires_at));
    if (!maage_status_failed(result)) result = maage_profile_member(object, "verified_at", json_object_new_double(session->verified_at));
    if (!maage_status_failed(result)) result = maage_profile_member(object, "verified_until", json_object_new_double(session->verified_until));
    if (maage_status_failed(result)) goto done;
    plain = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    if (!plain) {
        result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SAVE, 0);
        goto done;
    }
    size = strlen(plain);
    if (size > MAAGE_PROFILE_LIMIT) {
        result = maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SAVE, 0);
        goto done;
    }
    sealed = (unsigned char *) malloc(size + MAAGE_PROFILE_HEADER);
    if (!sealed) {
        result = maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SAVE, 0);
        goto done;
    }
    sprintf(name, "%s.account", session->uuid);
    result = maage_profile_crypt(1, (unsigned char *) plain, (int) size, sealed, key, session->uuid, trace);
    if (!maage_status_failed(result)) result = maage_profile_path(root, name, path, trace);
    if (!maage_status_failed(result)) result = maage_profile_write(path, sealed, size + MAAGE_PROFILE_HEADER, trace);
done:
    MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_SAVE);
    maage_auth_json_free(object);
    if (sealed) {
        maage_crypto_clear(sealed, size + MAAGE_PROFILE_HEADER);
        free(sealed);
    }
    maage_crypto_clear(key, sizeof(key));
    return maage_profile_unlock(lock, result, MAAGE_OPERATION_SAVE, trace);
}
maage_status maage_profile_store_remove(const char *root, const char *uuid, maage_status_trace *trace) {
    char name[48], normalized[33], path[MAAGE_PATH_CAPACITY];
    maage_storage_lock *lock = NULL;
    maage_status result;
    if (!root || !uuid || !maage_access_uuid(uuid, normalized) || strcmp(uuid, normalized)) return MAAGE_STATUS_PROPAGATE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_REMOVE, MAAGE_DETAIL_ARGUMENT_UNKNOWN), MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_REMOVE);
    result = maage_profile_lock(root, &lock, trace);
    if (!maage_status_failed(result)) {
        sprintf(name, "%s.account", uuid);
        result = maage_profile_path(root, name, path, trace);
        if (!maage_status_failed(result)) result = maage_storage_remove(path);
    }
    MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_REMOVE);
    return maage_profile_unlock(lock, result, MAAGE_OPERATION_REMOVE, trace);
}
