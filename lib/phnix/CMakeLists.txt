# SPDX-License-Identifier: GPL-3.0-or-later
#
# Host build for the platform-agnostic `phnix` protocol library and its unit
# tests. This compiles and runs on a development machine / CI with nothing but
# a C++17 compiler and CMake — no ESP toolchain, no BLE, no network deps.
#
#   cmake -S . -B build && cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.16)
project(phnix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

add_library(phnix STATIC
  src/crc.cpp
  src/frame.cpp
  src/decode.cpp
)
target_include_directories(phnix PUBLIC include)

enable_testing()

add_executable(phnix_tests
  tests/test_main.cpp
  tests/test_crc.cpp
  tests/test_frame.cpp
  tests/test_decode.cpp
)
target_include_directories(phnix_tests PRIVATE tests)
target_link_libraries(phnix_tests PRIVATE phnix)

add_test(NAME phnix_tests COMMAND phnix_tests)
