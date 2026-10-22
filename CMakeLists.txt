cmake_minimum_required(VERSION 3.25)
project(onf_tools VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ONF_BUILD_TESTS "Build the onf test target" ON)
option(ONF_SANITIZE "Build everything with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion)
endif()

if(ONF_SANITIZE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "ONF_SANITIZE needs GCC or Clang")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

find_library(LIBZIP_LIBRARY NAMES zip REQUIRED)
find_path(LIBZIP_INCLUDE_DIR NAMES zip.h REQUIRED)
find_package(ZLIB REQUIRED)

add_subdirectory(onf)

if(ONF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
