add_library(maage_core STATIC core.c)
maage_configure_portable_target(maage_core)
target_include_directories(maage_core PUBLIC "${PROJECT_SOURCE_DIR}/include")
