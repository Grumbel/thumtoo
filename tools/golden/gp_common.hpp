// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared helpers for the golden-path tools (tools/golden/gp_*.cpp).
// Header-only and free of thumtoo library dependencies on purpose: golden
// tools measure "the obvious correct thing" without Client/Store linkage.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace gp {

using clock_type = std::chrono::steady_clock;

inline double ms_since(clock_type::time_point t0) {
  return std::chrono::duration<double, std::milli>(clock_type::now() - t0)
      .count();
}

/// Wall-clock summary of `samples` timed runs (after one untimed warmup).
struct Timing {
  double median = 0;
  double min = 0;
  double max = 0;
  int samples = 0;
};

/// One untimed warmup call, then `repeats` timed calls (at least one).
inline Timing time_median(int repeats, const std::function<void()>& fn) {
  repeats = std::max(1, repeats);
  std::vector<double> times;
  times.reserve(static_cast<std::size_t>(repeats));
  fn();
  for (int i = 0; i < repeats; ++i) {
    const auto t0 = clock_type::now();
    fn();
    times.push_back(ms_since(t0));
  }
  std::sort(times.begin(), times.end());
  return Timing{times[times.size() / 2], times.front(), times.back(),
                static_cast<int>(times.size())};
}

/// Write `s` as a JSON string literal (quotes included).
inline void json_string(std::ostream& os, std::string_view s) {
  os << '"';
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    switch (c) {
      case '"': os << "\\\""; break;
      case '\\': os << "\\\\"; break;
      case '\n': os << "\\n"; break;
      case '\r': os << "\\r"; break;
      case '\t': os << "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          os << buf;
        } else {
          os << ch;
        }
    }
  }
  os << '"';
}

}  // namespace gp
