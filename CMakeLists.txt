# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)
project(thumtoo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(THUMTOO_BUILD_TESTS "Build unit tests" ON)
option(THUMTOO_BUILD_TOOLS "Build CLI tools" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(VIPS REQUIRED IMPORTED_TARGET vips)
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive)

add_library(thumtoo
  src/database.cpp
  src/uri.cpp
  src/client.cpp
  src/image.cpp
  src/archive.cpp
  src/blob_store.cpp
)
add_library(thumtoo::thumtoo ALIAS thumtoo)
target_include_directories(thumtoo
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(thumtoo PUBLIC
  PkgConfig::SQLITE3
  PkgConfig::VIPS
  PkgConfig::LIBARCHIVE
)
target_compile_options(thumtoo PRIVATE -Wall -Wextra -Wpedantic)

if(THUMTOO_BUILD_TOOLS)
  add_executable(thumtoo-status tools/thumtoo_status.cpp)
  target_link_libraries(thumtoo-status PRIVATE thumtoo)
  add_executable(thumtoo-prepare tools/thumtoo_prepare.cpp)
  target_link_libraries(thumtoo-prepare PRIVATE thumtoo)
endif()

if(THUMTOO_BUILD_TESTS)
  enable_testing()
  add_executable(thumtoo-test-database tests/test_database.cpp)
  target_link_libraries(thumtoo-test-database PRIVATE thumtoo)
  add_test(NAME database COMMAND thumtoo-test-database)
  add_executable(thumtoo-test-client tests/test_client.cpp)
  target_link_libraries(thumtoo-test-client PRIVATE thumtoo)
  add_test(NAME client COMMAND thumtoo-test-client)
endif()

install(TARGETS thumtoo ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
install(DIRECTORY include/thumtoo DESTINATION include)
if(THUMTOO_BUILD_TOOLS)
  install(TARGETS thumtoo-status thumtoo-prepare RUNTIME DESTINATION bin)
endif()
