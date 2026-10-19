# Build-time descriptor; never executes the Client.
file(READ "${MAAGE_VERSION_HEADER}" MAAGE_HEADER)
foreach(field SEMANTIC_VERSION GIT_HASH)
    string(REGEX MATCH "#define MAAGE_CLIENT_${field} \"([^\"]+)\"" MAAGE_MATCH "${MAAGE_HEADER}")
    if(NOT MAAGE_MATCH)
        message(FATAL_ERROR "Missing Client ${field}")
    endif()
    set(MAAGE_${field} "${CMAKE_MATCH_1}")
endforeach()
file(SHA256 "${MAAGE_EXECUTABLE}" MAAGE_SHA256)
get_filename_component(MAAGE_FILENAME "${MAAGE_EXECUTABLE}" NAME)
get_filename_component(MAAGE_DIRECTORY "${MAAGE_MANIFEST}" DIRECTORY)
file(MAKE_DIRECTORY "${MAAGE_DIRECTORY}")
file(CONFIGURE OUTPUT "${MAAGE_MANIFEST}" CONTENT [=[
{
  "schema": 1,
  "application": "maage-client",
  "version": "@MAAGE_SEMANTIC_VERSION@",
  "build": "@MAAGE_GIT_HASH@",
  "build_type": "@MAAGE_BUILD_TYPE@",
  "platform": "@MAAGE_PLATFORM@",
  "architecture": "@MAAGE_ARCH@",
  "executable": "@MAAGE_FILENAME@",
  "sha256": "@MAAGE_SHA256@",
  "protocol": null
}
]=] @ONLY)
