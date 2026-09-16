// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/blob_store.hpp"
#include "thumtoo/layout.hpp"
#include "thumtoo/database.hpp"
#include "thumtoo/uri.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

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
      << "Usage: " << argv0
      << " [--cache DIR] [--dry-run]\n"
      << "       [--uri URI]... [--path PATH]...\n"
      << "       [--min-scale N] [--orphans] [--dead-paths] [--soft-levels]\n"
      << "\n"
      << "Manual cache maintenance (no automatic eviction).\n"
      << "\n"
      << "  --cache DIR     Cache root (default: $XDG_CACHE_HOME/thumtoo or\n"
      << "                  ~/.cache/thumtoo)\n"
      << "  --dry-run       Report only; do not delete\n"
      << "  --uri URI       Forget this location URI (locator + orphan content/tiles)\n"
      << "  --path PATH     Forget all locators for this filesystem path (outer_path\n"
      << "                  and file:/// form). Returns that path to cold cache state.\n"
      << "  --min-scale N   Drop tile rows/blobs with scale < N (keep coarser)\n"
      << "                  Higher scale = coarser in thumtoo tile coords.\n"
      << "                  Example: --min-scale 3 removes fine scales 0,1,2\n"
      << "  --orphans       Remove content with no locators (+ their blobs)\n"
      << "  --dead-paths    Remove locators whose outer_path is missing on disk\n"
      << "                  (then purge newly orphaned content)\n"
      << "  --soft-levels   Drop soft/overview levels (max_edge ≤ batch edge)\n"
      << "                  from index + blobs; keep full_native and tiles\n"
      << "\n"
      << "At least one action flag is required.\n"
      << "LQIP on content rows is kept until the content row is purged.\n"
      << "Shared content_id is only fully purged when no locators remain.\n";
}

thumtoo::Database::PurgeStats purge_uris(thumtoo::Database& db,
                                         thumtoo::BlobStore& blobs,
                                         const std::vector<std::string>& uris,
                                         bool dry_run) {
  thumtoo::Database::PurgeStats stats;
  for (const auto& uri : uris) {
    if (uri.empty()) {
      continue;
    }
    auto loc = db.find_locator(uri);
    if (!loc) {
      std::cout << "  miss (not in cache): " << uri << "\n";
      continue;
    }
    stats.removed_uris.push_back(uri);
    std::cout << "  locator: " << uri;
    if (loc->content_id) {
      std::cout << "  content=" << *loc->content_id;
    }
    std::cout << "\n";
    const auto cid = loc->content_id;
    if (dry_run) {
      continue;
    }
    db.delete_locator(uri);
    if (!cid || cid->empty()) {
      continue;
    }
    if (!db.list_locators_for_content_id(*cid, 1).empty()) {
      std::cout << "    content kept (other locators)\n";
      continue;
    }
    stats.tiles_deleted += blobs.delete_tiles_for_content(*cid);
    stats.levels_deleted += blobs.delete_levels_for_content(*cid);
    db.purge_content_metadata(*cid);
    stats.purged_content_ids.push_back(*cid);
    std::cout << "    purged content + tiles/levels\n";
  }
  return stats;
}

std::vector<std::string> uris_for_path(thumtoo::Database& db,
                                       const std::filesystem::path& path) {
  std::vector<std::string> uris;
  auto add = [&](const std::string& u) {
    if (u.empty()) return;
    for (const auto& e : uris) {
      if (e == u) return;
    }
    uris.push_back(u);
  };
  std::error_code ec;
  std::filesystem::path abs = path;
  if (!abs.is_absolute()) {
    abs = std::filesystem::absolute(path, ec);
    if (ec) abs = path;
  }
  const std::string path_s = abs.lexically_normal().string();
  for (const auto& loc : db.list_locators_for_outer_path(path_s)) {
    add(loc.uri);
  }
  if (path_s != path.string()) {
    for (const auto& loc : db.list_locators_for_outer_path(path.string())) {
      add(loc.uri);
    }
  }
  const std::string file_uri = thumtoo::file_uri_from_path(abs);
  add(file_uri);
  if (auto loc = db.find_locator(file_uri)) {
    add(loc->uri);
  }
  return uris;
}

void print_purge_stats(const thumtoo::Database::PurgeStats& s, bool dry_run) {
  std::cout << "  uris=" << s.removed_uris.size()
            << " content_purged=" << s.purged_content_ids.size()
            << " tiles=" << s.tiles_deleted
            << " levels=" << s.levels_deleted
            << (dry_run ? " (dry-run)\n" : "\n");
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path cache = default_cache_root();
  bool dry_run = false;
  bool do_orphans = false;
  bool do_dead = false;
  bool do_soft_levels = false;
  int min_scale = -1;
  std::vector<std::string> uris;
  std::vector<std::filesystem::path> paths;

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
    if (a == "--dry-run") {
      dry_run = true;
      continue;
    }
    if (a == "--uri" && i + 1 < argc) {
      uris.emplace_back(argv[++i]);
      continue;
    }
    if (a == "--path" && i + 1 < argc) {
      paths.emplace_back(argv[++i]);
      continue;
    }
    if (a == "--min-scale" && i + 1 < argc) {
      min_scale = std::atoi(argv[++i]);
      continue;
    }
    if (a == "--orphans") {
      do_orphans = true;
      continue;
    }
    if (a == "--dead-paths") {
      do_dead = true;
      continue;
    }
    if (a == "--soft-levels") {
      do_soft_levels = true;
      continue;
    }
    std::cerr << "Unknown argument: " << a << "\n";
    usage(argv[0]);
    return 2;
  }

  if (min_scale < 0 && !do_orphans && !do_dead && !do_soft_levels && uris.empty()
      && paths.empty()) {
    usage(argv[0]);
    return 2;
  }
  if (min_scale >= 0 && min_scale > 32) {
    std::cerr << "--min-scale out of range\n";
    return 2;
  }

  try {
    auto db = thumtoo::Database::open(thumtoo::legacy_db_root(cache));
    auto blobs = thumtoo::BlobStore::open(thumtoo::legacy_db_root(cache));

    std::cout << "cache: " << cache << (dry_run ? " (dry-run)\n" : "\n");

    if (!uris.empty() || !paths.empty()) {
      std::vector<std::string> all = uris;
      for (const auto& p : paths) {
        auto more = uris_for_path(db, p);
        if (more.empty()) {
          std::cout << "path: " << p << " — no locators\n";
        }
        for (const auto& u : more) {
          bool seen = false;
          for (const auto& e : all) {
            if (e == u) {
              seen = true;
              break;
            }
          }
          if (!seen) all.push_back(u);
        }
      }
      std::cout << "purge uris (" << all.size() << "):\n";
      const auto st = purge_uris(db, blobs, all, dry_run);
      print_purge_stats(st, dry_run);
    }

    if (min_scale >= 0) {
      const auto before_t = db.count_tiles();
      const auto before_b = blobs.count_tiles();
      std::cout << "tiles below scale " << min_scale << ": index=" << before_t
                << " blobs=" << before_b << " (counts are all tiles; deleting"
                << " scale < " << min_scale << ")\n";
      if (!dry_run) {
        const auto n_idx = db.delete_tiles_below_scale(min_scale);
        const auto n_blob = blobs.delete_tiles_below_scale(min_scale);
        std::cout << "  deleted index rows: " << n_idx
                  << "  blob rows: " << n_blob << "\n";
      } else {
        std::cout << "  dry-run: would DELETE FROM tiles/tile_blobs WHERE scale < "
                  << min_scale << "\n";
      }
    }

    if (do_soft_levels) {
      const int edge_cap = thumtoo::kBatchMaxEdge;
      std::cout << "soft/overview levels (max_edge <= " << edge_cap << "):\n";
      auto contents = db.list_content(1000000);
      std::int64_t n = 0;
      for (const auto& c : contents) {
        auto levels = db.list_levels(c.content_id, 100000);
        for (const auto& lv : levels) {
          if (lv.max_edge > edge_cap) continue;
          std::cout << "  " << lv.content_id << " edge=" << lv.max_edge
                    << " frame=" << lv.frame_idx << "\n";
          ++n;
          if (!dry_run) {
            blobs.delete_level(lv.content_id, lv.max_edge, lv.frame_idx);
            db.delete_level(lv.content_id, lv.max_edge, lv.frame_idx);
          }
        }
      }
      if (dry_run) {
        std::cout << "  dry-run: would delete " << n << " level rows\n";
      } else {
        std::cout << "  deleted index+blob levels: " << n << "\n";
      }
    }

    if (do_dead) {
      auto dead = db.list_dead_path_locators();
      std::cout << "dead-path locators: " << dead.size() << "\n";
      for (const auto& loc : dead) {
        std::cout << "  " << loc.uri;
        if (loc.outer_path) std::cout << "  (" << *loc.outer_path << ")";
        std::cout << "\n";
        if (!dry_run) db.delete_locator(loc.uri);
      }
      do_orphans = true;
    }

    if (do_orphans) {
      auto orphans = db.list_orphan_content_ids();
      std::cout << "orphan content_ids: " << orphans.size() << "\n";
      std::int64_t tiles = 0, levels = 0;
      for (const auto& id : orphans) {
        std::cout << "  " << id << "\n";
        if (!dry_run) {
          tiles += blobs.delete_tiles_for_content(id);
          levels += blobs.delete_levels_for_content(id);
          db.purge_content_metadata(id);
        }
      }
      if (!dry_run && !orphans.empty()) {
        std::cout << "  purged blob tiles=" << tiles << " levels=" << levels
                  << "\n";
      }
    }

    if (!dry_run) {
      std::cout << "done. tiles left: index=" << db.count_tiles()
                << " blobs=" << blobs.count_tiles() << "\n";
    }
  } catch (const std::exception& e) {
    std::cerr << "thumtoo-gc: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
