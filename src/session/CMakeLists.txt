# Include this directory only after the shared Core/World targets are declared:
#   add_subdirectory(src/session)
# Link MaaGE::Session from Client and Server after the Phase 7 integration.
if(NOT TARGET MaaGE::World OR NOT TARGET MaaGE::Thread OR NOT TARGET MaaGE::Status)
    message(FATAL_ERROR "MaaGE::Session requires MaaGE::World, MaaGE::Thread and MaaGE::Status")
endif()
add_library(maage_session STATIC local_session.c)
add_library(MaaGE::Session ALIAS maage_session)
maage_configure_portable_target(maage_session)
target_include_directories(maage_session PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(maage_session PUBLIC MaaGE::World MaaGE::Status MaaGE::Thread)
