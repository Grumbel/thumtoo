// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/blob_store.hpp"
#include "thumtoo/database.hpp"

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
      << "Usage: " << argv0
      << " [--cache DIR] [--dry-run] [--min-scale N] [--orphans] [--dead-paths]\n"
      << "\n"
      << "Manual cache maintenance (no automatic eviction).\n"
      << "\n"
      << "  --cache DIR     Cache root (default: $XDG_CACHE_HOME/thumtoo or\n"
      << "                  ~/.cache/thumtoo)\n"
      << "  --dry-run       Report only; do not delete\n"
      << "  --min-scale N   Drop tile rows/blobs with scale < N (keep coarser)\n"
      << "                  Higher scale = coarser in thumtoo tile coords.\n"
      << "                  Example: --min-scale 3 removes fine scales 0,1,2\n"
      << "  --orphans       Remove content with no locators (+ their blobs)\n"
      << "  --dead-paths    Remove locators whose outer_path is missing on disk\n"
      << "                  (then purge newly orphaned content)\n"
      << "\n"
      << "At least one of --min-scale / --orphans / --dead-paths is required.\n"
      << "LQIP on content rows is kept until the content row is purged.\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path cache = default_cache_root();
  bool dry_run = false;
  bool do_orphans = false;
  bool do_dead = false;
  int min_scale = -1;  // <0 means unused

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
    std::cerr << "Unknown argument: " << a << "\n";
    usage(argv[0]);
    return 2;
  }

  if (min_scale < 0 && !do_orphans && !do_dead) {
    usage(argv[0]);
    return 2;
  }
  if (min_scale >= 0 && min_scale > 32) {
    std::cerr << "--min-scale out of range\n";
    return 2;
  }

  try {
    auto db = thumtoo::Database::open(cache);
    auto blobs = thumtoo::BlobStore::open(cache);

    std::cout << "cache: " << cache << (dry_run ? " (dry-run)\n" : "\n");

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
        // Approximate: list not available; report intent only
        std::cout << "  dry-run: would DELETE FROM tiles/tile_blobs WHERE scale < "
                  << min_scale << "\n";
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
      // Fall through: orphans pass cleans content left without locators
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
