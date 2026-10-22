add_library(onf STATIC
    src/container/paths.cpp
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
