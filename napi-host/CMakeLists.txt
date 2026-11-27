cmake_minimum_required(VERSION 3.22)
project(napi-host)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(HERMES_SRC_DIR "" CACHE PATH "Path to Hermes source directory")
set(HERMES_BUILD_DIR "" CACHE PATH "Path to Hermes build directory")

if (NOT HERMES_SRC_DIR)
    message(FATAL_ERROR "Please specify HERMES_SRC_DIR")
endif ()
if (NOT EXISTS "${HERMES_SRC_DIR}/API/napi/hermes_napi.h")
    message(FATAL_ERROR "HERMES_SRC_DIR does not contain API/napi/hermes_napi.h")
endif ()

if (NOT HERMES_BUILD_DIR)
    message(FATAL_ERROR "Please specify HERMES_BUILD_DIR")
endif ()
if (NOT EXISTS "${HERMES_BUILD_DIR}/bin/hermes${CMAKE_EXECUTABLE_SUFFIX}")
    message(FATAL_ERROR "HERMES_BUILD_DIR does not contain bin/hermes${CMAKE_EXECUTABLE_SUFFIX}")
endif ()

file(GLOB_RECURSE HERMES_STATIC_LIBS "${HERMES_BUILD_DIR}/*.a")
list(FILTER HERMES_STATIC_LIBS EXCLUDE REGEX "/CMakeFiles/")

set(HERMES_NAPI_LIB "")
set(HERMES_VM_A_LIB "")
set(HERMES_LLVH_SUPPORT_LIB "")
set(HERMES_LLVH_DEMANGLE_LIB "")
foreach (_lib IN LISTS HERMES_STATIC_LIBS)
    if (_lib MATCHES "/libhermesNapi\\.a$")
        set(HERMES_NAPI_LIB "${_lib}")
    elseif (_lib MATCHES "/libhermesvm_a\\.a$")
        set(HERMES_VM_A_LIB "${_lib}")
    elseif (_lib MATCHES "/libLLVHSupport\\.a$")
        set(HERMES_LLVH_SUPPORT_LIB "${_lib}")
    elseif (_lib MATCHES "/libLLVHDemangle\\.a$")
        set(HERMES_LLVH_DEMANGLE_LIB "${_lib}")
    endif ()
endforeach ()

if (NOT HERMES_NAPI_LIB)
    message(FATAL_ERROR "libhermesNapi.a not found in HERMES_BUILD_DIR (build Hermes with -DHERMES_ENABLE_NAPI=ON)")
endif ()
if (NOT HERMES_VM_A_LIB)
    message(FATAL_ERROR "libhermesvm_a.a not found in HERMES_BUILD_DIR")
endif ()

find_package(Threads REQUIRED)

add_executable(napi-host napi-host.cpp)

target_include_directories(napi-host PRIVATE
    "${HERMES_SRC_DIR}/include"
    "${HERMES_SRC_DIR}/public"
    "${HERMES_SRC_DIR}/API"
    "${HERMES_SRC_DIR}/external/llvh/include"
    "${HERMES_SRC_DIR}/external/llvh/gen/include" # pre-generated llvh headers (Attributes.inc)
    "${HERMES_BUILD_DIR}/external/llvh/include"
    "${HERMES_BUILD_DIR}/lib/config" # libhermesvm-config.h, generated: the VM build flags (pointer compression, GC)
)
if (EXISTS "${HERMES_BUILD_DIR}/include")
    target_include_directories(napi-host PRIVATE "${HERMES_BUILD_DIR}/include")
endif ()

target_compile_definitions(napi-host PRIVATE NAPI_VERSION=10)
if (EXISTS "${HERMES_BUILD_DIR}/CMakeCache.txt")
    file(STRINGS "${HERMES_BUILD_DIR}/CMakeCache.txt" _hermes_build_type REGEX "^CMAKE_BUILD_TYPE:")
    if (_hermes_build_type MATCHES "=(Release|RelWithDebInfo|MinSizeRel)")
        target_compile_definitions(napi-host PRIVATE NDEBUG)
    endif ()
endif ()

if (APPLE)
    target_link_libraries(napi-host PRIVATE
        "-Wl,-force_load,${HERMES_NAPI_LIB}"
        "${HERMES_VM_A_LIB}"
    )
    target_link_options(napi-host PRIVATE -Wl,-export_dynamic)
elseif (UNIX)
    target_link_libraries(napi-host PRIVATE
        "-Wl,--whole-archive"
        "${HERMES_NAPI_LIB}"
        "-Wl,--no-whole-archive"
        "${HERMES_VM_A_LIB}"
    )
    target_link_options(napi-host PRIVATE -rdynamic)
endif ()

if (HERMES_LLVH_SUPPORT_LIB)
    target_link_libraries(napi-host PRIVATE "${HERMES_LLVH_SUPPORT_LIB}")
endif ()
if (HERMES_LLVH_DEMANGLE_LIB)
    target_link_libraries(napi-host PRIVATE "${HERMES_LLVH_DEMANGLE_LIB}")
endif ()

target_link_libraries(napi-host PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if (APPLE)
    target_link_libraries(napi-host PRIVATE "-framework CoreFoundation")
elseif (UNIX)
    target_link_libraries(napi-host PRIVATE m)
endif ()
