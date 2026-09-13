// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/constants.hpp"
#include "thumtoo/database.hpp"
#include "thumtoo/status.hpp"
#include "thumtoo/uri.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

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
      << "  " << argv0 << " [--cache DIR] [summary|locators|content|levels|tiles|archives]\n"
      << "  " << argv0 << " [--cache DIR] path PATH|URI\n"
      << "\n"
      << "  Inspect a thumtoo cache (index.sqlite under DIR).\n"
      << "  Default DIR: $XDG_CACHE_HOME/thumtoo or ~/.cache/thumtoo\n"
      << "\n"
      << "  path  Show locator(s), content, ladder levels, and tile scales for one\n"
      << "        filesystem path or Location URI (file:///…, //archive:, …).\n";
}

void print_locator(const thumtoo::Database::LocatorRow& r, int index, int total) {
  if (total > 1) {
    std::cout << "locator[" << index << "/" << total << "]:\n";
  } else {
    std::cout << "locator:\n";
  }
  std::cout << "  uri:         " << r.uri << "\n";
  if (r.content_id) std::cout << "  content_id:  " << *r.content_id << "\n";
  else std::cout << "  content_id:  (none)\n";
  if (r.outer_path) std::cout << "  outer_path:  " << *r.outer_path << "\n";
  if (r.member_path) std::cout << "  member_path: " << *r.member_path << "\n";
  if (r.size) std::cout << "  size:        " << *r.size << "\n";
  if (r.mtime_ns) std::cout << "  mtime_ns:    " << *r.mtime_ns << "\n";
}

void print_content(const thumtoo::Database::ContentRow& c) {
  std::cout << "content:\n";
  std::cout << "  content_id:  " << c.content_id << "\n";
  std::cout << "  status:      " << thumtoo::to_string(c.status) << "\n";
  if (c.width && c.height)
    std::cout << "  size:        " << *c.width << "x" << *c.height << "\n";
  if (c.format) std::cout << "  format:      " << *c.format << "\n";
  if (c.still_count) std::cout << "  stills:      " << *c.still_count << "\n";
  if (c.duration_ms) std::cout << "  duration_ms: " << *c.duration_ms << "\n";
  if (c.error_code) std::cout << "  error:       " << *c.error_code << "\n";
  if (!c.lqip.empty())
    std::cout << "  lqip:        " << c.lqip.size() << " bytes (kind=" << c.lqip_kind
              << ")\n";
}

void print_levels(thumtoo::Database& db, std::string_view content_id) {
  auto levels = db.list_levels(content_id, 256);
  std::cout << "levels:        " << levels.size() << "\n";
  for (const auto& lv : levels) {
    std::cout << "  edge=" << lv.max_edge << "  frame=" << lv.frame_idx;
    if (lv.width && lv.height)
      std::cout << "  " << *lv.width << "x" << *lv.height;
    if (lv.codec) std::cout << "  " << *lv.codec;
    if (lv.quality) std::cout << "  q=" << *lv.quality;
    if (lv.path) std::cout << "  " << *lv.path;
    std::cout << "  source=" << lv.source << "\n";
  }
}

void print_tiles(thumtoo::Database& db, std::string_view content_id) {
  int min_s = 0, max_s = 0;
  if (!db.tile_min_max_scale(content_id, min_s, max_s)) {
    std::cout << "tiles:         none\n";
    return;
  }
  auto tiles = db.list_tiles(content_id, 100000);
  std::cout << "tiles:         " << tiles.size() << "  scales=[" << min_s << ".." << max_s
            << "]\n";
  // Summarize by scale rather than dumping every tile.
  std::vector<int> scales;
  scales.reserve(tiles.size());
  for (const auto& t : tiles) scales.push_back(t.scale);
  std::sort(scales.begin(), scales.end());
  scales.erase(std::unique(scales.begin(), scales.end()), scales.end());
  for (int s : scales) {
    int n = 0;
    for (const auto& t : tiles)
      if (t.scale == s) ++n;
    std::cout << "  scale=" << s << "  count=" << n << "\n";
  }
}

/// Collect locators matching a filesystem path or Location URI.
std::vector<thumtoo::Database::LocatorRow> resolve_locators(
    thumtoo::Database& db, std::string_view query) {
  std::vector<thumtoo::Database::LocatorRow> out;
  std::unordered_set<std::string> seen;

  auto add = [&](const thumtoo::Database::LocatorRow& r) {
    if (seen.insert(r.uri).second) out.push_back(r);
  };

  // 1) Exact URI match.
  if (auto loc = db.find_locator(query)) add(*loc);

  // 2) Filesystem path → file URI + outer_path.
  std::error_code ec;
  fs::path as_path(query);
  if (!query.empty() && query.find("://") == std::string_view::npos) {
    fs::path abs = as_path;
    if (!abs.is_absolute()) {
      abs = fs::absolute(abs, ec);
    }
    if (!ec) {
      abs = abs.lexically_normal();
      const std::string uri = thumtoo::file_uri_from_path(abs);
      if (auto loc = db.find_locator(uri)) add(*loc);
      for (const auto& r : db.list_locators_for_outer_path(abs.string())) add(r);
      // Prefix match for archive members under this outer path.
      for (const auto& r : db.list_locators_by_outer_path_prefix(abs.string(), 500))
        add(r);
    }
  }

  // 3) URI prefix (e.g. file:///foo/bar → members under //archive:).
  if (query.find("://") != std::string_view::npos
      || (query.size() >= 7 && query.substr(0, 7) == "file://")) {
    for (const auto& r : db.list_locators_by_uri_prefix(query, 500)) add(r);
  }

  return out;
}

int cmd_path(thumtoo::Database& db, std::string_view query) {
  std::cout << "query:         " << query << "\n";
  std::cout << "cache_root:    " << db.cache_root() << "\n";

  auto locs = resolve_locators(db, query);
  if (locs.empty()) {
    std::cout << "result:        not cached (no locator)\n";
    return 0;
  }

  std::cout << "locators:      " << locs.size() << "\n";
  std::unordered_set<std::string> content_ids;
  for (std::size_t i = 0; i < locs.size(); ++i) {
    print_locator(locs[i], static_cast<int>(i + 1), static_cast<int>(locs.size()));
    if (locs[i].content_id) content_ids.insert(*locs[i].content_id);
  }

  if (content_ids.empty()) {
    std::cout << "content:       (no content_id on locator)\n";
    return 0;
  }

  for (const auto& cid : content_ids) {
    if (auto c = db.find_content(cid)) {
      print_content(*c);
    } else {
      std::cout << "content:\n  content_id:  " << cid
                << "\n  status:      (row missing)\n";
    }
    print_levels(db, cid);
    print_tiles(db, cid);
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
    if (a == "summary" || a == "locators" || a == "content" || a == "levels"
        || a == "tiles" || a == "archives") {
      mode = a;
      continue;
    }
    std::cerr << "Unknown argument: " << a << "\n";
    usage(argv[0]);
    return 2;
  }

  try {
    auto db = thumtoo::Database::open(cache);
    if (mode == "path") {
      return cmd_path(db, *path_query);
    }
    if (mode == "summary") {
      std::cout << "cache_root:     " << db.cache_root() << "\n"
                << "db_path:        " << db.db_path() << "\n"
                << "blobs_path:     " << (db.cache_root() / "blobs.sqlite") << "\n"
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
                << "tiles:          " << db.count_tiles() << "\n"
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
    if (mode == "tiles") {
      for (const auto& c : db.list_content(500)) {
        int min_s = 0, max_s = 0;
        if (!db.tile_min_max_scale(c.content_id, min_s, max_s)) continue;
        auto tiles = db.list_tiles(c.content_id, 20);
        std::cout << c.content_id << "  scales=[" << min_s << ".." << max_s
                  << "]  sample=" << tiles.size() << "\n";
        for (const auto& t : tiles) {
          std::cout << "  s=" << t.scale << " x=" << t.x << " y=" << t.y;
          if (t.width && t.height)
            std::cout << "  " << *t.width << "x" << *t.height;
          std::cout << "\n";
        }
      }
      return 0;
    }
    if (mode == "archives") {
      // List members for every archive_uri that has locator rows or entries.
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
