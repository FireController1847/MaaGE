#ifndef MAAGE_LAUNCHER_SETUP_H
#define MAAGE_LAUNCHER_SETUP_H
#include "../assets.h"
typedef struct maage_setup_text {
    char directories[96], metadata[96], resources[96], reconstruction[96], completion[96], count[256], detail[768];
    const char *heading, *subtitle, *button;
    int actionable, complete, failed;
} maage_setup_text;
void maage_setup_describe(const maage_setup_progress *progress, maage_setup_text *text);
/* Runs native UI on the main thread. 1 = continue; 2 = verify access again; 0 = cancelled; -1 = UI error.
 * Closing cancels asynchronously; caller destroys/joins assets before returning. */
int maage_setup_native_run(maage_assets *assets);
#endif
