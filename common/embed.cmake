# embed_js(<file.js> <out.inc>): the file as a C++ raw string literal, to `#include` as an expression. A change to the
# file reconfigures (each file registered once: Ninja rejects a file listed twice, as console.js would be).
# Licensed under the Apache License 2.0 (see LICENSE).
function(embed_js src out)
    file(READ "${src}" content)
    file(WRITE "${out}" "R\"JSLIB(${content})JSLIB\"\n")
    get_filename_component(src "${src}" REALPATH)
    get_property(seen GLOBAL PROPERTY EMBED_JS_SOURCES)
    if (NOT src IN_LIST seen)
        set_property(GLOBAL APPEND PROPERTY EMBED_JS_SOURCES "${src}")
        set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${src}")
    endif ()
endfunction()
