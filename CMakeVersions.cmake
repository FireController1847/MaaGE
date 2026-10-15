# Independent application releases. Edit only the application's own fields.
set(MAAGE_LAUNCHER_VERSION_MAJOR 1)
set(MAAGE_LAUNCHER_VERSION_MINOR 0)
set(MAAGE_LAUNCHER_VERSION_PATCH 0)
set(MAAGE_LAUNCHER_VERSION_PRERELEASE "alpha.1")

set(MAAGE_CLIENT_VERSION_MAJOR 1)
set(MAAGE_CLIENT_VERSION_MINOR 0)
set(MAAGE_CLIENT_VERSION_PATCH 0)
set(MAAGE_CLIENT_VERSION_PRERELEASE "alpha.1")

set(MAAGE_SERVER_VERSION_MAJOR 1)
set(MAAGE_SERVER_VERSION_MINOR 0)
set(MAAGE_SERVER_VERSION_PATCH 0)
set(MAAGE_SERVER_VERSION_PRERELEASE "alpha.1")

if(CMAKE_SCRIPT_MODE_FILE)
    # Run once per build, including in already configured workspaces. Archives
    # without .git must not accidentally inherit an enclosing repository's HEAD.
    set(MAAGE_GIT_HASH "unknown")
    if(EXISTS "${MAAGE_VERSION_SOURCE_DIR}/.git")
        find_package(Git QUIET)
        if(GIT_FOUND)
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${MAAGE_VERSION_SOURCE_DIR}"
                rev-parse --short=7 HEAD
                RESULT_VARIABLE MAAGE_GIT_RESULT
                OUTPUT_VARIABLE MAAGE_GIT_OUTPUT
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(MAAGE_GIT_RESULT EQUAL 0 AND MAAGE_GIT_OUTPUT MATCHES "^[0-9a-fA-F]+$")
                set(MAAGE_GIT_HASH "${MAAGE_GIT_OUTPUT}")
            endif()
        endif()
    endif()

    foreach(application launcher client server)
        string(TOUPPER "${application}" MAAGE_APPLICATION)
        foreach(component MAJOR MINOR PATCH)
            set(MAAGE_VERSION_${component} "${MAAGE_${MAAGE_APPLICATION}_VERSION_${component}}")
            if(NOT MAAGE_VERSION_${component} MATCHES "^(0|[1-9][0-9]*)$")
                message(FATAL_ERROR "Invalid ${application} SemVer ${component}.")
            endif()
        endforeach()
        set(MAAGE_VERSION_PRERELEASE "${MAAGE_${MAAGE_APPLICATION}_VERSION_PRERELEASE}")
        set(MAAGE_VERSION "${MAAGE_VERSION_MAJOR}.${MAAGE_VERSION_MINOR}.${MAAGE_VERSION_PATCH}")
        if(NOT MAAGE_VERSION_PRERELEASE STREQUAL "")
            if(NOT MAAGE_VERSION_PRERELEASE MATCHES "^[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*$")
                message(FATAL_ERROR "Invalid ${application} SemVer prerelease.")
            endif()
            string(REPLACE "." ";" MAAGE_PRERELEASE_IDENTIFIERS "${MAAGE_VERSION_PRERELEASE}")
            foreach(identifier IN LISTS MAAGE_PRERELEASE_IDENTIFIERS)
                if(identifier MATCHES "^0[0-9]+$")
                    message(FATAL_ERROR "Numeric SemVer prerelease identifiers cannot have leading zeroes.")
                endif()
            endforeach()
            string(APPEND MAAGE_VERSION "-${MAAGE_VERSION_PRERELEASE}")
        endif()
        string(APPEND MAAGE_VERSION "+${MAAGE_GIT_HASH}")
        # file(CONFIGURE) preserves timestamps when content is unchanged.
        file(CONFIGURE OUTPUT "${MAAGE_VERSION_OUTPUT_DIR}/maage/${application}_version.h" CONTENT [=[
/* Generated during the build. Edit release definitions in CMakeVersions.cmake. */
#ifndef MAAGE_@MAAGE_APPLICATION@_VERSION_H
#define MAAGE_@MAAGE_APPLICATION@_VERSION_H

#define MAAGE_@MAAGE_APPLICATION@_VERSION_MAJOR @MAAGE_VERSION_MAJOR@
#define MAAGE_@MAAGE_APPLICATION@_VERSION_MINOR @MAAGE_VERSION_MINOR@
#define MAAGE_@MAAGE_APPLICATION@_VERSION_PATCH @MAAGE_VERSION_PATCH@
#define MAAGE_@MAAGE_APPLICATION@_VERSION_PRERELEASE "@MAAGE_VERSION_PRERELEASE@"
#define MAAGE_@MAAGE_APPLICATION@_GIT_HASH "@MAAGE_GIT_HASH@"
#define MAAGE_@MAAGE_APPLICATION@_VERSION "@MAAGE_VERSION@"

#endif
]=] @ONLY)
    endforeach()
else()
    # Explicit application dependencies also support building a single target.
    add_custom_target(maage_versions
        COMMAND "${CMAKE_COMMAND}"
            "-DMAAGE_VERSION_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DMAAGE_VERSION_OUTPUT_DIR=${PROJECT_BINARY_DIR}/generated"
            -P "${CMAKE_CURRENT_LIST_FILE}"
        BYPRODUCTS
            "${PROJECT_BINARY_DIR}/generated/maage/launcher_version.h"
            "${PROJECT_BINARY_DIR}/generated/maage/client_version.h"
            "${PROJECT_BINARY_DIR}/generated/maage/server_version.h"
        COMMENT "Refreshing application versions from Git HEAD"
        VERBATIM)
endif()
