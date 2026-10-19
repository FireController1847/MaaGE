#ifndef MAAGE_LAUNCHER_ACCOUNTS_H
#define MAAGE_LAUNCHER_ACCOUNTS_H
#include "access.h"
#define MAAGE_ACCESS_TOKEN_CAPACITY 16385
int maage_access_uuid(const char *value, char normalized[33]);
int maage_access_username(const char *value);
#endif
