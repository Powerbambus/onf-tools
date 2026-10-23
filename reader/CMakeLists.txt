add_executable(onf_reader
    src/base64.cpp
    src/info.cpp
    src/main.cpp
    src/svg_export.cpp
)
set_target_properties(onf_reader PROPERTIES OUTPUT_NAME onf-reader)

# The reader depends on the format layer only, never on libzip or the writer.
target_link_libraries(onf_reader PRIVATE onf::onf)
target_include_directories(onf_reader PRIVATE src)
