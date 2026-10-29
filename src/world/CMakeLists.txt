add_library(maage_world STATIC content.c blocks.c chunk.c world.c raycast.c mesh.c player.c items.c recipes.c recipes_builtin.c crafting.c entities.c gameplay.c
    seed.c biomes.c generation.c generation_features.c generation_surface.c lighting.c store.c environment.c random_ticks.c)
add_library(MaaGE::World ALIAS maage_world)
maage_configure_portable_target(maage_world)
target_include_directories(maage_world PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(maage_world PUBLIC MaaGE::Status PRIVATE MaaGE::Storage MaaGE::Jobs MaaGE::Log)
if(NOT WIN32)
    target_link_libraries(maage_world PUBLIC m)
endif()
