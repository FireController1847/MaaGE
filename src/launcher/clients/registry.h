#ifndef MAAGE_CLIENT_REGISTRY_H
#define MAAGE_CLIENT_REGISTRY_H
#include "maage/storage.h"
#ifndef MAAGE_LAUNCHER_DEVELOPMENT
#define MAAGE_LAUNCHER_DEVELOPMENT 0
#endif
typedef struct maage_client_build {
    char id[65], version[96], build[96], build_type[32], platform[32], architecture[96];
    char executable[256], sha256[65], origin[16], channel[32];
    double imported;
    int available, instance_storage; /* 0 legacy; 1 UUID-owned storage launch contract. */
    char status[128];
} maage_client_build;
typedef struct maage_client_registry {
    maage_client_build *builds;
    unsigned count, capacity;
    char root[MAAGE_PATH_CAPACITY];
} maage_client_registry;
typedef struct maage_client_candidate {
    maage_client_build build;
    char source[MAAGE_PATH_CAPACITY];
} maage_client_candidate;
void maage_clients_dispose(maage_client_registry *registry);
int maage_clients_load(maage_client_registry *registry, const char *root, char error[256]);
int maage_clients_find(const maage_client_registry *registry, const char *id);
int maage_clients_path(const maage_client_registry *registry, const char *id, char path[MAAGE_PATH_CAPACITY]);
void maage_client_build_label(const maage_client_build *build, char label[384]);
/* Only unreferenced local builds may be passed for removal by the controller. */
int maage_clients_remove(maage_client_registry *registry, const char *id, char error[256]);
#if MAAGE_LAUNCHER_DEVELOPMENT
/* Explicitly trusted developer packages, inspected without execution. A manifest
 * and matching SHA-256 are required. Import revalidates after copying. */
int maage_clients_probe(const char *source, maage_client_candidate *candidate, char error[256]);
int maage_clients_import(maage_client_registry *registry, const maage_client_candidate *candidate, char error[256]);
#endif
#endif
