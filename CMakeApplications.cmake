# GUI identity is shared by CMake target configuration and build-time metadata.
set(MAAGE_LAUNCHER_NAME "MaaGE Launcher")
set(MAAGE_LAUNCHER_IDENTIFIER "org.maage.launcher")
set(MAAGE_CLIENT_NAME "MaaGE Client")
set(MAAGE_CLIENT_IDENTIFIER "org.maage.client")
set(MAAGE_SERVER_GUI_NAME "MaaGE Server GUI")
set(MAAGE_SERVER_GUI_IDENTIFIER "org.maage.server-gui")
if(CMAKE_SCRIPT_MODE_FILE)
    return()
endif()

include(GNUInstallDirs)
function(maage_configure_gui_application target application)
    string(TOUPPER "${application}" MAAGE_APPLICATION)
    set(MAAGE_GUI_NAME "${MAAGE_${MAAGE_APPLICATION}_NAME}")
    set(MAAGE_GUI_IDENTIFIER "${MAAGE_${MAAGE_APPLICATION}_IDENTIFIER}")
    if(application STREQUAL "server_gui")
        set(MAAGE_PRODUCT SERVER)
    else()
        set(MAAGE_PRODUCT "${MAAGE_APPLICATION}")
    endif()
    target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src/core/application/entry.c")
    target_compile_definitions(${target} PRIVATE "MAAGE_APPLICATION_KEY=\"${application}\"")
    set_target_properties(${target} PROPERTIES
        WIN32_EXECUTABLE "${MAAGE_PLATFORM_WINDOWS}"
        MACOSX_BUNDLE "${MAAGE_PLATFORM_MACOS}")
    if(MAAGE_PLATFORM_MACOS)
        set(MAAGE_NUMERIC_VERSION "${MAAGE_${MAAGE_PRODUCT}_VERSION_MAJOR}.${MAAGE_${MAAGE_PRODUCT}_VERSION_MINOR}.${MAAGE_${MAAGE_PRODUCT}_VERSION_PATCH}")
        set_target_properties(${target} PROPERTIES
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
    elseif(MAAGE_PLATFORM_LINUX)
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
