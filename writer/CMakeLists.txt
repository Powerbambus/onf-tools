add_executable(onf_writer
    src/main.cpp
    src/rewrite.cpp
)
set_target_properties(onf_writer PROPERTIES OUTPUT_NAME onf-writer)

# The writer depends on the format layer only, never on libzip or the reader.
target_link_libraries(onf_writer PRIVATE onf::onf)
target_include_directories(onf_writer PRIVATE src)
