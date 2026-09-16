// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/// Inspect a thumtoo redesign Store cache.

#include "thumtoo/layout.hpp"
#include "thumtoo/store.hpp"
#include "thumtoo/uri.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

fs::path default_cache_root() {
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  if (xdg && *xdg) {
    return fs::path(xdg) / "thumtoo";
  }
  const char* home = std::getenv("HOME");
  if (home && *home) {
    return fs::path(home) / ".cache" / "thumtoo";
  }
  return fs::path(".cache") / "thumtoo";
}

void usage(const char* argv0) {
  std::cerr
      << "Usage:\n"
      << "  " << argv0 << " [--cache DIR] [summary|store]\n"
      << "  " << argv0 << " [--cache DIR] path PATH|URI\n"
      << "\n"
      << "  Inspect a thumtoo Store cache (schema ≥ 100).\n"
      << "  Default DIR: $XDG_CACHE_HOME/thumtoo or ~/.cache/thumtoo\n";
}

void print_store_summary(const fs::path& cache) {
  const auto store_root = thumtoo::redesign_store_root(cache);
  const auto legacy_root = thumtoo::legacy_db_root(cache);
  std::error_code ec;
  const bool legacy_left =
      fs::is_regular_file(legacy_root / "index.sqlite", ec) ||
      fs::is_regular_file(legacy_root / "blobs.sqlite", ec);
  std::cout << "cache:         " << cache << "\n"
            << "layout:        store-root\n"
            << "store_root:    " << store_root << "\n"
            << "legacy_root:   " << legacy_root
            << (legacy_left ? " (files present, ignored by Client)\n"
                            : " (absent)\n");

  if (!fs::is_regular_file(store_root / "index.sqlite", ec)) {
    std::cout << "store:         (no index.sqlite)\n";
    return;
  }
  thumtoo::Store::Paths sp;
  sp.cache_root = store_root;
  sp.data_root = cache;
  auto store = thumtoo::Store::open(sp);
  std::cout << "index_schema:  " << store.index_schema_version() << "\n"
            << "blobs:         " << store.count_blobs() << "\n"
            << "locators:      " << store.count_locators() << "\n"
            << "media:         " << store.count_media() << "\n"
            << "regions:       " << store.count_regions() << "\n"
            << "tiles:         " << store.count_tiles() << "\n";
}

int cmd_path_store(const fs::path& cache, std::string_view query) {
  thumtoo::Store::Paths sp;
  sp.cache_root = thumtoo::redesign_store_root(cache);
  sp.data_root = cache;
  std::error_code ec;
  if (!fs::is_regular_file(sp.cache_root / "index.sqlite", ec)) {
    std::cout << "result:        no Store index at " << sp.cache_root << "\n";
    return 0;
  }
  auto store = thumtoo::Store::open(sp);
  std::cout << "query:         " << query << "\n"
            << "store_root:    " << sp.cache_root << "\n";

  std::string uri(query);
  if (query.find("://") == std::string_view::npos && !query.empty() &&
      query[0] == '/') {
    uri = thumtoo::file_uri_from_path(fs::path(std::string(query)));
  }

  auto loc = store.find_locator(uri);
  if (!loc) {
    std::cout << "result:        not cached (no locator)\n";
    return 0;
  }
  std::cout << "locator:\n"
            << "  uri:         " << loc->uri << "\n";
  if (loc->blob_id) std::cout << "  blob_id:     " << *loc->blob_id << "\n";
  if (loc->size) std::cout << "  size:        " << *loc->size << "\n";
  if (loc->mtime_ns) std::cout << "  mtime_ns:    " << *loc->mtime_ns << "\n";

  if (loc->blob_id) {
    if (auto media =
            store.find_media_for_blob(*loc->blob_id, thumtoo::MediaKind::Image)) {
      std::cout << "media:\n"
                << "  id:          " << media->id << "\n"
                << "  kind:        image\n";
      if (media->width && media->height)
        std::cout << "  size:        " << *media->width << "x" << *media->height
                  << "\n";
      if (auto full = store.find_full_region(media->id)) {
        auto scales = store.list_tile_scales(media->id, full->id);
        std::cout << "  tile_scales: ";
        if (scales.empty())
          std::cout << "(none)\n";
        else {
          for (std::size_t i = 0; i < scales.size(); ++i) {
            if (i) std::cout << ",";
            std::cout << scales[i];
          }
          std::cout << "\n";
        }
      }
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  fs::path cache = default_cache_root();
  std::string mode = "summary";
  std::optional<std::string> path_query;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--help" || a == "-h") {
      usage(argv[0]);
      return 0;
    }
    if (a == "--cache" && i + 1 < argc) {
      cache = argv[++i];
      continue;
    }
    if (a == "path" || a == "query" || a == "show") {
      mode = "path";
      if (i + 1 >= argc) {
        std::cerr << "error: path requires PATH or URI\n";
        usage(argv[0]);
        return 2;
      }
      path_query = argv[++i];
      continue;
    }
    if (a == "summary" || a == "store") {
      mode = a;
      continue;
    }
    if (a == "legacy" || a == "locators" || a == "content" || a == "levels" ||
        a == "tiles" || a == "archives") {
      std::cerr << "thumtoo-status: mode '" << a
                << "' removed (schema-4 ladder is gone)\n"
                << "  Use summary or path.\n";
      return 2;
    }
    std::cerr << "Unknown argument: " << a << "\n";
    usage(argv[0]);
    return 2;
  }

  try {
    if (mode == "path") {
      return cmd_path_store(cache, *path_query);
    }
    print_store_summary(cache);
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
