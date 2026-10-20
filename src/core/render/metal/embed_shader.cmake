file(READ "${MAAGE_METAL_SOURCE}" MAAGE_METAL_TEXT)
string(REPLACE "\\" "\\\\" MAAGE_METAL_TEXT "${MAAGE_METAL_TEXT}")
string(REPLACE "\"" "\\\"" MAAGE_METAL_TEXT "${MAAGE_METAL_TEXT}")
string(REPLACE "\n" "\\n\"\n\"" MAAGE_METAL_TEXT "${MAAGE_METAL_TEXT}")
file(WRITE "${MAAGE_METAL_HEADER}"
    "/* Generated from mesh.metal; do not edit. */\nstatic const char maage_metal_mesh_source[] = \"${MAAGE_METAL_TEXT}\";\n")
