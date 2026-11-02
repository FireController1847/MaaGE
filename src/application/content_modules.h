#ifndef MAAGE_APPLICATION_CONTENT_MODULES_H
#define MAAGE_APPLICATION_CONTENT_MODULES_H
#include "maage/content.h"
/* One shared statically linked composition for authoritative and presentation
 * content. This application policy is outside the generic engine libraries. */
void maage_application_content_config(maage_content_config *config);
/* Application handling boundary supplies module/phase diagnostics on failure. */
maage_status maage_application_content_create(const maage_content_config *, maage_content **, maage_status_trace *);
#endif
