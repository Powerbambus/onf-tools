add_library(onf STATIC
    src/container/bytesource.cpp
    src/container/central_directory.cpp
    src/container/paths.cpp
    src/container/zip_archive.cpp
    src/errors.cpp
    src/primitives/bytecursor.cpp
    src/primitives/utf8.cpp
    src/primitives/varint.cpp
    src/read/json.cpp
    src/read/stroke_parser.cpp
    src/read/strokes_file.cpp
    src/read/values.cpp
)
add_library(onf::onf ALIAS onf)

target_include_directories(onf PUBLIC include)
target_compile_features(onf PUBLIC cxx_std_20)

target_include_directories(onf PRIVATE ${LIBZIP_INCLUDE_DIR})
target_link_libraries(onf PRIVATE ${LIBZIP_LIBRARY})
