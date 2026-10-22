if(MAAGE_BUILD_MINECRAFT_ASSETS)
    add_library(maage_minecraft_assets STATIC minecraft_assets.c)
    add_library(MaaGE::MinecraftAssets ALIAS maage_minecraft_assets)
    maage_configure_portable_target(maage_minecraft_assets)
    target_include_directories(maage_minecraft_assets PUBLIC "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(maage_minecraft_assets PUBLIC MaaGE::Status PRIVATE MaaGE::Storage)

    # Opt-in, client-side sound resources. These targets are not built into the
    # Launcher or headless Server merely because path resolution is available.
    add_library(maage_vorbis_decoder STATIC EXCLUDE_FROM_ALL audio_decode/stb_vorbis.c)
    set_target_properties(maage_vorbis_decoder PROPERTIES
        C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_compile_definitions(maage_vorbis_decoder PRIVATE
        STB_VORBIS_NO_PUSHDATA_API STB_VORBIS_NO_INTEGER_CONVERSION)
    if(NOT WIN32 AND NOT APPLE)
        target_link_libraries(maage_vorbis_decoder PUBLIC m)
    endif()

    add_library(maage_sound_bank STATIC EXCLUDE_FROM_ALL sound_bank.c)
    add_library(MaaGE::SoundBank ALIAS maage_sound_bank)
    maage_configure_portable_target(maage_sound_bank)
    target_include_directories(maage_sound_bank PUBLIC "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(maage_sound_bank
        PUBLIC MaaGE::Status
        PRIVATE MaaGE::MinecraftAssets MaaGE::Storage maage_vorbis_decoder)
endif()
if(MAAGE_BUILD_IMAGE)
    find_package(PNG 1.6 REQUIRED)
    add_library(maage_image STATIC image.c)
    add_library(MaaGE::Image ALIAS maage_image)
    maage_configure_portable_target(maage_image)
    target_include_directories(maage_image PUBLIC "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(maage_image PUBLIC MaaGE::Status PRIVATE PNG::PNG MaaGE::Storage)
endif()
