set(MAAGE_CLIENT_RELEASE "${MAAGE_CLIENT_VERSION_MAJOR}.${MAAGE_CLIENT_VERSION_MINOR}.${MAAGE_CLIENT_VERSION_PATCH}")
if(NOT MAAGE_CLIENT_VERSION_PRERELEASE STREQUAL "")
    string(APPEND MAAGE_CLIENT_RELEASE "-${MAAGE_CLIENT_VERSION_PRERELEASE}")
endif()
maage_client_target_architecture(MAAGE_CLIENT_ARCH)
set(MAAGE_CLIENT_ARTIFACT "MaaGE-Client-${MAAGE_CLIENT_RELEASE}")
add_executable(maage_client main.c metadata.c viewport.c frontend.c pacing.c storage_context.c options.c resources.c debug.c interaction.c environment.c
    audio_service.c audio_scene.c audio_native.c
    ui/ui.c ui/gameplay.c ui/navigation.c ui/resources.c ui/font.c ui/draw.c ui/front.c ui/title.c ui/options.c)
add_dependencies(maage_client maage_versions)
maage_configure_portable_target(maage_client)
maage_configure_gui_application(maage_client client LAUNCHER_MANAGED)
set_target_properties(maage_client PROPERTIES OUTPUT_NAME "maage-client")
target_compile_definitions(maage_client PRIVATE MAAGE_APPLICATION_METADATA_QUERY MAAGE_APPLICATION_LOG_CONFIGURE
    "MAAGE_CLIENT_BUILD_TYPE=\"$<CONFIG>\"" "MAAGE_CLIENT_PLATFORM=\"${MAAGE_TARGET_SYSTEM}\""
    "MAAGE_CLIENT_ARCHITECTURE=\"${MAAGE_CLIENT_ARCH}\"")
if(NOT MAAGE_BUILD_RENDER)
    message(FATAL_ERROR "Client requires MAAGE_BUILD_RENDER=ON for its own native window.")
endif()
if(NOT MAAGE_BUILD_MINECRAFT_ASSETS OR NOT MAAGE_BUILD_IMAGE OR NOT MAAGE_BUILD_WORLD)
    message(FATAL_ERROR "Client requires MAAGE_BUILD_MINECRAFT_ASSETS, MAAGE_BUILD_IMAGE and MAAGE_BUILD_WORLD.")
endif()
if(NOT MAAGE_BUILD_INSTANCE)
    message(FATAL_ERROR "Client requires MAAGE_BUILD_INSTANCE=ON.")
endif()
if(NOT MAAGE_BUILD_AUDIO)
    message(FATAL_ERROR "Client requires MAAGE_BUILD_AUDIO=ON.")
endif()
# Single-player uses the same Server runtime as the dedicated executable.
# The local Session transport has no dependency on a loopback TCP stack.
# Audio remains Client-only; Server depends solely on portable libraries.
target_link_libraries(maage_client PRIVATE MaaGE::Render MaaGE::MinecraftAssets MaaGE::Image
    MaaGE::World MaaGE::Instance MaaGE::Session MaaGE::Server MaaGE::Audio
    MaaGE::SoundBank MaaGE::AudioCatalog)
if(WIN32)
    target_link_libraries(maage_client PRIVATE psapi)
endif()
# A dedicated executable package on every OS; no desktop bundle or entry.
set_target_properties(maage_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    "${MAAGE_OUTPUT_DIRECTORY}/bin/$<CONFIG>/${MAAGE_CLIENT_ARTIFACT}-${MAAGE_TARGET_SYSTEM}-${MAAGE_CLIENT_ARCH}")
set(MAAGE_CLIENT_MANIFEST "$<TARGET_FILE_DIR:maage_client>/maage-client.json")
if(MAAGE_PLATFORM_MACOS)
    set_target_properties(maage_client PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
    # This Launcher-managed executable has no bundle Info.plist. Embed its
    # generated plist into Mach-O so AppKit opts out of notch compatibility mode.
    set(MAAGE_CLIENT_PLIST "${PROJECT_BINARY_DIR}/generated/maage/client_Info.plist")
    target_link_options(maage_client PRIVATE
        "LINKER:-sectcreate,__TEXT,__info_plist,${MAAGE_CLIENT_PLIST}")
    set_property(TARGET maage_client APPEND PROPERTY LINK_DEPENDS "${MAAGE_CLIENT_PLIST}")
elseif(MAAGE_PLATFORM_LINUX)
    set_target_properties(maage_client PROPERTIES BUILD_RPATH_USE_ORIGIN ON BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
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
install(FILES "${MAAGE_CLIENT_MANIFEST}" DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Runtime)
