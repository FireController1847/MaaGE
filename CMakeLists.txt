cmake_minimum_required(VERSION 3.21)

# Keep generated files out of the source directories, including for manual builds.
file(RELATIVE_PATH MAAGE_BINARY_SUBDIR "${CMAKE_SOURCE_DIR}/cmake" "${CMAKE_BINARY_DIR}")
if(MAAGE_BINARY_SUBDIR STREQUAL "" OR MAAGE_BINARY_SUBDIR MATCHES "^\\.\\." OR IS_ABSOLUTE "${MAAGE_BINARY_SUBDIR}")
    message(FATAL_ERROR "Configure beneath cmake/<name>, or use a root CMake preset.")
endif()

project(MaaGE DESCRIPTION "Native, extensible voxel game platform" LANGUAGES C)

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(MAAGE_BUILD_LAUNCHER "Build the standalone launcher" ON)
option(MAAGE_BUILD_CLIENT "Build the game client" ON)
option(MAAGE_BUILD_SERVER "Build the headless dedicated server" ON)
option(MAAGE_BUILD_TESTS "Register application smoke tests" OFF)

# Single-config builds use bin/ and lib/; multi-config builds append Debug/, etc.
set(MAAGE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/build/${MAAGE_BINARY_SUBDIR}")
foreach(kind RUNTIME LIBRARY ARCHIVE)
    if(kind STREQUAL "RUNTIME")
        set(CMAKE_${kind}_OUTPUT_DIRECTORY "${MAAGE_OUTPUT_DIRECTORY}/bin")
    else()
        set(CMAKE_${kind}_OUTPUT_DIRECTORY "${MAAGE_OUTPUT_DIRECTORY}/lib")
    endif()
endforeach()

# These describe the target, never CMAKE_HOST_SYSTEM_*.
set(MAAGE_PLATFORM_WINDOWS 0)
set(MAAGE_PLATFORM_LINUX 0)
set(MAAGE_PLATFORM_MACOS 0)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(MAAGE_PLATFORM_WINDOWS 1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(MAAGE_PLATFORM_LINUX 1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(MAAGE_PLATFORM_MACOS 1)
endif()
set(MAAGE_TARGET_SYSTEM "${CMAKE_SYSTEM_NAME}")
set(MAAGE_DEFAULT_TARGET_VENDOR "unknown")
if(MAAGE_PLATFORM_MACOS)
    set(MAAGE_DEFAULT_TARGET_VENDOR "apple")
endif()
set(MAAGE_TARGET_VENDOR "${MAAGE_DEFAULT_TARGET_VENDOR}" CACHE STRING "Target vendor, if known by the toolchain")
set(MAAGE_TARGET_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
if(CMAKE_C_COMPILER_TARGET)
    string(REGEX REPLACE "-.*" "" MAAGE_TARGET_ARCHITECTURE "${CMAKE_C_COMPILER_TARGET}")
elseif(CMAKE_GENERATOR_PLATFORM)
    set(MAAGE_TARGET_ARCHITECTURE "${CMAKE_GENERATOR_PLATFORM}")
endif()
if(MAAGE_PLATFORM_MACOS AND CMAKE_OSX_ARCHITECTURES)
    set(MAAGE_TARGET_ARCHITECTURE "${CMAKE_OSX_ARCHITECTURES}")
endif()

include(CheckSymbolExists)
check_symbol_exists(puts "stdio.h" MAAGE_HAVE_PUTS)
if(NOT MAAGE_HAVE_PUTS)
    message(FATAL_ERROR "The initial console applications require standard C stdio (puts).")
endif()
configure_file(CMakeConfig.h "${PROJECT_BINARY_DIR}/generated/cmakecfg.h" @ONLY)
include(CMakeVersions.cmake)

# Apply portable requirements only to portable targets. Future adapters may opt
# into other languages/standards locally, with target_sources inside platform ifs.
# In particular, enable_language(OBJC) only when adding a macOS adapter that needs it.
function(maage_configure_portable_target target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 90 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    )
    target_include_directories(${target} PRIVATE "${PROJECT_BINARY_DIR}/generated")
endfunction()

if(MAAGE_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(src/core)
foreach(application launcher client server)
    string(TOUPPER "${application}" application_option)
    if(MAAGE_BUILD_${application_option})
        add_subdirectory(src/${application})
    endif()
endforeach()
