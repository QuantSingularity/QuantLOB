# CompilerWarnings.cmake
# Usage: include(cmake/CompilerWarnings.cmake)
#        target_apply_warnings(my_target)

function(target_apply_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:
            -Wall -Wextra -Wpedantic
            -Wno-unused-parameter
            -Wshadow
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wcast-align
            -Wformat=2
        >
        $<$<CXX_COMPILER_ID:MSVC>:
            /W4 /permissive-
        >
    )
endfunction()
