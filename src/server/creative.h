#ifndef MAAGE_SERVER_CREATIVE_H
#define MAAGE_SERVER_CREATIVE_H
#include "maage/content.h"
#include "maage/session_protocol.h"
maage_status maage_server_creative_action(const maage_content *, maage_world *, maage_gameplay *, const maage_player *, const maage_session_command *, double, double);
#endif
