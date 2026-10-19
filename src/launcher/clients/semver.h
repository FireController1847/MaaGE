#ifndef MAAGE_CLIENT_SEMVER_H
#define MAAGE_CLIENT_SEMVER_H
/* Strict SemVer 2.0.0; arbitrary-length numeric fields, no integer overflow.
 * Comparison requires valid input and ignores build metadata. */
int maage_semver_valid(const char *version);
int maage_semver_compare(const char *left, const char *right);
#endif
