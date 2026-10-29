#ifndef MAAGE_WORLD_REGISTRY_PAGES_H
#define MAAGE_WORLD_REGISTRY_PAGES_H

#include <stdlib.h>

/* A four-level, 8-bit radix table. Only occupied paths and 256-pointer
 * leaves are allocated. Lookup cost is independent of the highest ID, and
 * each definition keeps a stable address for the registry lifetime. */
typedef struct maage_registry_pages {
    void *root[256];
} maage_registry_pages;

static void **maage_registry_pages_find(const maage_registry_pages *pages,
                                        unsigned long id) {
    void **second, **third, **leaf;
    if (!pages || id > 4294967295UL) return NULL;
    second = (void **) pages->root[(id >> 24) & 255UL];
    if (!second) return NULL;
    third = (void **) second[(id >> 16) & 255UL];
    if (!third) return NULL;
    leaf = (void **) third[(id >> 8) & 255UL];
    return leaf ? &leaf[id & 255UL] : NULL;
}

static void **maage_registry_pages_ensure(maage_registry_pages *pages,
                                          unsigned long id) {
    void **second, **third, **leaf;
    unsigned long high, middle, low;
    if (!pages || id > 4294967295UL) return NULL;
    high = (id >> 24) & 255UL;
    middle = (id >> 16) & 255UL;
    low = (id >> 8) & 255UL;
    second = (void **) pages->root[high];
    if (!second) {
        second = (void **) calloc(256, sizeof(*second));
        if (!second) return NULL;
        pages->root[high] = second;
    }
    third = (void **) second[middle];
    if (!third) {
        third = (void **) calloc(256, sizeof(*third));
        if (!third) return NULL;
        second[middle] = third;
    }
    leaf = (void **) third[low];
    if (!leaf) {
        leaf = (void **) calloc(256, sizeof(*leaf));
        if (!leaf) return NULL;
        third[low] = leaf;
    }
    return &leaf[id & 255UL];
}

static void maage_registry_pages_destroy(maage_registry_pages *pages,
                                         void (*release)(void *)) {
    size_t i, j, k, n;
    void **second, **third, **leaf;
    if (!pages) return;
    for (i = 0; i < 256; ++i) {
        second = (void **) pages->root[i];
        if (!second) continue;
        for (j = 0; j < 256; ++j) {
            third = (void **) second[j];
            if (!third) continue;
            for (k = 0; k < 256; ++k) {
                leaf = (void **) third[k];
                if (!leaf) continue;
                if (release)
                    for (n = 0; n < 256; ++n)
                        if (leaf[n]) release(leaf[n]);
                free(leaf);
            }
            free(third);
        }
        free(second);
    }
    for (i = 0; i < 256; ++i)
        pages->root[i] = NULL;
}

#endif
