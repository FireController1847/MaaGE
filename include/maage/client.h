#ifndef MAAGE_CLIENT_H
#define MAAGE_CLIENT_H
/* Process-lifetime immutable Client identity, independent of its location.
 * protocol is NULL until a network compatibility contract is implemented. */
typedef struct maage_client_metadata {
    const char *version, *build, *build_type, *platform, *architecture, *protocol;
} maage_client_metadata;
const maage_client_metadata *maage_client_identity(void);
#endif
