set(MAAGE_CLIENT_RELEASE "${MAAGE_CLIENT_VERSION_MAJOR}.${MAAGE_CLIENT_VERSION_MINOR}.${MAAGE_CLIENT_VERSION_PATCH}")
if(NOT MAAGE_CLIENT_VERSION_PRERELEASE STREQUAL "")
    string(APPEND MAAGE_CLIENT_RELEASE "-${MAAGE_CLIENT_VERSION_PRERELEASE}")
endif()
maage_client_target_architecture(MAAGE_CLIENT_ARCH)
set(MAAGE_CLIENT_ARTIFACT "MaaGE-Client-${MAAGE_CLIENT_RELEASE}")
add_executable(maage_client main.c metadata.c)
add_dependencies(maage_client maage_versions)
maage_configure_portable_target(maage_client)
maage_configure_gui_application(maage_client client LAUNCHER_MANAGED)
set_target_properties(maage_client PROPERTIES OUTPUT_NAME "${MAAGE_CLIENT_ARTIFACT}")
target_compile_definitions(maage_client PRIVATE MAAGE_APPLICATION_METADATA_QUERY
    "MAAGE_CLIENT_BUILD_TYPE=\"$<CONFIG>\"" "MAAGE_CLIENT_PLATFORM=\"${MAAGE_TARGET_SYSTEM}\""
    "MAAGE_CLIENT_ARCHITECTURE=\"${MAAGE_CLIENT_ARCH}\"")
target_link_libraries(maage_client PRIVATE maage_core)
if(MAAGE_PLATFORM_MACOS)
    set(MAAGE_CLIENT_MANIFEST "$<TARGET_BUNDLE_DIR:maage_client>/Contents/Resources/maage-client.json")
else()
    # A self-contained development package; adjacent runtime files belong here.
    set_target_properties(maage_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY
        "${MAAGE_OUTPUT_DIRECTORY}/bin/$<CONFIG>/${MAAGE_CLIENT_ARTIFACT}-${MAAGE_TARGET_SYSTEM}-${MAAGE_CLIENT_ARCH}")
    set(MAAGE_CLIENT_MANIFEST "$<TARGET_FILE_DIR:maage_client>/maage-client.json")
endif()
# Hash after linking, and on every requested build (including signing/relink changes).
# This manifest is a developer import descriptor, not a distribution signature.
set(MAAGE_CLIENT_METADATA_COMMAND "${CMAKE_COMMAND}"
    "-DMAAGE_EXECUTABLE=$<TARGET_FILE:maage_client>" "-DMAAGE_MANIFEST=${MAAGE_CLIENT_MANIFEST}"
    "-DMAAGE_VERSION_HEADER=${PROJECT_BINARY_DIR}/generated/maage/client_version.h"
    "-DMAAGE_PLATFORM=${MAAGE_TARGET_SYSTEM}" "-DMAAGE_ARCH=${MAAGE_CLIENT_ARCH}"
    "-DMAAGE_BUILD_TYPE=$<CONFIG>" -P "${CMAKE_CURRENT_SOURCE_DIR}/manifest.cmake")
add_custom_target(maage_client_manifest ALL COMMAND ${MAAGE_CLIENT_METADATA_COMMAND}
    DEPENDS maage_client VERBATIM)
# A focused maage_client build must also produce its import descriptor.
add_custom_command(TARGET maage_client POST_BUILD COMMAND ${MAAGE_CLIENT_METADATA_COMMAND} VERBATIM)
if(NOT MAAGE_PLATFORM_MACOS)
    install(FILES "${MAAGE_CLIENT_MANIFEST}" DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Runtime)
endif()
