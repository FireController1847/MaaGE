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

# Select static libraries AND static CRT; static-md still needs the VC runtime.
# Target information wins over the developer environment. Never infer a cross
# target from the build host's processor.
if(VCPKG_TARGET_TRIPLET)
    if(VCPKG_TARGET_TRIPLET MATCHES "^(x86|x64|arm|arm64)-windows(-static-md)?$")
        set(MAAGE_VCPKG_ARCH "${CMAKE_MATCH_1}")
        set(MAAGE_VCPKG_PREVIOUS_TRIPLET "${VCPKG_TARGET_TRIPLET}")
        set(VCPKG_TARGET_TRIPLET "${MAAGE_VCPKG_ARCH}-windows-maage-static"
            CACHE STRING "MaaGE static Windows dependency triplet" FORCE)

        # Find-package results still point to the previous triplet's import
        # libraries. Invalidate those paths before loading the new toolchain.
        if(VCPKG_INSTALLED_DIR)
            file(TO_CMAKE_PATH "${VCPKG_INSTALLED_DIR}" MAAGE_VCPKG_PREVIOUS_ROOT)
        else()
            set(MAAGE_VCPKG_PREVIOUS_ROOT "${CMAKE_BINARY_DIR}/vcpkg_installed")
        endif()
        get_cmake_property(MAAGE_VCPKG_CACHE_VARIABLES CACHE_VARIABLES)
        foreach(MAAGE_VCPKG_CACHE_VARIABLE IN LISTS MAAGE_VCPKG_CACHE_VARIABLES)
            get_property(MAAGE_VCPKG_CACHE_VALUE CACHE "${MAAGE_VCPKG_CACHE_VARIABLE}" PROPERTY VALUE)
            string(REPLACE "\\" "/" MAAGE_VCPKG_CACHE_VALUE "${MAAGE_VCPKG_CACHE_VALUE}")
            string(FIND "${MAAGE_VCPKG_CACHE_VALUE}"
                "${MAAGE_VCPKG_PREVIOUS_ROOT}/${MAAGE_VCPKG_PREVIOUS_TRIPLET}/" MAAGE_VCPKG_CACHE_MATCH)
            if(NOT MAAGE_VCPKG_CACHE_MATCH EQUAL -1)
                unset("${MAAGE_VCPKG_CACHE_VARIABLE}" CACHE)
                unset("${MAAGE_VCPKG_CACHE_VARIABLE}")
            endif()
        endforeach()
        message(STATUS "MaaGE dependencies: switching ${MAAGE_VCPKG_PREVIOUS_TRIPLET} to ${VCPKG_TARGET_TRIPLET}")
    endif()
    if(NOT VCPKG_TARGET_TRIPLET MATCHES "^(x86|x64|arm|arm64)-windows(-maage)?-static$")
        message(FATAL_ERROR "MaaGE's Windows manifest requires a static CRT/library triplet. "
            "Use a fresh CMake build directory to select it automatically, or set "
            "VCPKG_TARGET_TRIPLET to <target>-windows-static (not static-md).")
    endif()
    set(MAAGE_VCPKG_ARCH "${CMAKE_MATCH_1}")
else()
    set(MAAGE_VCPKG_ARCH "${CMAKE_GENERATOR_PLATFORM}")
    if(NOT MAAGE_VCPKG_ARCH AND CMAKE_C_COMPILER_TARGET)
        string(REGEX REPLACE "-.*" "" MAAGE_VCPKG_ARCH "${CMAKE_C_COMPILER_TARGET}")
    endif()
    if(NOT MAAGE_VCPKG_ARCH)
        set(MAAGE_VCPKG_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    if(NOT MAAGE_VCPKG_ARCH)
        if(CMAKE_C_COMPILER)
            find_program(MAAGE_VCPKG_COMPILER NAMES "${CMAKE_C_COMPILER}" NO_CACHE)
        else()
            find_program(MAAGE_VCPKG_COMPILER NAMES cl NO_CACHE)
        endif()
        if(MAAGE_VCPKG_COMPILER)
            get_filename_component(MAAGE_VCPKG_COMPILER_DIR "${MAAGE_VCPKG_COMPILER}" DIRECTORY)
            get_filename_component(MAAGE_VCPKG_COMPILER_ARCH "${MAAGE_VCPKG_COMPILER_DIR}" NAME)
            string(TOLOWER "${MAAGE_VCPKG_COMPILER_ARCH}" MAAGE_VCPKG_COMPILER_ARCH)
            if(MAAGE_VCPKG_COMPILER_ARCH MATCHES "^(x86|x64|amd64|arm|arm64)$")
                set(MAAGE_VCPKG_ARCH "${MAAGE_VCPKG_COMPILER_ARCH}")
            endif()
        endif()
    endif()
    if(NOT MAAGE_VCPKG_ARCH)
        set(MAAGE_VCPKG_ARCH "$ENV{VSCMD_ARG_TGT_ARCH}")
    endif()
    string(TOLOWER "${MAAGE_VCPKG_ARCH}" MAAGE_VCPKG_ARCH)
    if(MAAGE_VCPKG_ARCH MATCHES "^(amd64|x86_64)$")
        set(MAAGE_VCPKG_ARCH x64)
    elseif(MAAGE_VCPKG_ARCH STREQUAL "aarch64")
        set(MAAGE_VCPKG_ARCH arm64)
    elseif(MAAGE_VCPKG_ARCH STREQUAL "win32")
        set(MAAGE_VCPKG_ARCH x86)
    endif()
    if(NOT MAAGE_VCPKG_ARCH MATCHES "^(x86|x64|arm|arm64)$")
        message(FATAL_ERROR "Set the Windows target architecture with -A for Visual Studio, "
            "a target developer command prompt for Ninja, or VCPKG_TARGET_TRIPLET=<target>-windows-static.")
    endif()
    set(VCPKG_TARGET_TRIPLET "${MAAGE_VCPKG_ARCH}-windows-maage-static"
        CACHE STRING "MaaGE static Windows dependency triplet")
endif()

# Separate names keep vcpkg binary caches distinct from the stock /O2 triplets.
set(MAAGE_VCPKG_TRIPLETS "${CMAKE_BINARY_DIR}/vcpkg-triplets")
file(MAKE_DIRECTORY "${MAAGE_VCPKG_TRIPLETS}")
file(WRITE "${MAAGE_VCPKG_TRIPLETS}/${MAAGE_VCPKG_ARCH}-windows-maage-static.cmake"
    "set(VCPKG_TARGET_ARCHITECTURE ${MAAGE_VCPKG_ARCH})\n"
    "set(VCPKG_CRT_LINKAGE static)\nset(VCPKG_LIBRARY_LINKAGE static)\n"
    "set(VCPKG_C_FLAGS_RELEASE \"/O1 /Gy /Gw\")\n"
    "set(VCPKG_CXX_FLAGS_RELEASE \"/O1 /Gy /Gw\")\n")
list(PREPEND VCPKG_OVERLAY_TRIPLETS "${MAAGE_VCPKG_TRIPLETS}")
message(STATUS "MaaGE static Windows dependencies: ${VCPKG_TARGET_TRIPLET}")

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
