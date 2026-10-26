file(READ "${MAAGE_METAL_SOURCE}" MAAGE_METAL_TEXT)
get_filename_component(MAAGE_RENDER_DIRECTORY "${MAAGE_METAL_SOURCE}" DIRECTORY)
get_filename_component(MAAGE_RENDER_DIRECTORY "${MAAGE_RENDER_DIRECTORY}" DIRECTORY)
file(READ "${MAAGE_RENDER_DIRECTORY}/render_shader_common.h" MAAGE_SHADER_COMMON)
file(READ "${MAAGE_RENDER_DIRECTORY}/render_shader_defs.h" MAAGE_SHADER_DEFS)
file(READ "${MAAGE_MATERIAL_SOURCE}" MAAGE_SHADER_MATERIALS)
string(REPLACE "#include \"maage/geometry_materials.h\"" "${MAAGE_SHADER_MATERIALS}"
    MAAGE_SHADER_DEFS "${MAAGE_SHADER_DEFS}")
string(REPLACE "#include \"render_shader_defs.h\"" "${MAAGE_SHADER_DEFS}"
    MAAGE_SHADER_COMMON "${MAAGE_SHADER_COMMON}")
string(REPLACE "#include \"../render_shader_common.h\"" "${MAAGE_SHADER_COMMON}"
    MAAGE_METAL_TEXT "${MAAGE_METAL_TEXT}")
string(REPLACE "\r\n" "\n" MAAGE_METAL_TEXT "${MAAGE_METAL_TEXT}")
string(REPLACE "\\" "\\\\" MAAGE_METAL_TEXT "${MAAGE_METAL_TEXT}")
string(REPLACE "\"" "\\\"" MAAGE_METAL_TEXT "${MAAGE_METAL_TEXT}")
string(REPLACE "\n" "\\n\"\n\"" MAAGE_METAL_TEXT "${MAAGE_METAL_TEXT}")
file(WRITE "${MAAGE_METAL_HEADER}"
    "/* Generated from mesh.metal; do not edit. */\nstatic const char maage_metal_mesh_source[] = \"${MAAGE_METAL_TEXT}\";\n")
