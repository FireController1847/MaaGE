#include "maage/client.h"
#include "maage/client_version.h"
const maage_client_metadata *maage_client_identity(void)
{
    static const maage_client_metadata identity = {
        MAAGE_CLIENT_SEMANTIC_VERSION, MAAGE_CLIENT_GIT_HASH, MAAGE_CLIENT_BUILD_TYPE,
        MAAGE_CLIENT_PLATFORM, MAAGE_CLIENT_ARCHITECTURE, 0
    };
    return &identity;
}
