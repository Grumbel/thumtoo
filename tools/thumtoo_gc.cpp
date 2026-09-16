// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/// Cache maintenance. Legacy ladder GC when on-disk schema-4 files exist under
/// legacy_db_root; otherwise reports Store counts (redesign has no ladder GC).

#include "thumtoo/blob_store.hpp"
#include "thumtoo/database.hpp"
#include "thumtoo/layout.hpp"
#include "thumtoo/store.hpp"
#include "thumtoo/constants.hpp"
#include "thumtoo/uri.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
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

bool legacy_files_present(const fs::path& legacy_root) {
  std::error_code ec;
  return fs::is_regular_file(legacy_root / "index.sqlite", ec) ||
         fs::is_regular_file(legacy_root / "blobs.sqlite", ec);
}

void usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0
      << " [--cache DIR] [--dry-run]\n"
      << "       [--uri URI]... [--path PATH]...\n"
      << "       [--min-scale N] [--orphans] [--dead-paths] [--soft-levels]\n"
      << "       [--store-summary]\n"
      << "\n"
      << "Manual cache maintenance (no automatic eviction).\n"
      << "\n"
      << "  Legacy ladder GC runs only when index.sqlite/blobs.sqlite exist under\n"
      << "  the legacy root (top-level or $cache/legacy/ after STORE_ROOT migrate).\n"
      << "  Redesign Store tiles are not purged by ladder flags; use --store-summary.\n"
      << "\n"
      << "  --cache DIR       Cache root (default: $XDG_CACHE_HOME/thumtoo)\n"
      << "  --dry-run         Report only; do not delete\n"
      << "  --uri URI         Forget this location URI (legacy)\n"
      << "  --path PATH       Forget locators for this filesystem path (legacy)\n"
      << "  --min-scale N     Drop legacy tile rows with scale < N\n"
      << "  --orphans         Remove legacy content with no locators\n"
      << "  --dead-paths      Remove locators whose outer_path is missing\n"
      << "  --soft-levels     Drop soft/overview levels (legacy only)\n"
      << "  --store-summary   Print redesign Store blob/locator/tile counts\n";
}

thumtoo::Database::PurgeStats purge_uris(thumtoo::Database& db,
                                         thumtoo::BlobStore& blobs,
                                         const std::vector<std::string>& uris,
                                         bool dry_run) {
  thumtoo::Database::PurgeStats stats;
  for (const auto& uri : uris) {
    if (uri.empty()) continue;
    auto loc = db.find_locator(uri);
    if (!loc) continue;
    stats.removed_uris.push_back(uri);
    const std::optional<std::string> cid = loc->content_id;
    if (dry_run) continue;
    db.delete_locator(uri);
    if (!cid || cid->empty()) continue;
    const auto remaining = db.list_locators_for_content_id(*cid, 1);
    if (!remaining.empty()) continue;
    stats.tiles_deleted += blobs.delete_tiles_for_content(*cid);
    stats.levels_deleted += blobs.delete_levels_for_content(*cid);
    db.purge_content_metadata(*cid);
    stats.purged_content_ids.push_back(*cid);
  }
  return stats;
}

std::vector<std::string> uris_for_path(thumtoo::Database& db,
                                       const fs::path& path) {
  std::vector<std::string> uris;
  auto add = [&](const std::string& u) {
    if (u.empty()) return;
    for (const auto& e : uris) {
      if (e == u) return;
    }
    uris.push_back(u);
  };
  std::error_code ec;
  fs::path abs = path;
  if (!abs.is_absolute()) {
    abs = fs::absolute(path, ec);
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

void print_store_summary(const fs::path& cache) {
  thumtoo::Store::Paths sp;
  sp.cache_root = thumtoo::redesign_store_root(cache);
  sp.data_root = cache;
  std::error_code ec;
  if (!fs::is_regular_file(sp.cache_root / "index.sqlite", ec)) {
    std::cout << "store:         (no index.sqlite at " << sp.cache_root << ")\n";
    return;
  }
  auto store = thumtoo::Store::open(sp);
  std::cout << "store_root:    " << sp.cache_root << "\n"
            << "index_schema:  " << store.index_schema_version() << "\n"
            << "blobs:         " << store.count_blobs() << "\n"
            << "locators:      " << store.count_locators() << "\n"
            << "media:         " << store.count_media() << "\n"
            << "regions:       " << store.count_regions() << "\n"
            << "tiles:         " << store.count_tiles() << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  fs::path cache = default_cache_root();
  bool dry_run = false;
  bool do_orphans = false;
  bool do_dead = false;
  bool do_soft_levels = false;
  bool do_store_summary = false;
  int min_scale = -1;
  std::vector<std::string> uris;
  std::vector<fs::path> paths;

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
    if (a == "--store-summary") {
      do_store_summary = true;
      continue;
    }
    std::cerr << "Unknown argument: " << a << "\n";
    usage(argv[0]);
    return 2;
  }

  const bool want_legacy = min_scale >= 0 || do_orphans || do_dead ||
                           do_soft_levels || !uris.empty() || !paths.empty();
  if (!want_legacy && !do_store_summary) {
    usage(argv[0]);
    return 2;
  }
  if (min_scale >= 0 && min_scale > 32) {
    std::cerr << "--min-scale out of range\n";
    return 2;
  }

  try {
    std::cout << "cache: " << cache << (dry_run ? " (dry-run)\n" : "\n");
    if (do_store_summary) {
      print_store_summary(cache);
    }

    if (!want_legacy) {
      return 0;
    }

    const auto legacy_root = thumtoo::legacy_db_root(cache);
    const bool have_legacy = legacy_files_present(legacy_root);

    // Store forget for --uri / --path when redesign index exists.
    if (!uris.empty() || !paths.empty()) {
      thumtoo::Store::Paths sp;
      sp.cache_root = thumtoo::redesign_store_root(cache);
      sp.data_root = cache;
      std::error_code ec;
      if (fs::is_regular_file(sp.cache_root / "index.sqlite", ec)) {
        auto store = thumtoo::Store::open(sp);
        std::cout << "store forget:\n";
        for (const auto& u : uris) {
          auto st = store.forget_uri(u, dry_run);
          std::cout << "  uri " << u
                    << (st.locator_removed ? " removed" : " miss")
                    << (st.blob_purged ? " (blob purged)" : "")
                    << " tiles=" << st.tiles_deleted
                    << (dry_run ? " (dry-run)\n" : "\n");
        }
        for (const auto& p : paths) {
          std::error_code e2;
          auto abs = fs::absolute(p, e2);
          if (e2) abs = p;
          const auto file_uri =
              thumtoo::file_uri_from_path(abs.lexically_normal());
          auto st = store.forget_uri(file_uri, dry_run);
          std::cout << "  path " << p << " -> " << file_uri
                    << (st.locator_removed ? " removed" : " miss")
                    << (st.blob_purged ? " (blob purged)" : "")
                    << " tiles=" << st.tiles_deleted
                    << (dry_run ? " (dry-run)\n" : "\n");
        }
        if (!have_legacy && !do_orphans && !do_dead && !do_soft_levels &&
            min_scale < 0) {
          return 0;
        }
      }
    }

    if (!have_legacy) {
      if (do_orphans || do_dead || do_soft_levels || min_scale >= 0) {
        std::cerr << "thumtoo-gc: no legacy index/blobs under " << legacy_root
                  << "\n"
                  << "  (ladder flags need schema-4 files)\n";
        return 2;
      }
      return 0;
    }

    auto db = thumtoo::Database::open(legacy_root);
    auto blobs = thumtoo::BlobStore::open(legacy_root);
    std::cout << "legacy_root: " << legacy_root << "\n";

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
                << " blobs=" << before_b << "\n";
      if (!dry_run) {
        const auto n_idx = db.delete_tiles_below_scale(min_scale);
        const auto n_blob = blobs.delete_tiles_below_scale(min_scale);
        std::cout << "  deleted index rows: " << n_idx
                  << "  blob rows: " << n_blob << "\n";
      }
    }

    if (do_orphans) {
      auto orphans = db.list_orphan_content_ids(100000);
      std::cout << "orphans: " << orphans.size() << "\n";
      if (!dry_run) {
        for (const auto& cid : orphans) {
          (void)blobs.delete_tiles_for_content(cid);
          (void)blobs.delete_levels_for_content(cid);
          db.purge_content_metadata(cid);
        }
      }
    }

    if (do_dead) {
      auto dead = db.list_dead_path_locators(100000);
      std::cout << "dead-path locators: " << dead.size() << "\n";
      if (!dry_run) {
        std::vector<std::string> dead_uris;
        dead_uris.reserve(dead.size());
        for (const auto& loc : dead) dead_uris.push_back(loc.uri);
        const auto st = purge_uris(db, blobs, dead_uris, /*dry_run=*/false);
        print_purge_stats(st, false);
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
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
