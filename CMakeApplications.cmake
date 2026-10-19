# GUI identity is shared by CMake target configuration and build-time metadata.
set(MAAGE_LAUNCHER_NAME "MaaGE")
set(MAAGE_LAUNCHER_IDENTIFIER "org.maage.launcher")
set(MAAGE_CLIENT_NAME "MaaGE Client")
set(MAAGE_CLIENT_IDENTIFIER "org.maage.client")
if(CMAKE_SCRIPT_MODE_FILE)
    return()
endif()

include(GNUInstallDirs)
# Canonical metadata spelling across supported native/cross toolchains.
function(maage_client_target_architecture output)
    set(MAAGE_ARCH_PARTS "")
    foreach(part IN LISTS MAAGE_TARGET_ARCHITECTURE)
        string(TOLOWER "${part}" part)
        if(part MATCHES "^(amd64|x64|x86_64)$")
            set(part x86_64)
        elseif(part MATCHES "^(aarch64|arm64)$")
            set(part arm64)
        elseif(part MATCHES "^(i[3-6]86|win32|x86)$")
            set(part x86)
        endif()
        list(APPEND MAAGE_ARCH_PARTS "${part}")
    endforeach()
    list(JOIN MAAGE_ARCH_PARTS "," MAAGE_ARCH_RESULT)
    set(${output} "${MAAGE_ARCH_RESULT}" PARENT_SCOPE)
endfunction()
function(maage_configure_gui_application target application)
    cmake_parse_arguments(MAAGE_GUI "LAUNCHER_MANAGED" "" "" ${ARGN})
    string(TOUPPER "${application}" MAAGE_APPLICATION)
    set(MAAGE_GUI_NAME "${MAAGE_${MAAGE_APPLICATION}_NAME}")
    set(MAAGE_GUI_IDENTIFIER "${MAAGE_${MAAGE_APPLICATION}_IDENTIFIER}")
    set(MAAGE_PRODUCT "${MAAGE_APPLICATION}")
    target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src/core/application/entry.c")
    target_compile_definitions(${target} PRIVATE "MAAGE_APPLICATION_KEY=\"${application}\"")
    set_target_properties(${target} PROPERTIES
        WIN32_EXECUTABLE "${MAAGE_PLATFORM_WINDOWS}"
        MACOSX_BUNDLE OFF)
    if(MAAGE_PLATFORM_WINDOWS)
        target_link_libraries(${target} PRIVATE shell32)
    elseif(MAAGE_PLATFORM_MACOS AND (NOT MAAGE_GUI_LAUNCHER_MANAGED OR application STREQUAL "client"))
        if(application STREQUAL "client")
            set(MAAGE_GUI_NAME "${MAAGE_CLIENT_ARTIFACT}")
        endif()
        set(MAAGE_NUMERIC_VERSION "${MAAGE_${MAAGE_PRODUCT}_VERSION_MAJOR}.${MAAGE_${MAAGE_PRODUCT}_VERSION_MINOR}.${MAAGE_${MAAGE_PRODUCT}_VERSION_PATCH}")
        set_target_properties(${target} PROPERTIES
            MACOSX_BUNDLE ON
            OUTPUT_NAME "${MAAGE_GUI_NAME}"
            MACOSX_BUNDLE_BUNDLE_NAME "${MAAGE_GUI_NAME}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${MAAGE_GUI_IDENTIFIER}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${MAAGE_NUMERIC_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${MAAGE_NUMERIC_VERSION}")
        # The version target writes the complete plist with the same HEAD snapshot
        # as the version headers. Refresh even when no relink is otherwise needed.
        add_custom_target(${target}_metadata
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:${target}>/Contents"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${PROJECT_BINARY_DIR}/generated/maage/${application}_Info.plist"
                "$<TARGET_BUNDLE_DIR:${target}>/Contents/Info.plist"
            DEPENDS maage_versions VERBATIM)
        add_dependencies(${target} ${target}_metadata)
    elseif(MAAGE_PLATFORM_LINUX AND NOT MAAGE_GUI_LAUNCHER_MANAGED)
        set(MAAGE_DESKTOP_DIR "${PROJECT_BINARY_DIR}/generated/applications")
        file(MAKE_DIRECTORY "${MAAGE_DESKTOP_DIR}")
        file(WRITE "${MAAGE_DESKTOP_DIR}/${application}.desktop.in"
            "[Desktop Entry]\nType=Application\nName=${MAAGE_GUI_NAME}\nExec=\"@MAAGE_DESKTOP_EXECUTABLE@\"\nTerminal=false\nCategories=Game;\n")
        if(IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}")
            set(MAAGE_EXEC_DIR "${CMAKE_INSTALL_BINDIR}")
        else()
            set(MAAGE_EXEC_DIR "\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
        endif()
        # Resolve the install prefix at install time, including --prefix overrides.
        # DESTDIR is a staging location, not part of the installed desktop command.
        install(CODE "
            set(MAAGE_DESKTOP_EXECUTABLE \"${MAAGE_EXEC_DIR}/$<TARGET_FILE_NAME:${target}>\")
            string(REPLACE \"%\" \"%%\" MAAGE_DESKTOP_EXECUTABLE \"\${MAAGE_DESKTOP_EXECUTABLE}\")
            configure_file(\"${MAAGE_DESKTOP_DIR}/${application}.desktop.in\"
                \"${MAAGE_DESKTOP_DIR}/${application}.desktop\" @ONLY)
            " COMPONENT Runtime)
        install(FILES "${MAAGE_DESKTOP_DIR}/${application}.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
            RENAME "${MAAGE_GUI_IDENTIFIER}.desktop" COMPONENT Runtime)
    endif()
    install(TARGETS ${target}
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Runtime
        BUNDLE DESTINATION Applications COMPONENT Runtime)
endfunction()
