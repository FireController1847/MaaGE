#include "semver.h"
#include <string.h>
static int maage_version_digit(char c) {
    return c >= '0' && c <= '9';
}
static int maage_version_char(char c) {
    return maage_version_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
}
int maage_semver_valid(const char *v) {
    const char *start;
    unsigned i;
    int numeric, prerelease;
    if (!v || !*v) return 0;
    for (i = 0; i < 3; ++i) {
        start = v;
        while (maage_version_digit(*v))
            ++v;
        if (v == start || (v - start > 1 && *start == '0')) return 0;
        if (i < 2 && *v++ != '.') return 0;
    }
    if (*v && *v != '-' && *v != '+') return 0;
    prerelease = *v == '-';
    if (*v) {
        ++v;
        for (;;) {
            start = v;
            numeric = 1;
            while (maage_version_char(*v)) {
                if (!maage_version_digit(*v)) numeric = 0;
                ++v;
            }
            if (v == start || (prerelease && numeric && v - start > 1 && *start == '0')) return 0;
            if (*v == '.') {
                ++v;
                continue;
            }
            if (*v == '+' && prerelease) {
                prerelease = 0;
                ++v;
                continue;
            }
            return !*v;
        }
    }
    return 1;
}
static int maage_version_part(const char *a, size_t n, const char *b, size_t m, int numeric) {
    int c;
    if (numeric && n != m) return n < m ? -1 : 1;
    c = memcmp(a, b, n < m ? n : m);
    return c ? (c < 0 ? -1 : 1) : n == m ? 0
                              : n < m    ? -1
                                         : 1;
}
int maage_semver_compare(const char *a, const char *b) {
    const char *x, *y;
    unsigned i;
    int c, an, bn;
    for (i = 0; i < 3; ++i) {
        x = a;
        y = b;
        while (maage_version_digit(*a))
            ++a;
        while (maage_version_digit(*b))
            ++b;
        c = maage_version_part(x, (size_t) (a - x), y, (size_t) (b - y), 1);
        if (c) return c;
        if (i < 2) {
            ++a;
            ++b;
        }
    }
    if (*a != '-' || *b != '-') return (*a == '-') == (*b == '-') ? 0 : *a == '-' ? -1
                                                                                  : 1;
    ++a;
    ++b;
    for (;;) {
        x = a;
        y = b;
        an = bn = 1;
        while (maage_version_char(*a)) {
            if (!maage_version_digit(*a)) an = 0;
            ++a;
        }
        while (maage_version_char(*b)) {
            if (!maage_version_digit(*b)) bn = 0;
            ++b;
        }
        c = an != bn ? (an ? -1 : 1) : maage_version_part(x, (size_t) (a - x), y, (size_t) (b - y), an);
        if (c) return c;
        if (*a != '.' || *b != '.') return (*a == '.') == (*b == '.') ? 0 : *a == '.' ? 1
                                                                                      : -1;
        ++a;
        ++b;
    }
}
