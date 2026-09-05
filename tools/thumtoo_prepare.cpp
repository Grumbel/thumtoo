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
      << "Usage: " << argv0 << " [--cache DIR] [--quiet] PATH [PATH...]\n"
      << "  Register paths in the thumtoo cache and schedule size probes.\n"
      << "  Probe dimensions (libvips), promote sha256 content id, write JXL ladder blobs.\n"
      << "  Archive paths (zip/cbz/rar/…) expand image members as //archive: URIs.\n"
      << "  Progress lines go to stderr; final summary to stdout.\n"
      << "  --quiet  suppress per-job progress lines\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path cache = default_cache_root();
  std::vector<std::filesystem::path> paths;
  bool quiet = false;

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

    const size_t total = client->prepare_paths(
        paths,
        [&](std::string uri, std::optional<thumtoo::Size> size) {
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
