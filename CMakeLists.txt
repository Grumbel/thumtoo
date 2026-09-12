# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)
project(thumtoo VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

option(THUMTOO_BUILD_TESTS "Build unit tests" ON)
option(THUMTOO_BUILD_TOOLS "Build CLI tools" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(VIPS REQUIRED IMPORTED_TARGET vips)
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive)
# DjVuLibre ships ddjvuapi.pc (headers + libdjvulibre). On Nix the .pc lives
# in the .dev output — ensure djvulibre is on PKG_CONFIG_PATH (buildInputs).
pkg_check_modules(DJVULIBRE IMPORTED_TARGET ddjvuapi)
if(NOT DJVULIBRE_FOUND)
  pkg_check_modules(DJVULIBRE IMPORTED_TARGET djvulibre)
endif()
pkg_check_modules(LIBCURL IMPORTED_TARGET libcurl)

add_library(thumtoo
  src/database.cpp
  src/uri.cpp
  src/client.cpp
  src/image.cpp
  src/archive.cpp
  src/pdf.cpp
  src/pdf_mupdf.cpp
  src/epub.cpp
  src/djvu.cpp
  src/expand.cpp
  src/format.cpp
  src/blob_store.cpp
  src/build_stats.cpp
  src/lqip.cpp
  src/handsum.cpp
  external/basic_handsum_decode_impl.c
  src/network.cpp
  src/text.cpp
  src/appearance.cpp
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
# MuPDF (required for PDF pages; Poppler support removed).
pkg_check_modules(MUPDF IMPORTED_TARGET mupdf)
if(NOT MUPDF_FOUND)
  find_path(MUPDF_INCLUDE_DIR mupdf/fitz.h)
  find_library(MUPDF_LIBRARY NAMES mupdf)
  if(MUPDF_INCLUDE_DIR AND MUPDF_LIBRARY)
    set(MUPDF_FOUND TRUE)
  endif()
endif()
if(MUPDF_FOUND)
  if(TARGET PkgConfig::MUPDF)
    target_link_libraries(thumtoo PRIVATE PkgConfig::MUPDF)
  else()
    target_include_directories(thumtoo PRIVATE ${MUPDF_INCLUDE_DIR})
    target_link_libraries(thumtoo PRIVATE ${MUPDF_LIBRARY})
  endif()
  target_compile_definitions(thumtoo PUBLIC THUMTOO_HAVE_MUPDF=1)
  message(STATUS "thumtoo: MuPDF enabled (PDF pages)")
else()
  message(STATUS "thumtoo: MuPDF not found — PDF page support disabled")
endif()

if(DJVULIBRE_FOUND)
  # PRIVATE: headers do not expose ddjvuapi types; consumers only need thumtoo.
  target_link_libraries(thumtoo PRIVATE PkgConfig::DJVULIBRE)
  target_compile_definitions(thumtoo PUBLIC THUMTOO_HAVE_DJVU=1)
  message(STATUS "thumtoo: DjVuLibre enabled (DjVu pages) includes=${DJVULIBRE_INCLUDE_DIRS}")
else()
  message(WARNING
    "thumtoo: DjVuLibre (ddjvuapi.pc) not found — multipage DjVu will NOT expand. "
    "Install djvulibre/libdjvulibre-dev and ensure PKG_CONFIG_PATH sees ddjvuapi.pc. "
    "Nix: put pkgs.djvulibre in buildInputs (pulls .dev for pkg-config).")
endif()
if(LIBCURL_FOUND)
  target_link_libraries(thumtoo PRIVATE PkgConfig::LIBCURL)
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
  add_executable(thumtoo-gc tools/thumtoo_gc.cpp)
  target_link_libraries(thumtoo-gc PRIVATE thumtoo)
  target_link_libraries(thumtoo-tile PRIVATE thumtoo)
  target_link_libraries(thumtoo-bench PRIVATE thumtoo)
  add_executable(thumtoo-microbench-decode tools/microbench_decode.cpp)
  target_link_libraries(thumtoo-microbench-decode PRIVATE PkgConfig::VIPS)
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
  add_executable(thumtoo-test-lqip tests/test_lqip.cpp)
  target_link_libraries(thumtoo-test-lqip PRIVATE thumtoo)
  add_test(NAME lqip COMMAND thumtoo-test-lqip)
  target_link_libraries(thumtoo-test-uri PRIVATE thumtoo)
  add_test(NAME uri COMMAND thumtoo-test-uri)
  add_executable(thumtoo-test-appearance tests/test_appearance.cpp)
  target_link_libraries(thumtoo-test-appearance PRIVATE thumtoo)
  add_test(NAME appearance COMMAND thumtoo-test-appearance)

  add_executable(thumtoo-test-pdf-scale tests/test_pdf_scale.cpp)
  target_link_libraries(thumtoo-test-pdf-scale PRIVATE thumtoo)
  add_test(NAME pdf_scale COMMAND thumtoo-test-pdf-scale)

  add_executable(thumtoo-test-pdf-tiles tests/test_pdf_tiles.cpp)
  target_link_libraries(thumtoo-test-pdf-tiles PRIVATE thumtoo)
  add_test(NAME pdf_tiles COMMAND thumtoo-test-pdf-tiles)
  add_executable(thumtoo-test-soft-ladder tests/test_soft_ladder.cpp)
  target_link_libraries(thumtoo-test-soft-ladder PRIVATE thumtoo)
  add_test(NAME soft_ladder COMMAND thumtoo-test-soft-ladder)
  add_executable(thumtoo-test-pixels-from-tiles tests/test_pixels_from_tiles.cpp)
  target_link_libraries(thumtoo-test-pixels-from-tiles PRIVATE thumtoo)
  add_test(NAME pixels_from_tiles COMMAND thumtoo-test-pixels-from-tiles)
  add_executable(thumtoo-test-pdf-text tests/test_pdf_text.cpp)
  target_link_libraries(thumtoo-test-pdf-text PRIVATE thumtoo)
  add_test(NAME pdf_text COMMAND thumtoo-test-pdf-text)
  add_executable(thumtoo-test-djvu-text tests/test_djvu_text.cpp)
  target_link_libraries(thumtoo-test-djvu-text PRIVATE thumtoo)
  add_test(NAME djvu_text COMMAND thumtoo-test-djvu-text)
  add_executable(thumtoo-test-epub-text tests/test_epub_text.cpp)
  target_link_libraries(thumtoo-test-epub-text PRIVATE thumtoo)
  add_test(NAME epub_text COMMAND thumtoo-test-epub-text)
  add_executable(thumtoo-test-text-layer-cache tests/test_text_layer_cache.cpp)
  target_link_libraries(thumtoo-test-text-layer-cache PRIVATE thumtoo)
  add_test(NAME text_layer_cache COMMAND thumtoo-test-text-layer-cache)
endif()

install(TARGETS thumtoo
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/thumtoo DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(THUMTOO_BUILD_TOOLS)
  install(TARGETS thumtoo-tile thumtoo-status thumtoo-prepare thumtoo-bench
                  thumtoo-gc thumtoo-microbench-decode
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# pkg-config for consumers (biltoo, etc.). Static lib: Requires.private lists public deps.
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/thumtoo.pc.in"
  "${CMAKE_CURRENT_BINARY_DIR}/thumtoo.pc"
  @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/thumtoo.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

# Packaging environments (Nix cmake setup, etc.) pass the full GNUInstallDirs
# set and CMAKE_EXPORT_NO_PACKAGE_REGISTRY. Reference them so configure does
# not warn about unused -D variables for dirs we do not install into yet.
set(_thumtoo_pkg_vars_used
  "${CMAKE_INSTALL_SBINDIR}"
  "${CMAKE_INSTALL_LIBEXECDIR}"
  "${CMAKE_INSTALL_LOCALEDIR}"
  "${CMAKE_INSTALL_INFODIR}"
  "${CMAKE_INSTALL_DOCDIR}"
  "${CMAKE_INSTALL_MANDIR}"
  "${CMAKE_EXPORT_NO_PACKAGE_REGISTRY}")
unset(_thumtoo_pkg_vars_used)

# ---------------------------------------------------------------------------
# Feature summary (configure-time)
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "=== thumtoo feature summary ===")
# PDF: MuPDF only (Poppler removed).
if(MUPDF_FOUND)
  message(STATUS "  PDF backend:                 MuPDF  (//page:N, //mupdf-page:N)")
else()
  message(STATUS "  PDF backend:                 OFF    (install mupdf)")
endif()

if(DJVULIBRE_FOUND)
  message(STATUS "  DjVu pages (DjVuLibre):      ON")
else()
  message(STATUS "  DjVu pages (DjVuLibre):      OFF  (install djvulibre / ddjvuapi.pc)")
endif()
if(LIBCURL_FOUND)
  message(STATUS "  HTTP(S) fetch (libcurl):     ON")
else()
  message(STATUS "  HTTP(S) fetch (libcurl):     OFF")
endif()
message(STATUS "  Archives (libarchive):       ON")
message(STATUS "  Ladder/tiles (libvips):      ON")
message(STATUS "  SQLite cache:                ON")
message(STATUS "================================")
message(STATUS "")
