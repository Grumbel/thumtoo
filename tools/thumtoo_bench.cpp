// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/build_stats.hpp"
#include "thumtoo/client.hpp"
#include "thumtoo/image.hpp"
#include "thumtoo/uri.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <thread>
#include <string>
#include <vector>

namespace {

std::filesystem::path default_cache_root() {
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
    return std::filesystem::path(xdg) / "thumtoo-bench";
  }
  if (const char* home = std::getenv("HOME"); home && *home) {
    return std::filesystem::path(home) / ".cache" / "thumtoo-bench";
  }
  return std::filesystem::path(".cache") / "thumtoo-bench";
}

void usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0 << " [OPTION]... PATH [PATH...]\n"
      << "\n"
      << "Benchmark thumtoo cold-cache phases on PATH(s) (files or archives).\n"
      << "Each phase uses a fresh stats window; overall wall is also reported.\n"
      << "\n"
      << "Options:\n"
      << "  -h, --help         show this help and exit\n"
      << "      --cache DIR    cache root (default: …/thumtoo-bench; wiped each run)\n"
      << "      --jobs N       worker threads (default: CPUs, max 32)\n"
      << "      --ladder EDGE  preview long-edge (default: 256; 0 = skip)\n"
      << "      --tiles        run full tile pyramid phase\n"
      << "      --tile-cell    request one tile (0,0,0) per URI after probes\n"
      << "      --no-probe     skip size-probe phase (still needed for later phases)\n"
      << "\n"
      << "Phases (in order):\n"
      << "  1. size probe\n"
      << "  2. preview JXL (--ladder EDGE)\n"
      << "  3. single tile cell (--tile-cell)\n"
      << "  4. full tile pyramid (--tiles)\n";
}

struct PhaseResult {
  std::string name;
  double wall_s = 0;
  std::string pretty;
};

double wall_s_now(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
      .count();
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path cache = default_cache_root();
  std::vector<std::filesystem::path> paths;
  unsigned jobs = 0;
  int ladder_edge = 256;
  bool do_tiles = false;
  bool do_tile_cell = false;
  bool do_probe = true;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      usage(argv[0]);
      return 0;
    }
    if (a == "--cache" && i + 1 < argc) {
      cache = argv[++i];
      continue;
    }
    if (a == "--jobs" && i + 1 < argc) {
      int v = std::atoi(argv[++i]);
      if (v < 0) v = 0;
      jobs = static_cast<unsigned>(v);
      continue;
    }
    if (a == "--ladder" && i + 1 < argc) {
      ladder_edge = std::atoi(argv[++i]);
      continue;
    }
    if (a == "--tiles") {
      do_tiles = true;
      continue;
    }
    if (a == "--tile-cell") {
      do_tile_cell = true;
      continue;
    }
    if (a == "--no-probe") {
      do_probe = false;
      continue;
    }
    paths.emplace_back(a);
  }

  if (paths.empty()) {
    usage(argv[0]);
    return 2;
  }

  try {
    std::error_code ec;
    std::filesystem::remove_all(cache, ec);
    std::filesystem::create_directories(cache);

    thumtoo::image_library_init();
    const auto t_all = std::chrono::steady_clock::now();

    auto client = thumtoo::Client::open(cache, {}, jobs);
    std::vector<PhaseResult> phases;
    std::vector<std::string> uris;

    auto run_probe = [&]() {
      thumtoo::global_build_stats().reset();
      const auto t0 = std::chrono::steady_clock::now();
      std::vector<std::string> sized;
      const size_t total = client->prepare_paths(
          paths, [&](std::string uri, std::optional<thumtoo::Size> size) {
            if (size) sized.push_back(std::move(uri));
          });
      (void)total;
      client->drain();
      uris = std::move(sized);
      PhaseResult r;
      r.name = "probe";
      r.wall_s = wall_s_now(t0);
      r.pretty = thumtoo::global_build_stats().summary_pretty();
      phases.push_back(std::move(r));
    };

    if (do_probe || ladder_edge > 0 || do_tiles || do_tile_cell) {
      run_probe();
    }

    if (ladder_edge > 0 && !uris.empty()) {
      thumtoo::global_build_stats().reset();
      const auto t0 = std::chrono::steady_clock::now();
      for (const auto& uri : uris) {
        client->request_pixels(uri, ladder_edge, {});
      }
      client->drain();
      PhaseResult r;
      r.name = "preview(ladder=" + std::to_string(ladder_edge) + ")";
      r.wall_s = wall_s_now(t0);
      r.pretty = thumtoo::global_build_stats().summary_pretty();
      phases.push_back(std::move(r));
    }

    if (do_tile_cell && !uris.empty()) {
      thumtoo::global_build_stats().reset();
      const auto t0 = std::chrono::steady_clock::now();
      for (const auto& uri : uris) {
        // Coarse stand-in: scale 0 cell (0,0) — full-res corner tile.
        client->request_tile(uri, 0, 0, 0, {});
      }
      client->drain();
      PhaseResult r;
      r.name = "tile-cell(scale=0,0,0)";
      r.wall_s = wall_s_now(t0);
      r.pretty = thumtoo::global_build_stats().summary_pretty();
      phases.push_back(std::move(r));
    }

    if (do_tiles && !uris.empty()) {
      thumtoo::global_build_stats().reset();
      const auto t0 = std::chrono::steady_clock::now();
      for (const auto& uri : uris) {
        client->request_tile_pyramid(uri, 0, -1, {});
      }
      client->drain();
      PhaseResult r;
      r.name = "tiles(full pyramid)";
      r.wall_s = wall_s_now(t0);
      r.pretty = thumtoo::global_build_stats().summary_pretty();
      phases.push_back(std::move(r));
    }

    const double all_s = wall_s_now(t_all);

    std::cout << "thumtoo-bench  cache=" << cache << "  uris=" << uris.size()
              << "  paths=" << paths.size() << "\n";
    unsigned hw = jobs;
    if (hw == 0) {
      hw = std::thread::hardware_concurrency();
      if (hw == 0) hw = 1;
    }
    std::cout << "workers=" << hw << "  total_wall=" << std::fixed
              << std::setprecision(3) << all_s << " s\n\n";

    for (const auto& ph : phases) {
      std::cout << "▸ phase " << ph.name << "  wall=" << std::fixed
                << std::setprecision(3) << ph.wall_s << " s\n";
      std::cout << ph.pretty << "\n";
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
