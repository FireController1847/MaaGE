find_package(ZLIB 1.2.3 REQUIRED)
add_library(maage_world STATIC content.c content_presentation.c registry_policy.c presentation.c fluids.c block_geometry.c containers.c creative.c sounds.c blocks.c chunk.c world.c raycast.c mesh.c player.c items.c recipes.c crafting.c smelting.c entities.c entity_types.c models.c gameplay.c
    seed.c biomes.c generation.c lighting.c store.c region_store.c environment.c random_ticks.c support.c)
add_library(MaaGE::World ALIAS maage_world)
maage_configure_portable_target(maage_world)
target_include_directories(maage_world PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(maage_world PUBLIC MaaGE::Status MaaGE::Geometry PRIVATE maage_core MaaGE::Storage MaaGE::Jobs MaaGE::Thread MaaGE::Log ZLIB::ZLIB)
if(NOT WIN32)
    target_link_libraries(maage_world PUBLIC m)
endif()

add_library(maage_base STATIC
    ../base/base.c ../base/sounds.c ../base/sound_ambience.c ../base/containers.c ../base/creative.c ../base/fluids.c ../base/bindings.c ../base/blocks.c ../base/items.c ../base/biomes.c
    ../base/recipes.c ../base/furnace_content.c ../base/furnace.c ../base/furnace_entity.c
    ../base/models.c ../base/entities.c ../base/entity_models.c
    ../base/interaction.c ../base/placement.c ../base/drops.c ../base/support.c
    ../base/states.c ../base/random_ticks.c ../base/leaves.c ../base/presentation.c ../base/textures.c
    ../base/generation_content.c ../base/generation.c ../base/generation_features.c ../base/generation_surface.c)
add_library(MaaGE::Base ALIAS maage_base)
maage_configure_portable_target(maage_base)
target_include_directories(maage_base PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(maage_base PUBLIC MaaGE::World)
