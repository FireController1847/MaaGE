#ifndef MAAGE_BASE_H
#define MAAGE_BASE_H
#include "maage/content.h"
/* Statically linked first content module. Registration performs no window,
 * device or asset I/O. Applications explicitly compose this with other modules. */
maage_status maage_base_register(maage_content_context *context);
maage_status maage_base_content_create(const maage_content_config *config, maage_content **output, maage_status_trace *trace);
/* Mutable copies used by Client colormap initialization before finalization. */
maage_status maage_base_biomes_register(maage_biome_registry *registry);
#endif
