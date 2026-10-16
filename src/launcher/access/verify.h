#ifndef MAAGE_LAUNCHER_VERIFY_H
#define MAAGE_LAUNCHER_VERIFY_H
#include "accounts.h"
#include <json-c/json.h>
/* Bounded strict JSON for MaaGE-owned records and HTTPS responses, never for
 * external authentication databases. */
struct json_object *maage_access_json(const char *data, size_t size);
const char *maage_access_json_string(struct json_object *object, const char *key);
/* Returns VERIFIED only after both authenticated endpoint responses validate.
 * No raw response, bearer token, or curl error text is logged or persisted. */
maage_access_state maage_access_verify(const char *token, const char *uuid,
    char name[17], char detail[256], int (*cancelled)(void *), void *context);
#endif
