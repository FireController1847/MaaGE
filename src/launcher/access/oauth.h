#ifndef MAAGE_LAUNCHER_OAUTH_H
#define MAAGE_LAUNCHER_OAUTH_H
#include "profile_store.h"
struct json_object;
#define MAAGE_MICROSOFT_CLIENT_ID "5e2b53b6-d00b-4de9-8f4d-a1897aaeca43"
#define MAAGE_MICROSOFT_REDIRECT "maage://auth"
#define MAAGE_MICROSOFT_AUTHORITY "https://login.microsoftonline.com/consumers/oauth2/v2.0/"
#define MAAGE_MICROSOFT_SCOPE "XboxLive.signin offline_access"
typedef struct maage_auth_observer {
    int (*cancelled)(void *context);
    void (*status)(void *context, const char *text);
    void (*device)(void *context, const char *url, const char *code, double deadline);
    void *context;
} maage_auth_observer;
/* Worker-only. Refresh persists rotated OAuth credentials before downstream
 * exchanges, even if Minecraft is temporarily unavailable. No token in snapshots. */
int maage_oauth_login(maage_profile_session *session, int device,
    const maage_auth_observer *observer, char detail[256]);
int maage_oauth_refresh(const char *root, maage_profile_session *session,
    const maage_auth_observer *observer, char detail[256]);
struct json_object *maage_auth_request(const char *endpoint, const char *bearer,
    const char *body, int form, const maage_auth_observer *observer,
    long *status, char detail[256]);
void maage_auth_json_free(struct json_object *object);
double maage_auth_number(struct json_object *object, const char *key);
int maage_auth_token(char *out, size_t capacity, struct json_object *object, const char *key);
int maage_auth_wait(double seconds, const maage_auth_observer *observer);
/* OS adapters: no credentials in command lines or browser URLs. */
int maage_auth_random(unsigned char *bytes, size_t count);
int maage_auth_browser(const char *url);
void maage_auth_sleep(unsigned milliseconds);
/* Local callback transport is NOT the OAuth redirect. It forwards OS scheme
 * activations to the pending worker, authenticated by its random state + PKCE. */
typedef struct maage_auth_listener maage_auth_listener;
maage_auth_listener *maage_auth_listen(unsigned *port);
int maage_auth_receive(maage_auth_listener *listener, char *uri, size_t capacity);
void maage_auth_close(maage_auth_listener *listener);
int maage_oauth_forward_url(const char *uri);
/* Install/remove lifecycle callbacks on the main thread (macOS); other platforms
 * receive protocol activations as argv. Does not start authentication. */
void maage_auth_platform_start(void);
void maage_auth_platform_stop(void);
#endif
