add_library(maage_render STATIC render.c render_shared.c)
add_library(MaaGE::Render ALIAS maage_render)
maage_configure_portable_target(maage_render)
target_include_directories(maage_render PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(maage_render PUBLIC MaaGE::Window PRIVATE MaaGE::Log)

set(MAAGE_DEFAULT_VULKAN OFF)
if(MAAGE_PLATFORM_WINDOWS OR MAAGE_PLATFORM_LINUX OR MAAGE_PLATFORM_MACOS)
    set(MAAGE_DEFAULT_VULKAN ON)
endif()
option(MAAGE_ENABLE_METAL "Build native Metal on macOS" ${MAAGE_PLATFORM_MACOS})
option(MAAGE_ENABLE_VULKAN "Build Vulkan (statically linked MoltenVK on macOS)" ${MAAGE_DEFAULT_VULKAN})
option(MAAGE_ENABLE_OPENGL "Build modern OpenGL when its system SDK is available" ON)
option(MAAGE_ENABLE_OPENGL_LEGACY "Build OpenGL 1.1 compatibility when available" ON)
option(MAAGE_ENABLE_SOFTWARE "Build the portable CPU framebuffer renderer" ON)

if(MAAGE_PLATFORM_MACOS AND MAAGE_ENABLE_METAL)
    if(NOT TARGET MaaGE::Thread)
        add_subdirectory("${PROJECT_SOURCE_DIR}/src/core/thread" "${PROJECT_BINARY_DIR}/src/core/thread")
    endif()
    enable_language(OBJC)
    find_library(MAAGE_METAL_FRAMEWORK Metal REQUIRED)
    find_library(MAAGE_QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(MAAGE_RENDER_APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(MAAGE_METAL_PERFORMANCE_SHADERS_FRAMEWORK MetalPerformanceShaders REQUIRED)
    set(MAAGE_METAL_GENERATED "${PROJECT_BINARY_DIR}/generated/maage_mesh")
    # Embed one authoritative MSL asset for the native runtime shader compiler;
    # command-line-tools installations need no optional offline Metal toolchain.
    add_custom_command(OUTPUT "${MAAGE_METAL_GENERATED}.h"
        COMMAND "${CMAKE_COMMAND}" "-DMAAGE_METAL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/metal/mesh.metal"
            "-DMAAGE_MATERIAL_SOURCE=${PROJECT_SOURCE_DIR}/include/maage/geometry_materials.h"
            "-DMAAGE_METAL_HEADER=${MAAGE_METAL_GENERATED}.h"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/metal/embed_shader.cmake"
        DEPENDS metal/mesh.metal metal/embed_shader.cmake render_shader_common.h render_shader_defs.h
            "${PROJECT_SOURCE_DIR}/include/maage/geometry_materials.h" VERBATIM)
    target_sources(maage_render PRIVATE metal/render_metal.m "${MAAGE_METAL_GENERATED}.h")
    # Apple's Metal Performance Shaders SDK headers use C99+ constructs such
    # as inline. The shared renderer is C90-first, so isolate this language
    # exception to the single macOS Objective-C backend translation unit.
    # Source compile options follow the target's inherited -std=c90 flag.
    set_source_files_properties(metal/render_metal.m PROPERTIES
        COMPILE_OPTIONS "-std=gnu11")
    target_compile_definitions(maage_render PRIVATE MAAGE_RENDER_HAS_METAL)
    target_compile_options(maage_render PRIVATE "$<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc>")
    target_link_libraries(maage_render PRIVATE "${MAAGE_METAL_FRAMEWORK}"
        "${MAAGE_QUARTZCORE_FRAMEWORK}" "${MAAGE_RENDER_APPKIT_FRAMEWORK}"
        "${MAAGE_METAL_PERFORMANCE_SHADERS_FRAMEWORK}" MaaGE::Thread)
endif()
if((MAAGE_PLATFORM_WINDOWS OR MAAGE_PLATFORM_LINUX OR MAAGE_PLATFORM_MACOS) AND MAAGE_ENABLE_VULKAN)
    # Required for Vulkan-enabled builds: never silently drop the native backend.
    if(MAAGE_PLATFORM_WINDOWS)
        find_package(VulkanHeaders CONFIG REQUIRED)
        set(MAAGE_VULKAN_TARGET Vulkan::Headers)
    elseif(MAAGE_PLATFORM_MACOS)
        # Link the archive directly so no loader or adjacent runtime is needed.
        find_path(MAAGE_VULKAN_INCLUDE_DIR vulkan/vulkan.h
            HINTS "$ENV{VULKAN_SDK}/include" REQUIRED)
        find_file(MAAGE_MOLTENVK_STATIC_LIBRARY NAMES libMoltenVK.a
            HINTS "$ENV{VULKAN_SDK}/lib" PATH_SUFFIXES lib REQUIRED)
        if(NOT EXISTS "${MAAGE_MOLTENVK_STATIC_LIBRARY}" OR IS_DIRECTORY "${MAAGE_MOLTENVK_STATIC_LIBRARY}"
                OR NOT MAAGE_MOLTENVK_STATIC_LIBRARY MATCHES "\\.a$")
            message(FATAL_ERROR "MAAGE_MOLTENVK_STATIC_LIBRARY must identify a target-compatible MoltenVK static archive.")
        endif()
        add_library(maage_vulkan_moltenvk STATIC IMPORTED)
        set_target_properties(maage_vulkan_moltenvk PROPERTIES
            IMPORTED_LOCATION "${MAAGE_MOLTENVK_STATIC_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MAAGE_VULKAN_INCLUDE_DIR}")
        # MoltenVK is implemented in C++/Objective-C, behind its C Vulkan ABI.
        # System runtime linkage does not require enabling C++ for MaaGE sources.
        find_library(MAAGE_VULKAN_CXX_LIBRARY c++ REQUIRED)
        find_library(MAAGE_VULKAN_OBJC_LIBRARY objc REQUIRED)
        set_property(TARGET maage_vulkan_moltenvk APPEND PROPERTY INTERFACE_LINK_LIBRARIES
            "${MAAGE_VULKAN_CXX_LIBRARY}" "${MAAGE_VULKAN_OBJC_LIBRARY}")
        foreach(MAAGE_VULKAN_FRAMEWORK Metal Foundation QuartzCore CoreGraphics IOSurface IOKit AppKit)
            find_library(MAAGE_VULKAN_${MAAGE_VULKAN_FRAMEWORK}_FRAMEWORK "${MAAGE_VULKAN_FRAMEWORK}" REQUIRED)
            set_property(TARGET maage_vulkan_moltenvk APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                "${MAAGE_VULKAN_${MAAGE_VULKAN_FRAMEWORK}_FRAMEWORK}")
        endforeach()
        set(MAAGE_VULKAN_TARGET maage_vulkan_moltenvk)
    else()
        find_package(Vulkan REQUIRED)
        set(MAAGE_VULKAN_TARGET Vulkan::Vulkan)
    endif()
    # Compile native GLSL once at build time and embed the resulting SPIR-V.
    # Compilers run on the build host even when Vulkan targets are cross-compiled.
    set(MAAGE_VULKAN_TOOL_HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
    if(VCPKG_INSTALLED_DIR)
        if(VCPKG_HOST_TRIPLET)
            list(APPEND MAAGE_VULKAN_TOOL_HINTS
                "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/glslang")
        else()
            # vcpkg resolves its default host triplet internally. This manifest
            # installs glslang as a host dependency; never substitute the target
            # triplet, which can differ even in a native static-library build.
            file(GLOB MAAGE_VULKAN_HOST_TOOL_DIRS LIST_DIRECTORIES true
                "${VCPKG_INSTALLED_DIR}/*/tools/glslang")
            list(LENGTH MAAGE_VULKAN_HOST_TOOL_DIRS MAAGE_VULKAN_HOST_TOOL_COUNT)
            if(MAAGE_VULKAN_HOST_TOOL_COUNT EQUAL 1)
                list(APPEND MAAGE_VULKAN_TOOL_HINTS ${MAAGE_VULKAN_HOST_TOOL_DIRS})
            endif()
        endif()
    endif()
    find_program(MAAGE_GLSLC NAMES glslc HINTS ${MAAGE_VULKAN_TOOL_HINTS}
        NO_CMAKE_FIND_ROOT_PATH)
    find_program(MAAGE_GLSLANG NAMES glslangValidator glslang HINTS ${MAAGE_VULKAN_TOOL_HINTS}
        NO_CMAKE_FIND_ROOT_PATH)
    if(NOT MAAGE_GLSLC AND NOT MAAGE_GLSLANG)
        message(FATAL_ERROR "Vulkan shaders require a host glslc or glslangValidator executable. "
            "Install Vulkan SDK shader tools or glslang-tools; Windows vcpkg provisions glslang[tools]. "
            "If multiple vcpkg tool directories exist, set VCPKG_HOST_TRIPLET or MAAGE_GLSLANG explicitly.")
    endif()
    set(MAAGE_VULKAN_GENERATED "${PROJECT_BINARY_DIR}/generated/vulkan")
    set(MAAGE_VULKAN_SPIRV)
    foreach(MAAGE_SHADER mesh.vert mesh.frag ui.vert ui.frag fullscreen.vert blur.frag)
        string(REPLACE "." "_" MAAGE_SHADER_NAME "${MAAGE_SHADER}")
        set(MAAGE_SHADER_OUTPUT "${MAAGE_VULKAN_GENERATED}/${MAAGE_SHADER_NAME}.spv")
        if(MAAGE_GLSLC)
            set(MAAGE_SHADER_COMMAND "${MAAGE_GLSLC}" --target-env=vulkan1.0 -O)
        else()
            set(MAAGE_SHADER_COMMAND "${MAAGE_GLSLANG}" --target-env vulkan1.0 -V)
        endif()
        add_custom_command(OUTPUT "${MAAGE_SHADER_OUTPUT}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${MAAGE_VULKAN_GENERATED}"
            COMMAND ${MAAGE_SHADER_COMMAND} "-I${PROJECT_SOURCE_DIR}/include"
                "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/shaders/${MAAGE_SHADER}"
                -o "${MAAGE_SHADER_OUTPUT}"
            DEPENDS "vulkan/shaders/${MAAGE_SHADER}" render_shader_common.h render_shader_defs.h
                "${PROJECT_SOURCE_DIR}/include/maage/geometry_materials.h" VERBATIM)
        list(APPEND MAAGE_VULKAN_SPIRV "${MAAGE_SHADER_OUTPUT}")
    endforeach()
    set(MAAGE_VULKAN_HEADER "${MAAGE_VULKAN_GENERATED}/maage_vulkan_shaders.h")
    add_custom_command(OUTPUT "${MAAGE_VULKAN_HEADER}"
        COMMAND "${CMAKE_COMMAND}" "-DMAAGE_VULKAN_DIRECTORY=${MAAGE_VULKAN_GENERATED}"
            "-DMAAGE_VULKAN_HEADER=${MAAGE_VULKAN_HEADER}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/embed_shaders.cmake"
        DEPENDS ${MAAGE_VULKAN_SPIRV} vulkan/embed_shaders.cmake VERBATIM)
    add_library(maage_render_vulkan OBJECT vulkan/render_vulkan.c vulkan/vulkan_platform.c
        "${MAAGE_VULKAN_HEADER}")
    set_target_properties(maage_render_vulkan PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(maage_render_vulkan PRIVATE "${PROJECT_SOURCE_DIR}/include"
        "${MAAGE_VULKAN_GENERATED}")
    target_link_libraries(maage_render_vulkan PRIVATE ${MAAGE_VULKAN_TARGET})
    if(MAAGE_PLATFORM_WINDOWS)
        target_compile_definitions(maage_render_vulkan PRIVATE MAAGE_RENDER_WIN32)
    elseif(MAAGE_PLATFORM_MACOS)
        enable_language(OBJC)
        set_target_properties(maage_render_vulkan PROPERTIES
            OBJC_STANDARD 99 OBJC_STANDARD_REQUIRED ON OBJC_EXTENSIONS OFF)
        target_sources(maage_render_vulkan PRIVATE vulkan/surface_macos.m)
        target_compile_definitions(maage_render_vulkan PRIVATE MAAGE_RENDER_COCOA)
        target_compile_options(maage_render_vulkan PRIVATE "$<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc>")
    else()
        if(MAAGE_WINDOW_ENABLE_X11)
            find_package(X11 REQUIRED)
            target_compile_definitions(maage_render_vulkan PRIVATE MAAGE_RENDER_X11)
            target_link_libraries(maage_render_vulkan PRIVATE X11::X11)
        endif()
        if(MAAGE_WINDOW_ENABLE_WAYLAND)
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(MAAGE_RENDER_WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
            target_compile_definitions(maage_render_vulkan PRIVATE MAAGE_RENDER_WAYLAND)
            target_link_libraries(maage_render_vulkan PRIVATE PkgConfig::MAAGE_RENDER_WAYLAND)
        endif()
    endif()
    target_sources(maage_render PRIVATE $<TARGET_OBJECTS:maage_render_vulkan>)
    target_compile_definitions(maage_render PRIVATE MAAGE_RENDER_HAS_VULKAN)
    target_link_libraries(maage_render PRIVATE ${MAAGE_VULKAN_TARGET})
endif()

if(MAAGE_ENABLE_OPENGL OR MAAGE_ENABLE_OPENGL_LEGACY)
    if(MAAGE_PLATFORM_MACOS OR MAAGE_PLATFORM_WINDOWS OR (MAAGE_PLATFORM_LINUX AND MAAGE_WINDOW_ENABLE_X11))
        find_package(OpenGL QUIET)
        if(OpenGL_FOUND AND TARGET OpenGL::GL)
            target_sources(maage_render PRIVATE opengl/opengl.c)
            target_link_libraries(maage_render PRIVATE OpenGL::GL)
            if(MAAGE_PLATFORM_MACOS)
                enable_language(OBJC)
                target_sources(maage_render PRIVATE opengl/context_macos.m)
                target_compile_options(maage_render PRIVATE "$<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc>")
            elseif(MAAGE_PLATFORM_WINDOWS)
                target_sources(maage_render PRIVATE opengl/context_win32.c)
            else()
                target_sources(maage_render PRIVATE opengl/context_x11.c)
                target_link_libraries(maage_render PRIVATE X11::X11)
            endif()
            if(MAAGE_ENABLE_OPENGL)
                target_sources(maage_render PRIVATE opengl/modern/modern.c)
                target_compile_definitions(maage_render PRIVATE MAAGE_RENDER_HAS_OPENGL)
            endif()
            if(MAAGE_ENABLE_OPENGL_LEGACY)
                target_sources(maage_render PRIVATE opengl/legacy/legacy.c)
                target_compile_definitions(maage_render PRIVATE MAAGE_RENDER_HAS_OPENGL_LEGACY)
            endif()
            message(STATUS "Native OpenGL adapter enabled for ${CMAKE_SYSTEM_NAME}; Wayland contexts are unsupported")
        else()
            message(STATUS "OpenGL system SDK unavailable: optional OpenGL backends are not compiled")
        endif()
    else()
        message(STATUS "No OpenGL context adapter for this window platform: optional backends are not compiled")
    endif()
endif()

if(MAAGE_ENABLE_SOFTWARE)
    target_sources(maage_render PRIVATE software/software.c)
    target_compile_definitions(maage_render PRIVATE MAAGE_RENDER_HAS_SOFTWARE)
endif()
