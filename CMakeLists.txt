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
pkg_check_modules(POPPLERCPP IMPORTED_TARGET poppler-cpp)
pkg_check_modules(LIBCURL IMPORTED_TARGET libcurl)

add_library(thumtoo
  src/database.cpp
  src/uri.cpp
  src/client.cpp
  src/image.cpp
  src/archive.cpp
  src/pdf.cpp
  src/format.cpp
  src/blob_store.cpp
  src/build_stats.cpp
  src/lqip.cpp
  src/network.cpp
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
if(POPPLERCPP_FOUND)
  target_link_libraries(thumtoo PUBLIC PkgConfig::POPPLERCPP)
  target_compile_definitions(thumtoo PUBLIC THUMTOO_HAVE_POPPLER=1)
  message(STATUS "thumtoo: Poppler-cpp enabled (PDF pages)")
else()
  message(STATUS "thumtoo: Poppler-cpp not found — PDF page support disabled")
endif()
if(LIBCURL_FOUND)
  target_link_libraries(thumtoo PUBLIC PkgConfig::LIBCURL)
  target_compile_definitions(thumtoo PUBLIC THUMTOO_HAVE_CURL=1)
  message(STATUS "thumtoo: libcurl enabled (HTTP/S source fetch)")
else()
  message(STATUS "thumtoo: libcurl not found — HTTP/S fetch disabled")
endif()
target_compile_options(thumtoo PRIVATE -Wall -Wextra -Wpedantic)

if(THUMTOO_BUILD_TOOLS)
  add_executable(thumtoo-status tools/thumtoo_status.cpp)
  target_link_libraries(thumtoo-status PRIVATE thumtoo)
  add_executable(thumtoo-prepare tools/thumtoo_prepare.cpp)
  target_link_libraries(thumtoo-prepare PRIVATE thumtoo)
  add_executable(thumtoo-bench tools/thumtoo_bench.cpp)
  add_executable(thumtoo-tile tools/thumtoo_tile.cpp)
  target_link_libraries(thumtoo-tile PRIVATE thumtoo)
  target_link_libraries(thumtoo-bench PRIVATE thumtoo)
endif()

if(THUMTOO_BUILD_TESTS)
  enable_testing()
  add_executable(thumtoo-test-database tests/test_database.cpp)
  target_link_libraries(thumtoo-test-database PRIVATE thumtoo)
  add_test(NAME database COMMAND thumtoo-test-database)
  add_executable(thumtoo-test-client tests/test_client.cpp)
  target_link_libraries(thumtoo-test-client PRIVATE thumtoo)
  add_test(NAME client COMMAND thumtoo-test-client)
  add_executable(thumtoo-test-uri tests/test_uri.cpp)
  target_link_libraries(thumtoo-test-uri PRIVATE thumtoo)
  add_test(NAME uri COMMAND thumtoo-test-uri)

  add_executable(thumtoo-test-pdf-scale tests/test_pdf_scale.cpp)
  target_link_libraries(thumtoo-test-pdf-scale PRIVATE thumtoo)
  add_test(NAME pdf_scale COMMAND thumtoo-test-pdf-scale)

  add_executable(thumtoo-test-pdf-tiles tests/test_pdf_tiles.cpp)
  target_link_libraries(thumtoo-test-pdf-tiles PRIVATE thumtoo)
  add_test(NAME pdf_tiles COMMAND thumtoo-test-pdf-tiles)
endif()

install(TARGETS thumtoo ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
install(DIRECTORY include/thumtoo DESTINATION include)
if(THUMTOO_BUILD_TOOLS)
  install(TARGETS thumtoo-tile thumtoo-status thumtoo-prepare thumtoo-bench RUNTIME DESTINATION bin)
endif()
