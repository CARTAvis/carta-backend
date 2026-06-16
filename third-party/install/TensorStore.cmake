include(FetchContent)

set(TENSORSTORE_USE_SYSTEM_ZLIB ON CACHE BOOL "" FORCE)
set(TENSORSTORE_USE_SYSTEM_ZSTD ON CACHE BOOL "" FORCE)
set(TENSORSTORE_USE_SYSTEM_PROTOBUF OFF CACHE BOOL "" FORCE)

# Point TensorStore at CARTA's vendored nlohmann_json instead of requiring a system install.
set(TENSORSTORE_USE_SYSTEM_NLOHMANN_JSON ON CACHE BOOL "" FORCE)
file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/nlohmann_json-config.cmake"
    "if(NOT TARGET nlohmann_json::nlohmann_json)\n"
    "    message(FATAL_ERROR \"nlohmann_json redirect: target missing; add_subdirectory(third-party/nlohmann_json) must run before include(TensorStore)\")\n"
    "endif()\n"
    "set(nlohmann_json_FOUND TRUE)\n")

FetchContent_Declare(
    tensorstore
    URL "https://github.com/google/tensorstore/archive/refs/tags/v0.1.84.tar.gz"
    URL_HASH SHA256=d86fe9dca4b69e5c8b488562351dc654bce1400f5981f922e4be9ba4fafc95ce
)

FetchContent_MakeAvailable(tensorstore)
