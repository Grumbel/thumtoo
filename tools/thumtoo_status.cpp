// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/constants.hpp"
#include "thumtoo/database.hpp"
#include "thumtoo/status.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::filesystem::path default_cache_root() {
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  if (xdg && *xdg) {
    return std::filesystem::path(xdg) / "thumtoo";
  }
  const char* home = std::getenv("HOME");
  if (home && *home) {
    return std::filesystem::path(home) / ".cache" / "thumtoo";
  }
  return std::filesystem::path(".cache") / "thumtoo";
}

void usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0 << " [--cache DIR] [summary|locators|content|levels|archives]\n"
      << "  Inspect a thumtoo cache (index.sqlite under DIR).\n"
      << "  Default DIR: $XDG_CACHE_HOME/thumtoo or ~/.cache/thumtoo\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path cache = default_cache_root();
  std::string mode = "summary";

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
    if (a == "summary" || a == "locators" || a == "content" || a == "levels" || a == "archives") {
      mode = a;
      continue;
    }
    std::cerr << "Unknown argument: " << a << "\n";
    usage(argv[0]);
    return 2;
  }

  try {
    auto db = thumtoo::Database::open(cache);
    if (mode == "summary") {
      std::cout << "cache_root:     " << db.cache_root() << "\n"
                << "db_path:        " << db.db_path() << "\n"
                << "schema_version: " << db.schema_version()
                << " (build " << thumtoo::kSchemaVersion << ")\n";
      if (auto v = db.meta_get(thumtoo::kSchemaMetaLadderEdgesKey))
        std::cout << "ladder_edges:   " << *v << "\n";
      if (auto v = db.meta_get(thumtoo::kSchemaMetaJxlQualityKey))
        std::cout << "jxl_quality:    " << *v << "\n";
      else if (auto v = db.meta_get(thumtoo::kSchemaMetaWebpQualityKey))
        std::cout << "webp_quality:   " << *v << " (legacy)\n";
      std::cout << "content rows:   " << db.count_content() << "\n"
                << "locators:       " << db.count_locators() << "\n"
                << "levels:         " << db.count_levels() << "\n"
                << "dir snapshots:  " << db.count_directory_snapshots() << "\n";
      return 0;
    }
    if (mode == "locators") {
      for (const auto& r : db.list_locators(500)) {
        std::cout << r.uri;
        if (r.content_id) std::cout << "  content=" << *r.content_id;
        if (r.size) std::cout << "  size=" << *r.size;
        std::cout << "\n";
      }
      return 0;
    }
    if (mode == "content") {
      for (const auto& r : db.list_content(500)) {
        std::cout << r.content_id << "  status=" << thumtoo::to_string(r.status);
        if (r.width && r.height)
          std::cout << "  " << *r.width << "x" << *r.height;
        if (r.still_count) std::cout << "  stills=" << *r.still_count;
        if (r.duration_ms) std::cout << "  duration_ms=" << *r.duration_ms;
        if (r.error_code) std::cout << "  error=" << *r.error_code;
        std::cout << "\n";
      }
      return 0;
    }
    if (mode == "levels") {
      for (const auto& c : db.list_content(500)) {
        for (const auto& lv : db.list_levels(c.content_id)) {
          std::cout << c.content_id << "  edge=" << lv.max_edge
                    << "  frame=" << lv.frame_idx;
          if (lv.width && lv.height)
            std::cout << "  " << *lv.width << "x" << *lv.height;
          if (lv.codec) std::cout << "  " << *lv.codec;
          if (lv.path) std::cout << "  " << *lv.path;
          std::cout << "\n";
        }
      }
      return 0;
    }
    if (mode == "archives") {
      // List members for every archive_uri that has locator rows or entries.
      // Simple: scan locators with //archive and unique outer, then entries.
      std::vector<std::string> seen;
      for (const auto& loc : db.list_locators(5000)) {
        if (loc.uri.find("//archive") == std::string::npos) continue;
        auto pipe = loc.uri.find("//archive");
        std::string root = loc.uri.substr(0, pipe + std::string("//archive").size());
        if (std::find(seen.begin(), seen.end(), root) != seen.end()) continue;
        seen.push_back(root);
        auto entries = db.list_archive_entries(root);
        std::cout << root << "  members=" << entries.size() << "\n";
        for (const auto& e : entries) {
          std::cout << "  " << e.member_path;
          if (e.uncompressed_size) std::cout << "  size=" << *e.uncompressed_size;
          std::cout << "\n";
        }
      }
      return 0;
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
