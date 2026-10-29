#ifndef MAAGE_LAUNCHER_SKIN_CACHE_H
#define MAAGE_LAUNCHER_SKIN_CACHE_H
#include "maage/skin.h"
#include "oauth.h"
#include <json-c/json.h>
/* Acquisition/publication run on the access worker; cached reads also serve
 * the launch worker. Public metadata never authorizes gameplay. */
maage_status maage_launcher_skin_profile(struct json_object *profile,
                                         const char *uuid, const char *name, maage_skin_reference *skin);
maage_status maage_launcher_skin_cached(const char *root, const char *uuid,
                                        const char *name, maage_skin_reference *skin, maage_status_trace *trace);
maage_status maage_launcher_skin_publish(const char *root, const maage_skin_reference *skin,
                                         const maage_auth_observer *observer);
maage_status maage_launcher_skin_restore(const char *root, const maage_profile_session *session,
                                         const maage_auth_observer *observer);
#endif
