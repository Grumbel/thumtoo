// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/client.hpp"
#include "thumtoo/image.hpp"
#include "thumtoo/status.hpp"
#include "thumtoo/uri.hpp"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace {

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
      << "Usage: " << argv0
      << " [--cache DIR] [--quiet] [--ladder EDGE] [--tiles] PATH [PATH...]\n"
      << "  Register paths in the thumtoo cache and schedule size probes.\n"
      << "  Size probes set native width×height (status Incomplete until a ladder\n"
      << "  exists). Use --ladder EDGE to also encode display JXL levels up to EDGE.\n"
      << "  Archive paths (zip/cbz/rar/…) expand image members as //archive: URIs.\n"
      << "  PDF paths expand pages as //page:N URIs (1-based; prepare caps at 512).\n"
      << "  Progress lines go to stderr; final summary to stdout.\n"
      << "  --quiet         suppress per-job progress lines\n"
      << "  --ladder EDGE   after size probes, request pixels (long-edge EDGE)\n"
      << "  --tiles         after probes, build Galapix-style 256x256 JPEG tile pyramid\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path cache = default_cache_root();
  std::vector<std::filesystem::path> paths;
  bool quiet = false;
  int ladder_edge = 0;
  bool do_tiles = false;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
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
    if (a == "--tiles") {
      do_tiles = true;
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
    auto client = thumtoo::Client::open(cache);

    std::mutex progress_mu;
    std::atomic<int> completed{0};
    std::atomic<size_t> total_atom{0};
    std::vector<std::string> sized_uris;
    sized_uris.reserve(256);

    const size_t total = client->prepare_paths(
        paths,
        [&](std::string uri, std::optional<thumtoo::Size> size) {
          if (size && ladder_edge > 0) {
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
          std::lock_guard lock(progress_mu);
          std::cerr << "[" << n;
          if (tot > 0) std::cerr << "/" << tot;
          std::cerr << "] " << status << "  " << uri;
          if (size) {
            std::cerr << "  " << size->width << "x" << size->height;
          }
          std::cerr << detail << "\n";
        });
    total_atom.store(total, std::memory_order_release);

    if (!quiet && total == 0) {
      std::cerr << "nothing to do (all paths already ready or invalid)\n";
    }

    client->drain();

    if (ladder_edge > 0 && !sized_uris.empty()) {
      if (!quiet) {
        std::cerr << "encoding ladder (edge=" << ladder_edge << ") for "
                  << sized_uris.size() << " uri(s)…\n";
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
        std::cerr << "encoding tile pyramids for " << sized_uris.size()
                  << " uri(s)…\n";
      }
      std::atomic<int> tile_done{0};
      const int tile_total = static_cast<int>(sized_uris.size());
      for (const auto& uri : sized_uris) {
        client->request_tile_pyramid(
            uri, 0, -1,
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

    // Status tallies from the content table (best-effort after drain).
    const auto ncontent = client->db().count_content();
    const int list_limit =
        static_cast<int>(ncontent > 0 ? ncontent : 1);
    int ready = 0, failed = 0, unsupported = 0, pending = 0, incomplete = 0;
    for (const auto& row : client->db().list_content(list_limit)) {
      switch (row.status) {
        case thumtoo::ContentStatus::Ready:
          ++ready;
          break;
        case thumtoo::ContentStatus::Failed:
          ++failed;
          break;
        case thumtoo::ContentStatus::Unsupported:
          ++unsupported;
          break;
        case thumtoo::ContentStatus::Pending:
          ++pending;
          break;
        case thumtoo::ContentStatus::Incomplete:
          ++incomplete;
          break;
      }
    }

    std::cout << "registered " << paths.size() << " path(s), queued " << total
              << " probe(s) under " << cache << "\n"
              << "content=" << ncontent
              << " locators=" << client->db().count_locators()
              << " tiles=" << client->db().count_tiles()
              << " ready=" << ready << " failed=" << failed
              << " unsupported=" << unsupported;
    if (pending) std::cout << " pending=" << pending;
    if (incomplete) std::cout << " incomplete=" << incomplete;
    std::cout << "\n";
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
