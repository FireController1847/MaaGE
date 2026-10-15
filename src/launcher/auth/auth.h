#ifndef MAAGE_LAUNCHER_AUTH_H
#define MAAGE_LAUNCHER_AUTH_H
#include "state.h"

typedef struct maage_auth maage_auth;
typedef struct maage_minecraft_session {
    char uuid[33], username[17], access_token[16384];
    double expires_at;
} maage_minecraft_session;
/* Application-thread lifecycle. Poll before each UI frame; workers never mutate
 * UI snapshots. Destroy cancels and joins the worker before releasing secrets. */
maage_launcher_result maage_auth_create(maage_launcher_state *state, maage_auth **auth);
void maage_auth_poll(maage_auth *auth);
void maage_auth_destroy(maage_auth *auth);
/* Future launch integration only; never pass sessions to widgets. Copies only
 * freshly verified, entitled, profiled, unexpired credentials. Otherwise queues
 * a silent refresh when possible and returns UNAVAILABLE. Caller owns and must
 * clear the copy. This does not start a game. */
maage_launcher_result maage_auth_session_copy(maage_auth *auth, const char *id, maage_minecraft_session *session);
void maage_auth_session_clear(maage_minecraft_session *session);
#endif
