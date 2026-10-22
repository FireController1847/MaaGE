#ifndef MAAGE_CLIENT_AUDIO_NATIVE_H
#define MAAGE_CLIENT_AUDIO_NATIVE_H
#include "maage/status.h"
/* Install the platform-independent Audio / SoundBank / catalog / scene bridge
 * before audio_service_create, on the Client owner thread. */
maage_status maage_client_audio_native_install(void);
#endif
