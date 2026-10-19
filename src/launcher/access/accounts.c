#include "accounts.h"
#include <string.h>
static int maage_account_hex(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
int maage_access_uuid(const char *value, char normalized[33])
{
    size_t i, n = 0, length = strlen(value);
    int h, nonzero = 0;
    if (length != 32 && length != 36) return 0;
    for (i = 0; i < length; ++i) {
        if (length == 36 && (i == 8 || i == 13 || i == 18 || i == 23)) { if (value[i] != '-') return 0; continue; }
        h = maage_account_hex((unsigned char)value[i]); if (h < 0 || n >= 32) return 0;
        normalized[n++] = "0123456789abcdef"[h]; nonzero |= h;
    }
    normalized[n] = 0; return n == 32 && nonzero;
}
int maage_access_username(const char *value)
{
    size_t n = strlen(value), i;
    if (!n || n > 16) return 0;
    for (i = 0; i < n; ++i) if (!(value[i] >= 'A' && value[i] <= 'Z') && !(value[i] >= 'a' && value[i] <= 'z') && !(value[i] >= '0' && value[i] <= '9') && value[i] != '_') return 0;
    return 1;
}
