#include "audio_internal.h"
maage_status maage_audio_backend_start(maage_audio *audio, maage_status_trace *trace) {
    (void) audio;
    (void) trace;
    return maage_audio_result(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED,
                              MAAGE_OPERATION_ACQUIRE, MAAGE_DETAIL_AUDIO_DEVICE_UNAVAILABLE);
}
void maage_audio_backend_stop(maage_audio *audio) {
    (void) audio;
}
