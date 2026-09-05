# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)
project(thumtoo VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(THUMTOO_BUILD_TESTS "Build unit tests" ON)
option(THUMTOO_BUILD_TOOLS "Build CLI tools" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(VIPS REQUIRED IMPORTED_TARGET vips)

add_library(sqlite3_amalgamation STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3_amalgamation PUBLIC third_party/sqlite)
target_compile_definitions(sqlite3_amalgamation PUBLIC
  SQLITE_THREADSAFE=1
  SQLITE_ENABLE_RTREE=0
)
if(UNIX)
  target_link_libraries(sqlite3_amalgamation PUBLIC ${CMAKE_DL_LIBS} pthread)
endif()

add_library(thumtoo
  src/database.cpp
  src/uri.cpp
  src/client.cpp
  src/image.cpp
)
add_library(thumtoo::thumtoo ALIAS thumtoo)
target_include_directories(thumtoo
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite
)
target_link_libraries(thumtoo PUBLIC sqlite3_amalgamation PkgConfig::VIPS)
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

install(TARGETS thumtoo sqlite3_amalgamation
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)
install(DIRECTORY include/thumtoo DESTINATION include)
if(THUMTOO_BUILD_TOOLS)
  install(TARGETS thumtoo-status thumtoo-prepare RUNTIME DESTINATION bin)
endif()
