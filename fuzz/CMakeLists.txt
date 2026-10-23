# libFuzzer targets. Needs Clang: configure with
#   cmake -S . -B build/fuzz -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DONF_BUILD_FUZZERS=ON
# The targets are built with AddressSanitizer, UndefinedBehaviorSanitizer and libFuzzer instrumentation.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "ONF_BUILD_FUZZERS needs Clang")
endif()

function(onf_fuzzer name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE onf::onf)
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
endfunction()

onf_fuzzer(fuzz_document fuzz_document.cpp)
onf_fuzzer(fuzz_strokes fuzz_strokes.cpp)
onf_fuzzer(fuzz_meta fuzz_meta.cpp)
onf_fuzzer(fuzz_roundtrip fuzz_roundtrip.cpp)
