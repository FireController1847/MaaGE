add_library(maage_world STATIC blocks.c chunk.c world.c mesh.c player.c
    seed.c biomes.c generation.c lighting.c store.c)
add_library(MaaGE::World ALIAS maage_world)
maage_configure_portable_target(maage_world)
target_include_directories(maage_world PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(maage_world PRIVATE MaaGE::Storage MaaGE::Jobs MaaGE::Log)
if(NOT WIN32)
    target_link_libraries(maage_world PUBLIC m)
endif()
