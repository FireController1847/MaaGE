add_library(maage_server_core STATIC server.c)
add_library(MaaGE::Server ALIAS maage_server_core)
add_dependencies(maage_server_core maage_versions)
maage_configure_portable_target(maage_server_core)
target_link_libraries(maage_server_core PUBLIC maage_core)

if(MAAGE_BUILD_SERVER)
    add_executable(maage_server main.c)
    maage_configure_portable_target(maage_server)
    set_target_properties(maage_server PROPERTIES
        OUTPUT_NAME server WIN32_EXECUTABLE OFF MACOSX_BUNDLE OFF)
    target_link_libraries(maage_server PRIVATE MaaGE::Server)
    install(TARGETS maage_server RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Runtime)
    if(MAAGE_BUILD_TESTS)
        add_test(NAME maage_server_smoke COMMAND maage_server)
        set_tests_properties(maage_server_smoke PROPERTIES PASS_REGULAR_EXPRESSION "MaaGE Server")
    endif()
endif()

