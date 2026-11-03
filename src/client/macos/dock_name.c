#include "dock_name.h"
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>

/* Private ABI stays in this macOS adapter. No private symbol is referenced by
 * the linker, and no resolved address survives the library handle. */
typedef CFTypeRef (*client_application_asn)(void);
typedef OSStatus (*client_application_name)(int, CFTypeRef, CFStringRef, CFTypeRef, CFDictionaryRef *);

maage_status maage_client_dock_name(maage_status_trace *trace) {
    void *library;
    client_application_asn current;
    client_application_name update;
    CFStringRef *key;
    CFTypeRef application;
    OSStatus native_result;
    maage_status result;
    library = dlopen("/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/LaunchServices", RTLD_LAZY | RTLD_LOCAL);
    if (!library) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SET, 0), MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SET);
    current = (client_application_asn) dlsym(library, "_LSGetCurrentApplicationASN");
    update = (client_application_name) dlsym(library, "_LSSetApplicationInformationItem");
    key = (CFStringRef *) dlsym(library, "_kLSDisplayNameKey");
    result = maage_status_make(MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SET, 0);
    if (current && update && key && *key && CFGetTypeID(*key) == CFStringGetTypeID()) {
        application = current();
        if (application) {
            /* The private API uses -2 to select the current login session. */
            native_result = update(-2, application, *key, CFSTR(MAAGE_CLIENT_DISPLAY_NAME), NULL);
            result = maage_status_make(native_result == 0 ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SET, 0);
            if (native_result != 0) maage_status_trace_record(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SET, __FILE__, __LINE__, (long) native_result);
        }
    }
    dlclose(library);
    if (maage_status_is_warning(result)) return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CLIENT, MAAGE_OPERATION_SET);
    return result;
}
