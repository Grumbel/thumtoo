// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/build_stats.hpp"
#include "thumtoo/version.hpp"
#include "thumtoo/client.hpp"
#include "thumtoo/image.hpp"
#include "thumtoo/status.hpp"
#include "thumtoo/archive.hpp"
#include "thumtoo/uri.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

namespace {

/// Order URIs so Sequential archives are processed in TOC member order (one
/// extract stream warms neighbors). Random archives and plain files keep
/// relative order.
void order_uris_for_sequential_extract(std::vector<std::string>& uris) {
  if (uris.size() < 2) return;

  struct ArchGroup {
    std::filesystem::path archive;
    std::vector<std::pair<std::size_t, std::string>> members;  // toc_index, uri
  };
  std::vector<ArchGroup> seq_groups;
  std::vector<std::string> other;
  other.reserve(uris.size());

  for (const auto& uri : uris) {
    auto parsed = thumtoo::parse_archive_uri(uri);
    if (!parsed || parsed->member_path.empty()) {
      other.push_back(uri);
      continue;
    }
    if (thumtoo::archive_access_class(parsed->archive_path) !=
        thumtoo::ArchiveAccess::Sequential) {
      other.push_back(uri);
      continue;
    }
    ArchGroup* g = nullptr;
    for (auto& existing : seq_groups) {
      if (existing.archive == parsed->archive_path) {
        g = &existing;
        break;
      }
    }
    if (!g) {
      seq_groups.push_back({});
      g = &seq_groups.back();
      g->archive = parsed->archive_path;
    }
    g->members.emplace_back(0, uri);
  }

  if (seq_groups.empty()) return;

  for (auto& g : seq_groups) {
    std::vector<std::string> toc_order;
    if (auto toc = thumtoo::read_archive_toc(g.archive)) {
      for (const auto& m : *toc) {
        if (thumtoo::is_likely_image_member_path(m.member_path)) {
          toc_order.push_back(m.member_path);
        }
      }
    }
    for (auto& pair : g.members) {
      auto parsed = thumtoo::parse_archive_uri(pair.second);
      if (!parsed) continue;
      if (auto i =
              thumtoo::archive_member_toc_index(toc_order, parsed->member_path)) {
        pair.first = *i;
      } else {
        pair.first = toc_order.size() + 1;
      }
    }
    std::stable_sort(g.members.begin(), g.members.end(),
                     [](const auto& a, const auto& b) {
                       return a.first < b.first;
                     });
  }

  std::vector<std::string> out;
  out.reserve(uris.size());
  for (auto& g : seq_groups) {
    for (auto& pair : g.members) {
      out.push_back(std::move(pair.second));
    }
  }
  out.insert(out.end(), other.begin(), other.end());
  uris = std::move(out);
}


std::filesystem::path default_cache_root() {
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
    return std::filesystem::path(xdg) / "thumtoo";
  }
  if (const char* home = std::getenv("HOME"); home && *home) {
    return std::filesystem::path(home) / ".cache" / "thumtoo";
  }
  return std::filesystem::path(".cache") / "thumtoo";
}

void usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0 << " [OPTION]... PATH [PATH...]\n"
      << "\n"
      << "Register PATH(s) in the thumtoo cache and run size probes.\n"
      << "Archives (zip/cbz/rar/…) expand to image member URIs; PDFs expand to\n"
      << "page URIs (//page:N, 1-based, capped at 512 pages).\n"
      << "\n"
      << "Options:\n"
      << "  -h, --help         show this help and exit\n"
      << "  -q, --quiet        suppress per-job progress on stderr\n"
      << "      --cache DIR    cache root (default: $XDG_CACHE_HOME/thumtoo\n"
      << "                      or ~/.cache/thumtoo)\n"
      << "      --ladder EDGE  after probes, encode ephemeral soft ≤ EDGE\n"
      << "                      (NOT stored in cache; prefer --tiles)\n"
      << "      --lqip         after probes, ensure durable LQIP for each URI\n"
      << "      --tiles        after probes, build Galapix-style 256×256 JPEG\n"
      << "                      tile pyramid for each ready URI\n"
      << "      --min-scale N  finest tile scale to generate (default: 0 = full res)\n"
      << "                      higher N skips finer detail (e.g. 1 = no full-res)\n"
      << "      --max-scale M  coarsest tile scale (default: -1 = until single tile)\n"
      << "      --stats        print detailed timings on stderr (pretty multi-line)\n"
      << "                      (also implied when --tiles / --ladder / --lqip is used)\n"
      << "      --stats-line   one-line timings (script-friendly)\n"
      << "      --jobs N       worker threads for the job queue (default: CPUs,\n"
      << "                      max 32; 1 restores the old single-worker behaviour)\n"
      << "\n"
      << "Output:\n"
      << "  Progress and timings go to stderr; a one-line cache summary to stdout.\n"
      << "  Size probes store native width×height (content stays Incomplete until\n"
      << "  a ladder exists unless only probes are requested).\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path cache = default_cache_root();
  std::vector<std::filesystem::path> paths;
  bool quiet = false;
  bool show_stats = false;
  bool stats_line = false;
  int ladder_edge = 0;
  bool do_lqip = false;
  bool do_tiles = false;
  int tile_min_scale = 0;
  int tile_max_scale = -1;  // <0 → until single-tile coverage
  unsigned jobs = 0;  // 0 → hardware_concurrency

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--version" || a == "-V") {
      thumtoo::print_version(std::cout);
      return 0;
    }
    if (a == "-h" || a == "--help") {
      usage(argv[0]);
      return 0;
    }
    if (a == "--quiet" || a == "-q") {
      quiet = true;
      continue;
    }
    if (a == "--cache" && i + 1 < argc) {
      cache = argv[++i];
      continue;
    }
    if (a == "--ladder" && i + 1 < argc) {
      ladder_edge = std::atoi(argv[++i]);
      if (ladder_edge < 0) ladder_edge = 0;
      continue;
    }
    if (a == "--lqip") {
      do_lqip = true;
      continue;
    }
    if (a == "--tiles") {
      do_tiles = true;
      continue;
    }
    if (a == "--min-scale" && i + 1 < argc) {
      tile_min_scale = std::atoi(argv[++i]);
      if (tile_min_scale < 0) tile_min_scale = 0;
      do_tiles = true;
      continue;
    }
    if (a == "--max-scale" && i + 1 < argc) {
      tile_max_scale = std::atoi(argv[++i]);
      do_tiles = true;
      continue;
    }
    if (a == "--stats") {
      show_stats = true;
      continue;
    }
    if (a == "--stats-line") {
      show_stats = true;
      stats_line = true;
      continue;
    }
    if (a == "--jobs" && i + 1 < argc) {
      int v = std::atoi(argv[++i]);
      if (v < 0) v = 0;
      jobs = static_cast<unsigned>(v);
      continue;
    }
    paths.emplace_back(a);
  }

  if (paths.empty()) {
    usage(argv[0]);
    return 2;
  }

  try {
    thumtoo::image_library_init();
    thumtoo::global_build_stats().reset();
    auto client = thumtoo::Client::open(cache, {}, jobs);

    std::mutex progress_mu;
    std::atomic<int> completed{0};
    std::atomic<size_t> total_atom{0};
    std::vector<std::string> sized_uris;
    sized_uris.reserve(256);

    if (!quiet) {
      std::cerr
          << "=== phase 1: size probe ===\n"
          << "Measure native width×height and register locators in the Store.\n"
          << "  incomplete = size stored (normal after a successful probe)\n"
          << "  ready      = soft/full content already considered ready\n"
          << "  failed     = could not probe this URI\n"
          << "Cache hits skip decoding the file; cold probes read the source.\n"
          << "cache=" << cache << "  paths=" << paths.size()
          << "  jobs=" << (jobs ? jobs : 0) << "\n";
    }

    const size_t total = client->prepare_paths(
        paths,
        [&](std::string uri, thumtoo::SizeReply reply) {
          auto size = reply.size;
          if (size && (ladder_edge > 0 || do_tiles || do_lqip)) {
            std::lock_guard lock(progress_mu);
            sized_uris.push_back(uri);
          }
          if (quiet) return;
          const int n = ++completed;
          const size_t tot = total_atom.load(std::memory_order_acquire);
          std::string status = "failed";
          std::string detail;
          if (auto meta = client->get_meta(uri)) {
            status = std::string(thumtoo::to_string(meta->status));
            if (meta->error_code && !meta->error_code->empty()) {
              detail = "  error=" + *meta->error_code;
            }
          } else if (size) {
            status = "ready";
          }
          // LQIP on the size reply means durable placeholder already existed.
          const bool had_lqip = reply.lqip && !reply.lqip->empty();
          std::lock_guard lock(progress_mu);
          std::cerr << "[probe " << n;
          if (tot > 0) std::cerr << "/" << tot;
          std::cerr << "] " << status << "  " << uri;
          if (size) {
            std::cerr << "  " << size->width << "x" << size->height;
          }
          if (had_lqip) {
            std::cerr << "  lqip=yes";
          }
          std::cerr << detail << "\n";
        });
    total_atom.store(total, std::memory_order_release);

    if (!quiet && total == 0) {
      std::cerr << "phase 1: nothing to probe (all paths already ready or invalid)\n";
    }

    client->drain();
    if (!quiet) {
      std::cerr << "phase 1 done: " << completed.load() << " probe callback(s), "
                << sized_uris.size() << " URI(s) eligible for encode\n";
    }

    // Sequential archives: TOC order so one extract pass warms neighbors.
    order_uris_for_sequential_extract(sized_uris);

    if (do_lqip && !sized_uris.empty()) {
      if (!quiet) {
        std::cerr << "=== phase 2: LQIP ===\n"
                  << "Ensure durable ThumbHash/Handsum for "
                  << sized_uris.size() << " URI(s).\n";
      }
      for (const auto& uri : sized_uris) {
        client->request_lqip(uri);
      }
      client->drain();
      if (!quiet) {
        int have = 0;
        for (const auto& uri : sized_uris) {
          if (client->get_lqip(uri)) ++have;
        }
        std::cerr << "LQIP present: " << have << "/" << sized_uris.size()
                  << "\n";
      }
    }

    if (ladder_edge > 0 && !sized_uris.empty()) {
      if (!quiet) {
        std::cerr << "=== phase: ephemeral soft (max edge=" << ladder_edge
                  << ") ===\n"
                  << "WARNING: soft is not Store-durable; prefer --tiles.\n"
                  << "Encode one-shot soft for " << sized_uris.size()
                  << " URI(s). ready=encoded, miss=failed.\n";
      }
      std::atomic<int> px_done{0};
      const int px_total = static_cast<int>(sized_uris.size());
      for (const auto& uri : sized_uris) {
        client->request_pixels(
            uri, ladder_edge,
            [&](std::string u, int /*edge*/,
                std::optional<thumtoo::PixelLevel> px) {
              if (quiet) return;
              const int n = ++px_done;
              std::lock_guard lock(progress_mu);
              std::cerr << "[ladder " << n << "/" << px_total << "] "
                        << (px ? "ready" : "miss") << "  " << u;
              if (px) {
                std::cerr << "  " << px->width << "x" << px->height;
              }
              std::cerr << "\n";
            });
      }
      client->drain();
    }

    if (do_tiles && !sized_uris.empty()) {
      if (!quiet) {
        std::cerr << "=== phase: tile pyramid ===\n"
                  << "Build Galapix-style 256×256 JPEG cells for "
                  << sized_uris.size() << " URI(s) "
                  << "(min_scale=" << tile_min_scale
                  << " max_scale=" << tile_max_scale << ").\n"
                  << "ready=stored, miss=encode failed (check archive extract / format).\n";
      }
      std::atomic<int> tile_done{0};
      const int tile_total = static_cast<int>(sized_uris.size());
      for (const auto& uri : sized_uris) {
        client->request_tile_pyramid(
            uri, tile_min_scale, tile_max_scale,
            [&](std::string u, int /*s*/, int /*x*/, int /*y*/,
                std::optional<thumtoo::TileBlob> t) {
              if (quiet) return;
              const int n = ++tile_done;
              std::lock_guard lock(progress_mu);
              std::cerr << "[tiles " << n << "/" << tile_total << "] "
                        << (t ? "ready" : "miss") << "  " << u << "\n";
            });
      }
      client->drain();
    }

    // Status tallies from redesign Store (best-effort after drain).
    auto& store = client->store();
    if (!quiet) {
      std::cerr << "=== summary ===\n";
    }
    std::cout << "registered " << paths.size() << " path(s), queued " << total
              << " probe(s) under " << cache << "\n"
              << "blobs=" << store.count_blobs()
              << " locators=" << store.count_locators()
              << " media=" << store.count_media()
              << " tiles=" << store.count_tiles() << "\n";
    if (!quiet && do_tiles) {
      std::cerr
          << "Inspect one URI: thumtoo-status path <URI|PATH>\n"
          << "(per-scale have/expected/missing, LQIP, min/max scale)\n";
    }
    if (show_stats || do_tiles || ladder_edge > 0 || do_lqip) {
      if (stats_line)
        std::cerr << thumtoo::global_build_stats().summary_line() << "\n";
      else
        std::cerr << thumtoo::global_build_stats().summary_pretty();
      if (jobs == 0) {
        unsigned hw = std::thread::hardware_concurrency();
        if (hw == 0) hw = 1;
        std::cerr << "workers: " << hw << " (auto)\n";
      } else {
        std::cerr << "workers: " << jobs << "\n";
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
