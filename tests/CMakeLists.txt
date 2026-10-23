add_executable(onf_tests
    unit/main.cpp
    unit/test_build.cpp
    unit/test_bytecursor.cpp
    unit/test_result.cpp
    unit/test_document.cpp
    unit/test_assets.cpp
    unit/test_errors.cpp
    unit/test_ink_vectors.cpp
    unit/test_diagnostics.cpp
    unit/test_json.cpp
    unit/test_limits.cpp
    unit/test_malformed_fixtures.cpp
    unit/test_manifest.cpp
    unit/test_page_meta.cpp
    unit/test_paths.cpp
    unit/test_real_archives.cpp
    unit/test_sha256.cpp
    unit/test_stroke_parser.cpp
    unit/test_strokes_file.cpp
    unit/test_types.cpp
    unit/test_utf8.cpp
    unit/test_values.cpp
    unit/test_zip_archive.cpp
    unit/test_varint.cpp
    unit/test_version_policy.cpp
    unit/test_writer.cpp
)
target_link_libraries(onf_tests PRIVATE onf::onf ZLIB::ZLIB)
target_include_directories(onf_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(onf_tests PRIVATE ONF_VECTORS_DIR="${PROJECT_SOURCE_DIR}/tests/vectors"
                                             ONF_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures")
target_include_directories(onf_tests SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)

if(ONF_SANITIZE)
    target_compile_definitions(onf_tests PRIVATE ONF_EXPECT_SANITIZERS=1)
endif()

add_test(NAME onf_tests COMMAND onf_tests)

# The command line tool is started as a program; these tests need python3.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME onf_reader_cli
             COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/cli/test_reader_cli.py $<TARGET_FILE:onf_reader> ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
    add_test(NAME onf_writer_cli
             COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/cli/test_writer_cli.py $<TARGET_FILE:onf_writer> $<TARGET_FILE:onf_reader> ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
endif()
