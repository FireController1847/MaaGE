#ifndef MAAGE_LAUNCHER_VERIFY_H
#define MAAGE_LAUNCHER_VERIFY_H
#include "accounts.h"
#include "maage/skin.h"
#include <json-c/json.h>
/* Bounded strict JSON for MaaGE-owned records and HTTPS responses, never for
 * external authentication databases. */
maage_status maage_access_json(const char *data, size_t size, struct json_object **output);
const char *maage_access_json_string(struct json_object *object, const char *key);
/* Returns VERIFIED only after a valid profile plus explicit Java entitlement.
 * This routine never logs/persists raw responses, tokens, or curl error text.
 * Empty uuid accepts and returns a newly authenticated identity; nonempty must match. */
maage_status maage_access_verify(const char *token, char uuid[33], char name[17], char detail[256], int (*cancelled)(void *), void *context, maage_access_state *state, maage_skin_reference *skin, int *skin_resolved, maage_status_trace *trace);
#endif
