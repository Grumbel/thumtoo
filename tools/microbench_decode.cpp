// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Golden-path JPEG decode microbench (vips only — no thumtoo Client/Store).
// Measures size header, full load, jpegload shrink=2/4/8, and thumbnail edges.
//
// Usage:
//   thumtoo-microbench-decode [--repeat N] [--json] FILE.jpg...

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

void json_escape(std::ostream& os, const std::string& s) {
  os << '"';
  for (char c : s) {
    switch (c) {
      case '"':
        os << "\\\"";
        break;
      case '\\':
        os << "\\\\";
        break;
      case '\n':
        os << "\\n";
        break;
      default:
        os << c;
    }
  }
  os << '"';
}

}  // namespace

int main(int argc, char** argv) {
  int repeats = 5;
  bool json_out = false;
  std::vector<std::filesystem::path> files;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--repeat" && i + 1 < argc) {
      repeats = std::atoi(argv[++i]);
      if (repeats < 1) repeats = 1;
    } else if (a == "--json") {
      json_out = true;
    } else if (a == "-h" || a == "--help") {
      std::cerr
          << "Usage: " << argv[0] << " [--repeat N] [--json] FILE.jpg...\n"
          << "Golden-path vips JPEG decode timings (no thumtoo Client).\n";
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

  if (!json_out) {
    std::cout << "file,mpix,size_ms,full_ms,shrink2_ms,shrink4_ms,shrink8_ms,"
                 "thumb32_ms,thumb256_ms\n";
  } else {
    std::cout << "{\n  \"schema\": 1,\n  \"tool\": \"thumtoo-microbench-decode\",\n"
                 "  \"repeats\": "
              << repeats
              << ",\n  \"warmups\": 1,\n  \"cases\": [\n";
  }

  bool first_case = true;
  for (const auto& path : files) {
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

    if (!json_out) {
      std::printf(
          "%s,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
          path.filename().c_str(), mpix, size_s.median, full_s.median,
          s2.median, s4.median, s8.median, t32.median, t256.median);
    } else {
      if (!first_case) std::cout << ",\n";
      first_case = false;
      std::cout << "    {\n      \"path\": ";
      json_escape(std::cout, path.string());
      std::cout << ",\n      \"file\": ";
      json_escape(std::cout, path.filename().string());
      std::cout << ",\n      \"width\": " << w << ",\n      \"height\": " << h
                << ",\n      \"mpix\": " << mpix << ",\n      \"metrics\": {\n"
                << "        \"size_ms\": " << size_s.median << ",\n"
                << "        \"full_ms\": " << full_s.median << ",\n"
                << "        \"shrink2_ms\": " << s2.median << ",\n"
                << "        \"shrink4_ms\": " << s4.median << ",\n"
                << "        \"shrink8_ms\": " << s8.median << ",\n"
                << "        \"thumb32_ms\": " << t32.median << ",\n"
                << "        \"thumb256_ms\": " << t256.median << "\n"
                << "      }\n    }";
    }
  }

  if (json_out) {
    std::cout << "\n  ]\n}\n";
  }

  return 0;
}
