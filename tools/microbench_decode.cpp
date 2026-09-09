// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Standalone-ish microbench for JPEG decode paths used by thumtoo.
// Build with THUMTOO_BUILD_TOOLS (links vips via image.cpp / Client not required).
//
// Measures:
//   - header / sequential size
//   - full load
//   - vips_jpegload shrink=2/4/8
//   - vips_thumbnail edge
//   - build_tile_cell* style shrink mapping
//
// Usage:
//   microbench_decode [--repeat N] FILE.jpg...

#include <vips/vips.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

using clock_type = std::chrono::steady_clock;

double ms_since(clock_type::time_point t0) {
  return std::chrono::duration<double, std::milli>(clock_type::now() - t0)
      .count();
}

struct Stats {
  double median = 0;
  double min = 0;
  double max = 0;
};

Stats run_median(int repeats, const std::function<void()>& fn) {
  std::vector<double> times;
  times.reserve(static_cast<std::size_t>(repeats));
  fn();  // warmup
  for (int i = 0; i < repeats; ++i) {
    const auto t0 = clock_type::now();
    fn();
    times.push_back(ms_since(t0));
  }
  std::sort(times.begin(), times.end());
  Stats s;
  s.min = times.front();
  s.max = times.back();
  s.median = times[times.size() / 2];
  return s;
}

void ensure_vips_lib() {
  static bool once = false;
  if (!once) {
    if (VIPS_INIT("microbench_decode")) {
      std::cerr << "VIPS_INIT failed\n";
      std::exit(1);
    }
    once = true;
  }
}

}  // namespace

int main(int argc, char** argv) {
  int repeats = 5;
  std::vector<std::filesystem::path> files;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--repeat" && i + 1 < argc) {
      repeats = std::atoi(argv[++i]);
      if (repeats < 1) repeats = 1;
    } else if (a == "-h" || a == "--help") {
      std::cerr << "Usage: " << argv[0] << " [--repeat N] FILE.jpg...\n";
      return 0;
    } else {
      files.emplace_back(a);
    }
  }
  if (files.empty()) {
    std::cerr << "need at least one file\n";
    return 1;
  }

  ensure_vips_lib();

  std::cout << "file,mpix,size_ms,full_ms,shrink2_ms,shrink4_ms,shrink8_ms,"
               "thumb32_ms,thumb256_ms\n";

  for (const auto& path : files) {
    ensure_vips_lib();
    VipsImage* hdr = vips_image_new_from_file(
        path.string().c_str(), "access", VIPS_ACCESS_SEQUENTIAL, nullptr);
    if (!hdr) {
      std::cerr << "skip " << path << " (open failed)\n";
      continue;
    }
    const int w = vips_image_get_width(hdr);
    const int h = vips_image_get_height(hdr);
    g_object_unref(hdr);
    const double mpix = static_cast<double>(w) * static_cast<double>(h) / 1e6;

    auto size_s = run_median(repeats, [&] {
      VipsImage* img = vips_image_new_from_file(
          path.string().c_str(), "access", VIPS_ACCESS_SEQUENTIAL, nullptr);
      if (img) {
        (void)vips_image_get_width(img);
        (void)vips_image_get_height(img);
        g_object_unref(img);
      }
    });

    auto full_s = run_median(std::max(1, repeats / 2), [&] {
      VipsImage* img = vips_image_new_from_file(path.string().c_str(), nullptr);
      if (img) {
        // Force decode by writing to memory.
        size_t len = 0;
        void* buf = vips_image_write_to_memory(img, &len);
        if (buf) g_free(buf);
        g_object_unref(img);
      }
    });

    auto shrink_s = [&](int shrink) {
      return run_median(repeats, [&] {
        VipsImage* img = nullptr;
        if (vips_jpegload(path.string().c_str(), &img, "shrink", shrink,
                          nullptr) == 0 &&
            img) {
          size_t len = 0;
          void* buf = vips_image_write_to_memory(img, &len);
          if (buf) g_free(buf);
          g_object_unref(img);
        }
      });
    };

    auto s2 = shrink_s(2);
    auto s4 = shrink_s(4);
    auto s8 = shrink_s(8);

    auto thumb_s = [&](int edge) {
      return run_median(repeats, [&] {
        VipsImage* thumb = nullptr;
        if (vips_thumbnail(path.string().c_str(), &thumb, edge, "size",
                           VIPS_SIZE_DOWN, nullptr) == 0 &&
            thumb) {
          size_t len = 0;
          void* buf = vips_image_write_to_memory(thumb, &len);
          if (buf) g_free(buf);
          g_object_unref(thumb);
        }
      });
    };

    auto t32 = thumb_s(32);
    auto t256 = thumb_s(256);

    std::printf(
        "%s,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", path.filename().c_str(),
        mpix, size_s.median, full_s.median, s2.median, s4.median, s8.median,
        t32.median, t256.median);
  }

  return 0;
}
