#include "accounts.h"
#include "maage/storage.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/* Source data never grants access. MaaGE verifies the session and owns its
 * resulting lease. Unknown JSON subtrees are consumed without retaining values. */
enum maage_json_role { MAAGE_JSON_SKIP, MAAGE_JSON_ROOT, MAAGE_JSON_ACCOUNTS,
    MAAGE_JSON_ACCOUNT, MAAGE_JSON_PROFILE, MAAGE_JSON_YGG };
typedef struct maage_account_reader {
    FILE *file;
    unsigned long bytes;
    int ch, failed, prism, selected;
    maage_account_file *out;
    long token_offset;
} maage_account_reader;
static void maage_account_next(maage_account_reader *r)
{
    if (++r->bytes > 16UL * 1024UL * 1024UL) { r->failed = 1; r->ch = EOF; }
    else r->ch = fgetc(r->file);
}
static void maage_account_space(maage_account_reader *r)
{ while (r->ch == ' ' || r->ch == '\n' || r->ch == '\r' || r->ch == '\t') maage_account_next(r); }
static int maage_account_hex(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int maage_account_string(maage_account_reader *r, char *out, size_t capacity)
{
    size_t n = 0;
    int c, i, h, value, fits = 1, continuation, first;
    if (r->ch != '"') { r->failed = 1; return 0; }
    maage_account_next(r);
    while (r->ch != '"' && !r->failed) {
        c = r->ch;
        if (c < 32) { r->failed = 1; break; }
        maage_account_next(r);
        if (c >= 128) {
            continuation = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
            if (!continuation) r->failed = 1;
            for (i = 0; i < continuation && !r->failed; ++i) {
                first = r->ch;
                if (first < 0x80 || first > 0xbf || (!i && ((c == 0xe0 && first < 0xa0) || (c == 0xed && first > 0x9f) || (c == 0xf0 && first < 0x90) || (c == 0xf4 && first > 0x8f)))) r->failed = 1;
                maage_account_next(r);
            }
            if (out) fits = 0;
            continue;
        }
        if (c == '\\') {
            c = r->ch; maage_account_next(r);
            if (c == 'u') {
                value = 0;
                for (i = 0; i < 4; ++i) {
                    h = maage_account_hex(r->ch);
                    if (h < 0) { r->failed = 1; break; }
                    value = value * 16 + h; maage_account_next(r);
                }
                c = value;
                /* Captured identity/token fields are ASCII; do not truncate
                 * an escaped non-ASCII or NUL into a different field name. */
                if (c < 32 || c > 126) fits = 0;
            } else if (c == 'b') c = '\b';
            else if (c == 'f') c = '\f';
            else if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c != '"' && c != '\\' && c != '/') r->failed = 1;
        }
        if (out) {
            if (n + 1 < capacity && fits) out[n++] = (char)c;
            else fits = 0;
        }
    }
    if (!r->failed) maage_account_next(r);
    if (out) out[fits ? n : 0] = 0;
    return fits && !r->failed;
}
static void maage_account_number_char(maage_account_reader *r, char *out, size_t capacity, size_t *n)
{
    if (out) {
        if (*n + 1 >= capacity) { r->failed = 1; return; }
        out[(*n)++] = (char)r->ch;
    }
    maage_account_next(r);
}
static int maage_account_number(maage_account_reader *r, char *out, size_t capacity)
{
    size_t n = 0;
    if (r->ch == '-') maage_account_number_char(r, out, capacity, &n);
    if (r->ch == '0') maage_account_number_char(r, out, capacity, &n);
    else if (r->ch >= '1' && r->ch <= '9') {
        do { maage_account_number_char(r, out, capacity, &n); } while (!r->failed && r->ch >= '0' && r->ch <= '9');
    } else r->failed = 1;
    if (r->ch == '.') {
        maage_account_number_char(r, out, capacity, &n);
        if (r->ch < '0' || r->ch > '9') r->failed = 1;
        while (!r->failed && r->ch >= '0' && r->ch <= '9') maage_account_number_char(r, out, capacity, &n);
    }
    if (r->ch == 'e' || r->ch == 'E') {
        maage_account_number_char(r, out, capacity, &n);
        if (r->ch == '+' || r->ch == '-') maage_account_number_char(r, out, capacity, &n);
        if (r->ch < '0' || r->ch > '9') r->failed = 1;
        while (!r->failed && r->ch >= '0' && r->ch <= '9') maage_account_number_char(r, out, capacity, &n);
    }
    if (out) out[n] = 0;
    return !r->failed;
}
static int maage_account_value(maage_account_reader *r, int role, int record, unsigned depth);
static int maage_account_child(maage_account_reader *r, int role, int record,
    const char *key, unsigned depth, unsigned *seen)
{
    maage_account_record *a = record >= 0 ? &r->out->records[record] : NULL;
    char *capture = NULL;
    size_t capacity = 0;
    unsigned bit = 0;
    int child = MAAGE_JSON_SKIP;
    char number[64];
    if (role == MAAGE_JSON_ROOT && !strcmp(key, "accounts")) { child = MAAGE_JSON_ACCOUNTS; bit = 1; }
    else if (role == MAAGE_JSON_ROOT && !strcmp(key, "formatVersion")) bit = 2;
    else if (role == MAAGE_JSON_ACCOUNT && a) {
        if ((!strcmp(key, "profile") && r->prism) || (!strcmp(key, "minecraftProfile") && !r->prism)) { child = MAAGE_JSON_PROFILE; bit = 1; }
        else if (!strcmp(key, "type")) { capture = a->type; capacity = sizeof(a->type); bit = 2; }
        else if (r->prism && !strcmp(key, "ygg")) { child = MAAGE_JSON_YGG; bit = 4; }
    } else if (role == MAAGE_JSON_PROFILE && a) {
        if (!strcmp(key, "id")) { capture = a->uuid; capacity = sizeof(a->uuid); bit = 1; }
        else if (!strcmp(key, "name")) { capture = a->name; capacity = sizeof(a->name); bit = 2; }
    } else if (role == MAAGE_JSON_YGG && a) {
        if (!strcmp(key, "token")) {
            bit = 1;
            if (r->ch == '"') a->token_present = 1;
            /* Retain a position, never a credential, until the whole record
             * and its selected UUID have been validated. Field order is arbitrary. */
            if (r->selected == record && r->ch == '"') r->token_offset = ftell(r->file) - 1;
        } else if (!strcmp(key, "exp")) bit = 2;
    }
    if (bit && (*seen & bit)) { r->failed = 1; return 0; }
    *seen |= bit;
    if (capture && r->ch == '"') {
        if (!maage_account_string(r, capture, capacity)) r->failed = 1;
        return !r->failed;
    }
    if ((role == MAAGE_JSON_ROOT || role == MAAGE_JSON_YGG) && bit == 2 && (r->ch == '-' || (r->ch >= '0' && r->ch <= '9'))) {
        char *end;
        double value;
        if (!maage_account_number(r, number, sizeof(number))) return 0;
        value = strtod(number, &end);
        if (*end || value < 0 || value > 1.0e12) r->failed = 1;
        else if (role == MAAGE_JSON_ROOT) r->out->version = value == 3 ? 3 : 0;
        else a->expires = value;
        return !r->failed;
    }
    return maage_account_value(r, child, record, depth);
}
static int maage_account_value(maage_account_reader *r, int role, int record, unsigned depth)
{
    int closing, object, child, current;
    unsigned seen = 0;
    char key[80];
    const char *literal;
    if (depth > 32 || r->failed) { r->failed = 1; return 0; }
    maage_account_space(r);
    if ((role == MAAGE_JSON_ROOT || role == MAAGE_JSON_ACCOUNT || role == MAAGE_JSON_PROFILE || role == MAAGE_JSON_YGG) && r->ch != '{') { r->failed = 1; return 0; }
    if (role == MAAGE_JSON_ACCOUNTS && r->ch != (r->prism ? '[' : '{')) { r->failed = 1; return 0; }
    if (r->ch == '{' || r->ch == '[') {
        object = r->ch == '{'; closing = object ? '}' : ']'; maage_account_next(r); maage_account_space(r);
        if (role == MAAGE_JSON_ROOT && !object) r->failed = 1;
        if ((role == MAAGE_JSON_ACCOUNT || role == MAAGE_JSON_PROFILE || role == MAAGE_JSON_YGG) && !object) r->failed = 1;
        if (r->ch == closing) { maage_account_next(r); return !r->failed; }
        while (!r->failed) {
            key[0] = 0;
            if (object) {
                maage_account_string(r, key, sizeof(key)); maage_account_space(r);
                if (r->ch != ':') { r->failed = 1; break; }
                maage_account_next(r); maage_account_space(r);
            }
            current = record;
            if (role == MAAGE_JSON_ACCOUNTS) {
                child = MAAGE_JSON_ACCOUNT;
                if (r->out->count >= MAAGE_ACCESS_PROFILES) { r->failed = 1; break; }
                current = (int)r->out->count++;
                maage_account_value(r, child, current, depth + 1);
            } else maage_account_child(r, role, current, key, depth + 1, &seen);
            maage_account_space(r);
            if (r->ch == closing) { maage_account_next(r); return !r->failed; }
            if (r->ch != ',') { r->failed = 1; break; }
            maage_account_next(r); maage_account_space(r);
        }
    } else if (r->ch == '"') maage_account_string(r, NULL, 0);
    else if (r->ch == 't' || r->ch == 'f' || r->ch == 'n') {
        literal = r->ch == 't' ? "true" : r->ch == 'f' ? "false" : "null";
        while (*literal) {
            if (r->ch != *literal++) { r->failed = 1; break; }
            maage_account_next(r);
        }
    } else maage_account_number(r, NULL, 0);
    return !r->failed;
}
int maage_accounts_read(const char *path, int prism, int selected, maage_account_file *out, char *token, const char *selected_uuid)
{
    maage_account_reader reader;
    int ok;
    char uuid[33];
    memset(out, 0, sizeof(*out)); memset(&reader, 0, sizeof(reader));
    reader.file = maage_storage_open(path, "rb");
    if (!reader.file) return 0;
    setvbuf(reader.file, NULL, _IONBF, 0);
    reader.out = out; reader.prism = prism; reader.selected = selected; reader.token_offset = -1;
    maage_account_next(&reader); maage_account_space(&reader);
    ok = reader.ch == '{' && maage_account_value(&reader, MAAGE_JSON_ROOT, -1, 0);
    maage_account_space(&reader);
    ok = ok && !reader.failed && reader.ch == EOF && !ferror(reader.file);
    if (ok && token && selected >= 0) {
        token[0] = 0;
        ok = prism && out->version == 3 && (unsigned)selected < out->count && selected_uuid &&
            !strcmp(out->records[selected].type, "MSA") && maage_access_uuid(out->records[selected].uuid, uuid) && !strcmp(uuid, selected_uuid);
        if (ok && reader.token_offset >= 0) {
            if (fseek(reader.file, reader.token_offset, SEEK_SET)) ok = 0;
            else { maage_account_next(&reader); ok = maage_account_string(&reader, token, MAAGE_ACCESS_TOKEN_CAPACITY); }
        }
    }
    fclose(reader.file);
    return ok;
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
