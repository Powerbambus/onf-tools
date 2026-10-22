add_library(onf STATIC
    src/errors.cpp
    src/primitives/bytecursor.cpp
    src/primitives/varint.cpp
    src/read/strokes_file.cpp
)
add_library(onf::onf ALIAS onf)

target_include_directories(onf PUBLIC include)
target_compile_features(onf PUBLIC cxx_std_20)
