# Standalone Phase 6 catalog: integration adds this directory and links the
# target alongside Audio, SoundBank and the Client-owned adapter callbacks.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.21)
    project(MaaGEAudioCatalog LANGUAGES C)
endif()
add_library(maage_audio_catalog STATIC sound_catalog.c sound_catalog_manifest.c)
add_library(MaaGE::AudioCatalog ALIAS maage_audio_catalog)
set_target_properties(maage_audio_catalog PROPERTIES
    C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(maage_audio_catalog PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../../../include")