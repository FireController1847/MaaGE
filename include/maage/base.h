#ifndef MAAGE_BASE_H
#define MAAGE_BASE_H
#include "maage/content.h"
/* Statically linked minecraft root module. Registration performs no window,
 * device or asset I/O. Applications explicitly compose this with other modules. */
extern const maage_content_module maage_base_module;
#endif
