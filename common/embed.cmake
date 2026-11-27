# embed_js(<file.js> <out.inc>): the file as a C++ raw string literal, to `#include` as an expression.
# Licensed under the Apache License 2.0 (see LICENSE).
function(embed_js src out)
    file(READ "${src}" content)
    file(WRITE "${out}" "R\"JSLIB(${content})JSLIB\"\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${src}")
endfunction()
