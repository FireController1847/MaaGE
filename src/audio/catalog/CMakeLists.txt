# Client playback cache over portable registered sound content.
add_library(maage_audio_catalog STATIC sound_catalog.c)
add_library(MaaGE::AudioCatalog ALIAS maage_audio_catalog)
maage_configure_portable_target(maage_audio_catalog)
target_include_directories(maage_audio_catalog PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../../../include")
target_link_libraries(maage_audio_catalog PUBLIC MaaGE::World)
