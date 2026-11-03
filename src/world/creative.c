#include "maage/creative.h"
#include <stdlib.h>
#include <string.h>

typedef struct creative_tab {
    maage_creative_tab_definition definition;
    char *name, *title;
    maage_creative_icon_quad *quads;
    maage_creative_entry_id *view;
    size_t count;
} creative_tab;
struct maage_creative_registry {
    const maage_item_registry *items;
    const maage_presentation_registry *presentation;
    creative_tab *tabs;
    maage_creative_entry *entries;
    size_t tab_count, tab_capacity, entry_count, entry_capacity;
    maage_creative_tab_id *order;
    size_t *names, buckets;
    int finalized;
};
static maage_status creative_status(unsigned char code, unsigned char operation) {
    return maage_status_make(code, code < MAAGE_STATUS_WARNING ? 0 : MAAGE_CONTEXT_REGISTRY, operation, 0);
}
static char *creative_text(const char *text) {
    char *copy;
    if (!text) return NULL;
    copy = (char *) malloc(strlen(text) + 1);
    if (copy) strcpy(copy, text);
    return copy;
}
static int creative_name_valid(const char *text) {
    const char *start = text;
    int colon = 0;
    if (!text || !*text) return 0;
    while (*text) {
        if (*text == ':') {
            if (colon || text == start || !text[1]) return 0;
            colon = 1;
        } else if (!((*text >= 'a' && *text <= 'z') || (*text >= '0' && *text <= '9') || *text == '_' || *text == '-' || *text == '.' || (colon && *text == '/'))) return 0;
        ++text;
    }
    return colon;
}
static unsigned long creative_hash(const char *text) {
    unsigned long hash = 2166136261UL;
    while (*text)
        hash = (hash ^ (unsigned char) *text++) * 16777619UL;
    return hash;
}
static size_t creative_bucket(const maage_creative_registry *registry, const char *name) {
    size_t bucket = creative_hash(name) & (registry->buckets - 1);
    while (registry->names[bucket] && strcmp(registry->tabs[registry->names[bucket] - 1].name, name))
        bucket = (bucket + 1) & (registry->buckets - 1);
    return bucket;
}
static void *creative_grow(void *data, size_t *capacity, size_t count, size_t size) {
    size_t next;
    void *allocation;
    if (count < *capacity) return data;
    if (count >= (size_t) - 1 / size) return NULL;
    next = *capacity ? *capacity : 16;
    if (next <= count) next = next > (size_t) - 1 / size / 2 ? count + 1 : next * 2;
    allocation = realloc(data, next * size);
    if (!allocation) return NULL;
    *capacity = next;
    return allocation;
}
maage_status maage_creative_create(const maage_item_registry *items, const maage_presentation_registry *presentation, maage_creative_registry **output) {
    maage_creative_registry *registry;
    if (output) *output = NULL;
    if (!items || !presentation || !output) return creative_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_CREATE);
    registry = (maage_creative_registry *) calloc(1, sizeof(*registry));
    if (!registry) return creative_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    registry->buckets = 32;
    registry->names = (size_t *) calloc(registry->buckets, sizeof(*registry->names));
    if (!registry->names) {
        free(registry);
        return creative_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_CREATE);
    }
    registry->items = items;
    registry->presentation = presentation;
    *output = registry;
    return creative_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_CREATE);
}
maage_status maage_creative_destroy(maage_creative_registry *registry) {
    size_t i;
    if (!registry) return creative_status(MAAGE_STATUS_NOOP, MAAGE_OPERATION_DESTROY);
    for (i = 0; i < registry->tab_count; ++i) {
        free(registry->tabs[i].name);
        free(registry->tabs[i].title);
        free(registry->tabs[i].quads);
        free(registry->tabs[i].view);
    }
    for (i = 0; i < registry->entry_count; ++i)
        free((void *) registry->entries[i].tooltip);
    free(registry->tabs);
    free(registry->entries);
    free(registry->order);
    free(registry->names);
    free(registry);
    return creative_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_DESTROY);
}
maage_status maage_creative_tab_register(maage_creative_registry *registry, const char *name, maage_creative_tab_definition *definition) {
    creative_tab tab, *tabs;
    size_t i, bucket, buckets, *names;
    if (!registry || registry->finalized || !definition || !creative_name_valid(name) || !definition->title || !*definition->title || definition->kind > MAAGE_CREATIVE_INVENTORY || (definition->icon_quad_count && !definition->icon_quads)) return creative_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (registry->tab_count >= 4294967294UL || definition->icon_quad_count > (size_t) - 1 / sizeof(*tab.quads)) return creative_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    if (registry->names[creative_bucket(registry, name)]) return creative_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    if (definition->kind != MAAGE_CREATIVE_CATALOG)
        for (i = 0; i < registry->tab_count; ++i)
            if (registry->tabs[i].definition.kind == definition->kind) return creative_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_REGISTER);
    if (registry->tab_count >= registry->buckets / 2) {
        if (registry->buckets > (size_t) - 1 / sizeof(*names) / 2) return creative_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
        buckets = registry->buckets * 2;
        names = (size_t *) calloc(buckets, sizeof(*names));
        if (!names) return creative_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
        for (i = 0; i < registry->tab_count; ++i) {
            bucket = creative_hash(registry->tabs[i].name) & (buckets - 1);
            while (names[bucket])
                bucket = (bucket + 1) & (buckets - 1);
            names[bucket] = i + 1;
        }
        free(registry->names);
        registry->names = names;
        registry->buckets = buckets;
    }
    memset(&tab, 0, sizeof(tab));
    tab.name = creative_text(name);
    tab.title = creative_text(definition->title);
    if (definition->icon_quad_count) {
        tab.quads = (maage_creative_icon_quad *) malloc(definition->icon_quad_count * sizeof(*tab.quads));
        if (tab.quads) memcpy(tab.quads, definition->icon_quads, definition->icon_quad_count * sizeof(*tab.quads));
    }
    tabs = (creative_tab *) creative_grow(registry->tabs, &registry->tab_capacity, registry->tab_count, sizeof(*registry->tabs));
    if (tabs) registry->tabs = tabs;
    if (!tab.name || !tab.title || (definition->icon_quad_count && !tab.quads) || !tabs) {
        free(tab.name);
        free(tab.title);
        free(tab.quads);
        return creative_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    }
    tab.definition = *definition;
    tab.definition.id = (maage_creative_tab_id) registry->tab_count + 1;
    tab.definition.title = tab.title;
    tab.definition.icon_quads = tab.quads;
    registry->tabs[registry->tab_count++] = tab;
    registry->names[creative_bucket(registry, name)] = registry->tab_count;
    definition->id = tab.definition.id;
    return creative_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
maage_status maage_creative_tab_resolve(const maage_creative_registry *registry, const char *name, maage_creative_tab_id *id) {
    size_t value;
    if (id) *id = 0;
    if (!registry || !name || !id) return creative_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_RESOLVE);
    value = registry->names[creative_bucket(registry, name)];
    *id = (maage_creative_tab_id) value;
    return creative_status(value ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_NOT_FOUND, MAAGE_OPERATION_RESOLVE);
}
maage_status maage_creative_entry_register(maage_creative_registry *registry, maage_creative_entry *entry) {
    maage_creative_entry copy, *entries;
    const maage_creative_tab_definition *tab = registry && entry ? maage_creative_tab(registry, entry->tab) : NULL;
    if (!registry || registry->finalized || !entry || !tab || tab->kind != MAAGE_CREATIVE_CATALOG || entry->stack.count != 1 || entry->stack.damage || !maage_item_stack_valid(registry->items, &entry->stack) || (entry->searchable != 0 && entry->searchable != 1)) return creative_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_REGISTER);
    if (registry->entry_count >= 4294967294UL) return creative_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_REGISTER);
    copy = *entry;
    copy.tooltip = creative_text(entry->tooltip);
    entries = (maage_creative_entry *) creative_grow(registry->entries, &registry->entry_capacity, registry->entry_count, sizeof(*registry->entries));
    if (entries) registry->entries = entries;
    if ((entry->tooltip && !copy.tooltip) || !entries) {
        free((void *) copy.tooltip);
        return creative_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_REGISTER);
    }
    copy.id = (maage_creative_entry_id) registry->entry_count + 1;
    registry->entries[registry->entry_count++] = copy;
    entry->id = copy.id;
    return creative_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_REGISTER);
}
static int creative_tab_compare(const void *a, const void *b) {
    const creative_tab *const *left = (const creative_tab *const *) a, *const *right = (const creative_tab *const *) b;
    if ((*left)->definition.order != (*right)->definition.order) return (*left)->definition.order < (*right)->definition.order ? -1 : 1;
    return (*left)->definition.id < (*right)->definition.id ? -1 : ((*left)->definition.id != (*right)->definition.id);
}
static int creative_entry_compare(const void *a, const void *b) {
    const maage_creative_entry *const *left = (const maage_creative_entry *const *) a, *const *right = (const maage_creative_entry *const *) b;
    if ((*left)->order != (*right)->order) return (*left)->order < (*right)->order ? -1 : 1;
    return (*left)->id < (*right)->id ? -1 : ((*left)->id != (*right)->id);
}
static int creative_search_compare(const void *a, const void *b) {
    const maage_creative_entry *const *left = (const maage_creative_entry *const *) a, *const *right = (const maage_creative_entry *const *) b;
    if ((*left)->search_order != (*right)->search_order) return (*left)->search_order < (*right)->search_order ? -1 : 1;
    return (*left)->id < (*right)->id ? -1 : ((*left)->id != (*right)->id);
}
static int creative_stack_compare(const void *a, const void *b) {
    const maage_creative_entry *const *left = (const maage_creative_entry *const *) a, *const *right = (const maage_creative_entry *const *) b;
    if ((*left)->stack.item_id != (*right)->stack.item_id) return (*left)->stack.item_id < (*right)->stack.item_id ? -1 : 1;
    if ((*left)->stack.metadata != (*right)->stack.metadata) return (*left)->stack.metadata < (*right)->stack.metadata ? -1 : 1;
    return creative_search_compare(a, b);
}
maage_status maage_creative_finalize(maage_creative_registry *registry) {
    creative_tab **tabs;
    const maage_creative_entry **entries, *entry, *previous;
    const maage_creative_icon_quad *quad;
    maage_status result;
    size_t i, j, k, count;
    if (!registry || registry->finalized) return creative_status(MAAGE_STATUS_INVALID_STATE, MAAGE_OPERATION_FINALIZE);
    if (registry->tab_count > (size_t) - 1 / sizeof(*tabs) || registry->tab_count > (size_t) - 1 / sizeof(*registry->order) || registry->entry_count > (size_t) - 1 / sizeof(*entries) || registry->entry_count > (size_t) - 1 / sizeof(maage_creative_entry_id)) return creative_status(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_OPERATION_FINALIZE);
    tabs = registry->tab_count ? (creative_tab **) malloc(registry->tab_count * sizeof(*tabs)) : NULL;
    entries = registry->entry_count ? (const maage_creative_entry **) malloc(registry->entry_count * sizeof(*entries)) : NULL;
    registry->order = registry->tab_count ? (maage_creative_tab_id *) malloc(registry->tab_count * sizeof(*registry->order)) : NULL;
    if ((registry->tab_count && (!tabs || !registry->order)) || (registry->entry_count && !entries)) goto allocation_failure;
    for (i = 0; i < registry->tab_count; ++i) {
        tabs[i] = &registry->tabs[i];
        if (!maage_presentation_texture(registry->presentation, tabs[i]->definition.background) || !maage_item_stack_valid(registry->items, &tabs[i]->definition.icon)) goto invalid;
        for (j = 0; j < tabs[i]->definition.icon_quad_count; ++j) {
            quad = &tabs[i]->quads[j];
            if (!maage_presentation_texture(registry->presentation, quad->texture)) goto invalid;
            for (k = 0; k < 4; ++k) {
                if (!(quad->color[k] >= 0 && quad->color[k] <= 1)) goto invalid;
                if (!(quad->position[k][0] >= 0 && quad->position[k][0] <= 16 && quad->position[k][1] >= 0 && quad->position[k][1] <= 16 && quad->uv[k][0] >= 0 && quad->uv[k][0] <= 1 && quad->uv[k][1] >= 0 && quad->uv[k][1] <= 1)) goto invalid;
            }
        }
        count = 0;
        for (j = 0; j < registry->entry_count; ++j) {
            entry = &registry->entries[j];
            if (!maage_item_stack_valid(registry->items, &entry->stack)) goto invalid;
            if (tabs[i]->definition.kind == MAAGE_CREATIVE_SEARCH ? entry->searchable : entry->tab == tabs[i]->definition.id) entries[count++] = entry;
        }
        if (tabs[i]->definition.kind == MAAGE_CREATIVE_SEARCH && count) {
            qsort(entries, count, sizeof(*entries), creative_stack_compare);
            previous = NULL;
            k = 0;
            for (j = 0; j < count; ++j) {
                entry = entries[j];
                if (previous && previous->stack.item_id == entry->stack.item_id && previous->stack.metadata == entry->stack.metadata) continue;
                entries[k++] = previous = entry;
            }
            count = k;
            qsort(entries, count, sizeof(*entries), creative_search_compare);
        } else if (count) qsort(entries, count, sizeof(*entries), creative_entry_compare);
        tabs[i]->view = count ? (maage_creative_entry_id *) malloc(count * sizeof(*tabs[i]->view)) : NULL;
        if (count && !tabs[i]->view) goto allocation_failure;
        tabs[i]->count = count;
        for (j = 0; j < count; ++j)
            tabs[i]->view[j] = entries[j]->id;
    }
    if (registry->tab_count) qsort(tabs, registry->tab_count, sizeof(*tabs), creative_tab_compare);
    for (i = 0; i < registry->tab_count; ++i)
        registry->order[i] = tabs[i]->definition.id;
    free(tabs);
    free(entries);
    registry->finalized = 1;
    return creative_status(MAAGE_STATUS_SUCCESS, MAAGE_OPERATION_FINALIZE);
invalid:
    result = creative_status(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_OPERATION_FINALIZE);
    goto failed;
allocation_failure:
    result = creative_status(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_OPERATION_FINALIZE);
failed:
    for (i = 0; i < registry->tab_count; ++i) {
        free(registry->tabs[i].view);
        registry->tabs[i].view = NULL;
        registry->tabs[i].count = 0;
    }
    free(registry->order);
    registry->order = NULL;
    free(tabs);
    free(entries);
    return result;
}

int maage_creative_is_finalized(const maage_creative_registry *registry) {
    return registry && registry->finalized;
}
size_t maage_creative_tab_count(const maage_creative_registry *registry) {
    return registry ? registry->tab_count : 0;
}
size_t maage_creative_entry_count(const maage_creative_registry *registry) {
    return registry ? registry->entry_count : 0;
}
const maage_creative_tab_definition *maage_creative_tab(const maage_creative_registry *registry, maage_creative_tab_id id) {
    return registry && id && id <= registry->tab_count ? &registry->tabs[id - 1].definition : NULL;
}
const maage_creative_tab_definition *maage_creative_tab_at(const maage_creative_registry *registry, size_t index) {
    return registry && registry->finalized && index < registry->tab_count ? maage_creative_tab(registry, registry->order[index]) : NULL;
}
const maage_creative_entry *maage_creative_get(const maage_creative_registry *registry, maage_creative_entry_id id) {
    return registry && id && id <= registry->entry_count ? &registry->entries[id - 1] : NULL;
}
const maage_creative_entry_id *maage_creative_view(const maage_creative_registry *registry, maage_creative_tab_id id, size_t *count) {
    const creative_tab *tab = registry && registry->finalized && id && id <= registry->tab_count ? &registry->tabs[id - 1] : NULL;
    if (count) *count = tab ? tab->count : 0;
    return tab ? tab->view : NULL;
}
