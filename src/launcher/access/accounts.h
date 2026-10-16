#ifndef MAAGE_LAUNCHER_ACCOUNTS_H
#define MAAGE_LAUNCHER_ACCOUNTS_H
#include "access.h"
#define MAAGE_ACCESS_TOKEN_CAPACITY 16385
typedef struct maage_account_record {
    char name[64], uuid[40], type[32];
    int token_present;
    double expires;
} maage_account_record;
typedef struct maage_account_file {
    maage_account_record records[MAAGE_ACCESS_PROFILES];
    unsigned count;
    int version;
} maage_account_file;
/* Streaming whitelist reader. Never materializes unrelated token values or the
 * authentication database. selected < 0 is metadata-only; otherwise only that
 * record's ygg.token is returned after validating its UUID and account type. Caller wipes token even on failure. */
int maage_accounts_read(const char *path, int prism_format, int selected,
    maage_account_file *result, char *token, const char *selected_uuid);
int maage_access_uuid(const char *value, char normalized[33]);
int maage_access_username(const char *value);
#endif
