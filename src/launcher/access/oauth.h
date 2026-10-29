#ifndef MAAGE_LAUNCHER_OAUTH_H
#define MAAGE_LAUNCHER_OAUTH_H
#include "profile_store.h"
struct json_object;
#define MAAGE_MICROSOFT_CLIENT_ID "ff5d912a-45ca-409e-a364-9d1bf9512f61"
#define MAAGE_MICROSOFT_REDIRECT "http://localhost"
#define MAAGE_MICROSOFT_AUTHORITY "https://login.microsoftonline.com/consumers/oauth2/v2.0/"
#define MAAGE_MICROSOFT_SCOPE "XboxLive.signin offline_access"
typedef struct maage_auth_observer {
    int (*cancelled)(void *context);
    void (*status)(void *context, const char *text);
    void (*device)(void *context, const char *url, const char *code, double deadline);
    void *context;
    maage_status_trace *trace; /* Worker-owned; never contains token/URL text. */
} maage_auth_observer;
/* Worker-only. Refresh persists rotated OAuth credentials before downstream
 * exchanges, even if Minecraft is temporarily unavailable. No token in snapshots. */
maage_status maage_oauth_login(maage_profile_session *session, int device, const maage_auth_observer *observer, char detail[256]);
maage_status maage_oauth_refresh(const char *root, maage_profile_session *session, const maage_auth_observer *observer, char detail[256]);
/* A valid JSON error response may be returned alongside an HTTP failure so
 * OAuth protocol fields remain inspectable. Always wipe/free *response. */
maage_status maage_auth_request(const char *endpoint, const char *bearer, const char *body, int form, const maage_auth_observer *observer, long *status, char detail[256], struct json_object **response);
maage_status maage_auth_request_timed(long timeout_seconds, const char *endpoint, const char *bearer, const char *body, int form, const maage_auth_observer *observer, long *status, char detail[256], struct json_object **response);
/* Returns a static allowlisted error name; never echoes untrusted input. */
const char *maage_auth_error_name(const char *error);
maage_status maage_auth_json_free(struct json_object *object);
double maage_auth_number(struct json_object *object, const char *key);
maage_status maage_auth_token(char *out, size_t capacity, struct json_object *object, const char *key);
maage_status maage_auth_wait(double seconds, const maage_auth_observer *observer);
/* OS adapters: no credentials in command lines or browser URLs. */
maage_status maage_auth_random(unsigned char *bytes, size_t count);
maage_status maage_auth_browser(const char *url);
maage_status maage_auth_sleep(unsigned milliseconds);
/* Temporary IPv4 HTTP loopback callback. All calls belong to the access worker.
 * receive: 1 valid HTTP envelope (OAuth state not yet checked), 0 pending/rejected,
 * -1 transport failure. Only a state-validated result may complete the attempt.
 * reply: 1 authorization received, -1 validated OAuth error, 0 reject/keep waiting. */
typedef struct maage_auth_listener maage_auth_listener;
maage_status maage_auth_listen(unsigned *port, maage_auth_listener **output, maage_status_trace *trace);
maage_status maage_auth_receive(maage_auth_listener *listener, char *query, size_t capacity, const maage_auth_observer *observer);
maage_status maage_auth_reply(maage_auth_listener *listener, int accepted);
maage_status maage_auth_close(maage_auth_listener *listener);
#endif
