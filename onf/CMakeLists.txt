add_library(onf STATIC
    src/errors.cpp
)
add_library(onf::onf ALIAS onf)

target_include_directories(onf PUBLIC include)
target_compile_features(onf PUBLIC cxx_std_20)
