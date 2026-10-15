add_library(nv_core STATIC core.c)
nv_configure_portable_target(nv_core)
target_include_directories(nv_core PUBLIC "${PROJECT_SOURCE_DIR}/include")
