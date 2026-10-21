#ifndef MAAGE_CLIENT_MESH_DIAGNOSTICS_H
#define MAAGE_CLIENT_MESH_DIAGNOSTICS_H
#include "ui/ui.h"
typedef struct maage_client_mesh_diagnostics {
    unsigned long gpu_uploads, vertices_uploaded, rejected;
    unsigned int uploaded_this_frame;
    size_t gpu_resident, ready_waiting_estimate;
    double budget_ms, frame_upload_ms, stream_ms;
    double uploads_per_second, vertices_per_second;
} maage_client_mesh_diagnostics;
#endif
