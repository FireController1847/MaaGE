#ifndef MAAGE_AUTH_INTERNAL_H
#define MAAGE_AUTH_INTERNAL_H
#include "auth.h"
#include <stddef.h>
#define MAAGE_AUTH_MAX_ACCOUNTS 32
#define MAAGE_AUTH_TOKEN_SIZE 16384
#define MAAGE_AUTH_RESPONSE_LIMIT 262144UL

typedef struct maage_auth_runtime maage_auth_runtime;
typedef struct json_object maage_auth_json;
typedef enum maage_auth_error {
    MAAGE_AUTH_OK, MAAGE_AUTH_CANCELLED, MAAGE_AUTH_NETWORK, MAAGE_AUTH_PROTOCOL,
    MAAGE_AUTH_REAUTH, MAAGE_AUTH_RESTRICTED, MAAGE_AUTH_CONFIGURATION,
    MAAGE_AUTH_STORAGE, MAAGE_AUTH_BROWSER, MAAGE_AUTH_PROFILE, MAAGE_AUTH_SESSION, MAAGE_AUTH_EXPIRED, MAAGE_AUTH_LIMIT, MAAGE_AUTH_DENIED
} maage_auth_error;
typedef struct maage_auth_record {
    char id[40], name[64], uuid[33], client_id[37];
    char refresh[MAAGE_AUTH_TOKEN_SIZE], minecraft[MAAGE_AUTH_TOKEN_SIZE];
    unsigned char avatar[8 * 8 * 4];
    int avatar_valid;
    double expires_at;
    int microsoft_valid, entitled, owns_java, profile_valid, session_only;
    maage_auth_error error;
} maage_auth_record;
typedef struct maage_auth_config {
    char client_id[37];
    const char *device, *token, *xbox, *xsts, *minecraft;
} maage_auth_config;
struct maage_auth {
    maage_auth_runtime *runtime;
    maage_launcher_state *app;
    maage_auth_config config;
    maage_auth_record records[MAAGE_AUTH_MAX_ACCOUNTS], candidate;
    size_t count;
    char selected[40];
    maage_launcher_action command;
    char target[40];
    int busy, cancelled, stop, changed, decision, candidate_pending;
    char progress[256], browser_url[4096];
    maage_auth_error error;
    const char *response_stage;
    long response_status;
    char response_reason[128];
    double retry_at, device_deadline;
    int dialog_active, copy_requested;
    char user_code[64], verification_uri[512], link_hint[128];
    /* These copies belong exclusively to the UI/application thread. */
    maage_launcher_account views[MAAGE_AUTH_MAX_ACCOUNTS];
    char ids[MAAGE_AUTH_MAX_ACCOUNTS][40], names[MAAGE_AUTH_MAX_ACCOUNTS][64], uuids[MAAGE_AUTH_MAX_ACCOUNTS][33];
    unsigned char avatars[MAAGE_AUTH_MAX_ACCOUNTS][8 * 8 * 4];
    maage_pixel_buffer avatar_views[MAAGE_AUTH_MAX_ACCOUNTS];
    char statuses[MAAGE_AUTH_MAX_ACCOUNTS][96], ui_selected[40], ui_message[256];
    char ui_code[64], ui_uri[512], ui_hint[128], ui_feedback[256];
};
int maage_auth_config_init(maage_auth_config *config);
void maage_auth_wipe(void *memory, size_t size);
int maage_auth_copy(char *output, size_t capacity, const char *input);
const char *maage_auth_message(maage_auth_error error);
int maage_auth_cancelled(maage_auth *auth);
void maage_auth_progress(maage_auth *auth, const char *message);
void maage_auth_worker(void *context);
maage_auth_error maage_auth_oauth(maage_auth *auth, maage_auth_record *record, char *access);
maage_auth_error maage_auth_xbox(maage_auth *auth, const char *access, char *token, char *hash);
maage_auth_error maage_auth_minecraft(maage_auth *auth, maage_auth_record *record, const char *token, const char *hash);

/* Platform-private synchronization, OS crypto, browser and credential adapters. */
maage_auth_runtime *maage_auth_runtime_create(void);
void maage_auth_lock(maage_auth_runtime *runtime);
void maage_auth_unlock(maage_auth_runtime *runtime);
int maage_auth_thread_start(maage_auth_runtime *runtime, void (*work)(void *), void *context);
void maage_auth_wake(maage_auth_runtime *runtime);
void maage_auth_wait(maage_auth_runtime *runtime);
void maage_auth_runtime_destroy(maage_auth_runtime *runtime);
int maage_auth_random(unsigned char *bytes, size_t size);
void maage_auth_sleep_tick(void);
int maage_auth_copy_code(const char *code);
int maage_auth_device_expired(maage_auth *auth);
maage_auth_error maage_auth_device(maage_auth *auth, maage_auth_json **tokens);
int maage_auth_open_browser(maage_auth_runtime *runtime, const char *url);
void maage_auth_browser_poll(maage_auth_runtime *runtime);
/* 1 success, 0 not found, -1 unavailable/error. Only refresh tokens enter vaults. */
int maage_auth_secret_get(const char *key, char *output, size_t capacity);
int maage_auth_secret_set(const char *key, const char *value);
int maage_auth_secret_delete(const char *key);
int maage_auth_metadata_read(char **output);
int maage_auth_metadata_write(const char *data);

/* Bounded HTTPS transport; never follows redirects or logs payloads/headers. */
int maage_auth_http_init(void);
void maage_auth_http_cleanup(void);
maage_auth_error maage_auth_http(maage_auth *auth, const char *stage, const char *url, const char *type, const char *body, const char *bearer, long *status, maage_auth_json **response);
maage_auth_error maage_auth_http_bytes(maage_auth *auth, const char *url, const char *type, const char *body, const char *bearer, long *status, char **bytes, size_t *length);
void maage_auth_avatar(maage_auth *auth, maage_auth_record *record, maage_auth_json *profile);
char *maage_auth_escape(const char *text);
void maage_auth_free_secret(char *text);
maage_auth_json *maage_auth_json_parse(const char *text);
maage_auth_json *maage_auth_json_object(void);
maage_auth_json *maage_auth_json_array(void);
void maage_auth_json_put(maage_auth_json *json);
void maage_auth_json_add(maage_auth_json *json, const char *key, maage_auth_json *value);
void maage_auth_json_string(maage_auth_json *json, const char *key, const char *text);
void maage_auth_json_number(maage_auth_json *json, const char *key, double value);
void maage_auth_json_append(maage_auth_json *json, maage_auth_json *value);
void maage_auth_json_array_string(maage_auth_json *json, const char *text);
maage_auth_json *maage_auth_json_get(maage_auth_json *json, const char *key);
maage_auth_json *maage_auth_json_at(maage_auth_json *json, size_t index);
const char *maage_auth_json_text(maage_auth_json *json, const char *key);
double maage_auth_json_double(maage_auth_json *json, const char *key);
size_t maage_auth_json_count(maage_auth_json *json);
int maage_auth_json_is_array(maage_auth_json *json);
char *maage_auth_json_encode(maage_auth_json *json);
int maage_auth_store_load(maage_auth *auth);
int maage_auth_store_save(maage_auth *auth);
void maage_auth_store_key(const maage_auth_record *record, char key[128]);
#endif
