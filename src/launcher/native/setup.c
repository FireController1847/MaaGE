#include "setup.h"
#include <stdio.h>
#include <string.h>
void maage_setup_describe(const maage_setup_progress *p, maage_setup_text *t)
{
    const char *active;
    memset(t, 0, sizeof(*t));
    t->complete = p->stage == MAAGE_SETUP_COMPLETE;
    t->failed = p->stage == MAAGE_SETUP_FAILED;
    t->actionable = t->complete || t->failed;
    t->heading = t->complete ? "MaaGE is ready" : t->failed ? "Let’s try that again" : "Preparing MaaGE";
    t->subtitle = t->complete ? "Your Minecraft 1.7.10 asset-index resources are ready." : "Setting up the resources needed for your first launch.";
    t->button = t->failed ? "Retry" : "Continue";
    active = p->stage == MAAGE_SETUP_PREPARING ? "•" : "○";
    sprintf(t->directories, "%s  Prepare application directories", p->directories_ready ? "✓" : active);
    active = p->stage == MAAGE_SETUP_METADATA ? "•" : "○";
    sprintf(t->metadata, "%s  Retrieve Minecraft 1.7.10 metadata", p->metadata_ready ? "✓" : active);
    sprintf(t->resources, "%s  Download and verify resources", p->objects_ready ? "✓" : p->metadata_ready ? "◉" : "○");
    sprintf(t->reconstruction, "%s  Reconstruct Minecraft resources", p->reconstruction_ready ? "✓" : p->stage == MAAGE_SETUP_RECONSTRUCTING ? "◉" : "○");
    sprintf(t->completion, "%s  Complete setup", t->complete ? "✓" : p->objects_ready && p->stage == MAAGE_SETUP_VERIFYING ? "◉" : "○");
    if (t->complete)
        sprintf(t->count, "%lu logical resources · %lu verified objects", p->total_logical_assets, p->total_objects);
    else if (p->objects_ready)
        sprintf(t->count, "Reconstructing resources — %lu / %lu", p->reconstructed_files, p->total_logical_assets);
    else if (p->total_objects)
        sprintf(t->count, "Objects %lu / %lu · %lu downloaded · %.1f MiB transferred", p->verified_objects, p->total_objects, p->downloaded_objects, p->downloaded_bytes / 1048576.0);
    else strcpy(t->count, "Discovering resource totals…");
    if (p->cancellation_requested && !t->complete) strcpy(t->detail, "Stopping safely… Verified resources will be kept.");
    else if (t->failed) strncpy(t->detail, p->error, sizeof(t->detail) - 1);
    else strncpy(t->detail, p->activity[0] ? p->activity : "Checking the existing installation…", sizeof(t->detail) - 1);
}
