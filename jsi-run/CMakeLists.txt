# hermes-jsi-run: a JSI host to run scripts with native libraries (see README.md). Apache License 2.0.
cmake_minimum_required(VERSION 3.22)
project(jsi-run)

set(CMAKE_CXX_STANDARD 17)

set(HERMES_SRC_DIR "" CACHE PATH "Path to Hermes source directory")
set(HERMES_BUILD_DIR "" CACHE PATH "Path to Hermes build directory")
if (NOT EXISTS "${HERMES_SRC_DIR}/API/jsi/jsi/jsi.h")
    message(FATAL_ERROR "HERMES_SRC_DIR does not contain API/jsi/jsi/jsi.h")
endif ()
find_library(HERMES_RUNTIME_LIB NAMES hermesvm hermes
             PATHS "${HERMES_BUILD_DIR}/lib" "${HERMES_BUILD_DIR}/API/hermes" NO_DEFAULT_PATH REQUIRED)
find_library(HERMES_JSI_LIB NAMES jsi PATHS "${HERMES_BUILD_DIR}/jsi" NO_DEFAULT_PATH REQUIRED)
find_package(Threads REQUIRED)

# jslib.js and ../common/console.js, embedded as C++ raw string literals.
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/embed.cmake")
embed_js("${CMAKE_CURRENT_SOURCE_DIR}/jslib.js" "${CMAKE_CURRENT_BINARY_DIR}/jslib.js.inc")
embed_js("${CMAKE_CURRENT_SOURCE_DIR}/../common/console.js" "${CMAKE_CURRENT_BINARY_DIR}/console.js.inc")

set(HERMES_INCLUDES "${HERMES_SRC_DIR}/API" "${HERMES_SRC_DIR}/API/jsi" "${HERMES_SRC_DIR}/public")

add_executable(hermes-jsi-run jsi-run.cpp)
target_include_directories(hermes-jsi-run PRIVATE ${HERMES_INCLUDES} "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(hermes-jsi-run PRIVATE ${HERMES_RUNTIME_LIB} ${HERMES_JSI_LIB} Threads::Threads ${CMAKE_DL_LIBS})

# The example library: add() and a Promise resolved from another thread.
add_library(jsirunworker SHARED example/worker.cpp)
target_include_directories(jsirunworker PRIVATE ${HERMES_INCLUDES} "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(jsirunworker PRIVATE ${HERMES_RUNTIME_LIB} Threads::Threads)

# Found next to it when packaged (bin/ and lib/ side by side), and in the Hermes build when run from here.
get_filename_component(HERMES_RUNTIME_DIR "${HERMES_RUNTIME_LIB}" DIRECTORY)
if (APPLE)
    set_target_properties(hermes-jsi-run PROPERTIES BUILD_RPATH "@loader_path/../lib;${HERMES_RUNTIME_DIR}")
else ()
    set_target_properties(hermes-jsi-run PROPERTIES BUILD_RPATH "$ORIGIN/../lib;${HERMES_RUNTIME_DIR}")
endif ()
