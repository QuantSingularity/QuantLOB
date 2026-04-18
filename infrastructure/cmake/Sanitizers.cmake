# Sanitizers.cmake
# Activated by QUANTLOB_ENABLE_ASAN / TSAN / UBSAN options in CMakeLists.txt

function(target_enable_sanitizers target)
    if(QUANTLOB_ENABLE_ASAN)
        message(STATUS "ASan enabled for ${target}")
        target_compile_options(${target} PUBLIC -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(${target}    PUBLIC -fsanitize=address)
    endif()

    if(QUANTLOB_ENABLE_TSAN)
        message(STATUS "TSan enabled for ${target}")
        target_compile_options(${target} PUBLIC -fsanitize=thread)
        target_link_options(${target}    PUBLIC -fsanitize=thread)
    endif()

    if(QUANTLOB_ENABLE_UBSAN)
        message(STATUS "UBSan enabled for ${target}")
        target_compile_options(${target} PUBLIC -fsanitize=undefined)
        target_link_options(${target}    PUBLIC -fsanitize=undefined)
    endif()
endfunction()
