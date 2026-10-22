add_executable(onf_tests
    unit/main.cpp
    unit/test_build.cpp
    unit/test_bytecursor.cpp
    unit/test_result.cpp
    unit/test_errors.cpp
    unit/test_diagnostics.cpp
    unit/test_limits.cpp
    unit/test_stroke_parser.cpp
    unit/test_strokes_file.cpp
    unit/test_types.cpp
    unit/test_varint.cpp
)
target_link_libraries(onf_tests PRIVATE onf::onf)
target_include_directories(onf_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(onf_tests PRIVATE ONF_VECTORS_DIR="${PROJECT_SOURCE_DIR}/tests/vectors")
target_include_directories(onf_tests SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)

if(ONF_SANITIZE)
    target_compile_definitions(onf_tests PRIVATE ONF_EXPECT_SANITIZERS=1)
endif()

add_test(NAME onf_tests COMMAND onf_tests)
