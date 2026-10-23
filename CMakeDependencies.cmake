# Respect explicit toolchains, including CMake's environment initialization.
option(MAAGE_USE_VCPKG "Discover vcpkg automatically for native Windows builds" ON)
if(MAAGE_USE_VCPKG AND CMAKE_HOST_WIN32
        AND (NOT CMAKE_SYSTEM_NAME OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
        AND NOT DEFINED CMAKE_TOOLCHAIN_FILE AND NOT DEFINED ENV{CMAKE_TOOLCHAIN_FILE})
    set(MAAGE_VCPKG_ROOTS "$ENV{VCPKG_ROOT}" "$ENV{VCPKG_INSTALLATION_ROOT}"
        "$ENV{VSINSTALLDIR}/VC/vcpkg")
    find_program(MAAGE_VCPKG_EXECUTABLE vcpkg)
    if(MAAGE_VCPKG_EXECUTABLE)
        get_filename_component(MAAGE_VCPKG_ROOT "${MAAGE_VCPKG_EXECUTABLE}" DIRECTORY)
        list(APPEND MAAGE_VCPKG_ROOTS "${MAAGE_VCPKG_ROOT}")
    endif()
    find_program(MAAGE_VSWHERE_EXECUTABLE vswhere
        PATHS "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer")
    if(MAAGE_VSWHERE_EXECUTABLE)
        execute_process(COMMAND "${MAAGE_VSWHERE_EXECUTABLE}" -latest -products *
            -property installationPath OUTPUT_VARIABLE MAAGE_VISUAL_STUDIO_ROOT
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        list(APPEND MAAGE_VCPKG_ROOTS "${MAAGE_VISUAL_STUDIO_ROOT}/VC/vcpkg")
    endif()
    foreach(MAAGE_VCPKG_ROOT IN LISTS MAAGE_VCPKG_ROOTS)
        if(EXISTS "${MAAGE_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE "${MAAGE_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
                CACHE FILEPATH "CMake toolchain file")
            message(STATUS "MaaGE dependencies: ${CMAKE_TOOLCHAIN_FILE}")
            break()
        endif()
    endforeach()
endif()

# Do not place an auto-detected vcpkg.json in the source root: that would change
# explicitly configured vcpkg builds on unrelated platforms. Preserve user manifests.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{CMAKE_TOOLCHAIN_FILE})
    set(MAAGE_VCPKG_TOOLCHAIN "$ENV{CMAKE_TOOLCHAIN_FILE}")
else()
    set(MAAGE_VCPKG_TOOLCHAIN "${CMAKE_TOOLCHAIN_FILE}")
endif()
get_filename_component(MAAGE_VCPKG_TOOLCHAIN_NAME "${MAAGE_VCPKG_TOOLCHAIN}" NAME)
if(NOT MAAGE_VCPKG_TOOLCHAIN_NAME STREQUAL "vcpkg.cmake")
    return()
endif()
set(MAAGE_VCPKG_MANIFEST_DIR "${CMAKE_BINARY_DIR}/vcpkg-manifest")
if(DEFINED VCPKG_MANIFEST_DIR AND NOT VCPKG_MANIFEST_DIR STREQUAL MAAGE_VCPKG_MANIFEST_DIR)
    return()
endif()
file(MAKE_DIRECTORY "${MAAGE_VCPKG_MANIFEST_DIR}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/vcpkg-windows.json"
    "${MAAGE_VCPKG_MANIFEST_DIR}/vcpkg.json" COPYONLY)
set(VCPKG_MANIFEST_DIR "${MAAGE_VCPKG_MANIFEST_DIR}" CACHE PATH "Windows dependency manifest directory")

# Keep headless configurations free of Launcher/image/graphics dependencies.
set(VCPKG_MANIFEST_FEATURES "")
if(MAAGE_BUILD_INSTANCE OR MAAGE_BUILD_LAUNCHER)
    list(APPEND VCPKG_MANIFEST_FEATURES instances)
endif()
if(MAAGE_BUILD_LAUNCHER)
    list(APPEND VCPKG_MANIFEST_FEATURES launcher)
endif()
if(MAAGE_BUILD_IMAGE)
    list(APPEND VCPKG_MANIFEST_FEATURES image)
endif()
if((MAAGE_BUILD_RENDER OR (NOT DEFINED MAAGE_BUILD_RENDER AND MAAGE_RESOURCE_DEFAULT))
        AND (MAAGE_ENABLE_VULKAN OR NOT DEFINED MAAGE_ENABLE_VULKAN))
    list(APPEND VCPKG_MANIFEST_FEATURES vulkan)
endif()
