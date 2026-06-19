# SPDX-License-Identifier: GPL-3.0-or-later
#
# Host build for the `phnix` protocol codec and its unit tests. Compiles and
# runs on a dev machine / CI with nothing but a C++17 compiler and CMake — no
# ESP toolchain, no BLE, no network. The codec source lives in the ESPHome
# component dir (../../components/phnix); these tests compile the very same
# files, so a green run here validates exactly what ships in the firmware.
#
#   cmake -S tests/phnix -B tests/phnix/build && cmake --build tests/phnix/build \
#     && ctest --test-dir tests/phnix/build --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(phnix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

set(PHNIX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../components/phnix)

add_library(phnix STATIC
  ${PHNIX_DIR}/crc.cpp
  ${PHNIX_DIR}/frame.cpp
  ${PHNIX_DIR}/decode.cpp
)
target_include_directories(phnix PUBLIC ${PHNIX_DIR})

enable_testing()

add_executable(phnix_tests
  test_main.cpp
  test_crc.cpp
  test_frame.cpp
  test_decode.cpp
  test_golden.cpp
)
target_include_directories(phnix_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(phnix_tests PRIVATE phnix)

add_test(NAME phnix_tests COMMAND phnix_tests)
