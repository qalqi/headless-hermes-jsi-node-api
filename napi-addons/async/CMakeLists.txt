cmake_minimum_required(VERSION 3.22)
project(napi-addon-async C)

set(HERMES_SRC_DIR "" CACHE PATH "Path to Hermes source directory")
set(HERMES_BUILD_DIR "" CACHE PATH "Path to Hermes build directory")

if (NOT HERMES_SRC_DIR)
    message(FATAL_ERROR "Please specify HERMES_SRC_DIR")
endif ()
if (NOT EXISTS "${HERMES_SRC_DIR}/include/hermes/napi/node_api.h")
    message(FATAL_ERROR "HERMES_SRC_DIR does not contain include/hermes/napi/node_api.h")
endif ()

find_package(Threads REQUIRED)

add_library(napi_async SHARED async.c)
target_include_directories(napi_async PRIVATE "${HERMES_SRC_DIR}/include")
target_compile_definitions(napi_async PRIVATE NAPI_VERSION=10)
target_link_libraries(napi_async PRIVATE Threads::Threads)
set_target_properties(napi_async PROPERTIES
    OUTPUT_NAME "async_addon"
    PREFIX ""
    SUFFIX ".node"
)
if (APPLE)
    target_link_options(napi_async PRIVATE -undefined dynamic_lookup)
endif ()
