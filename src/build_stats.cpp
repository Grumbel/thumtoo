// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/build_stats.hpp"

#include <cstdio>
#include <sstream>

namespace thumtoo {

BuildStats& global_build_stats() {
  static BuildStats s;
  return s;
}

namespace {

double ns_to_s(std::uint64_t ns) {
  return static_cast<double>(ns) / 1e9;
}

}  // namespace

std::string BuildStats::summary_line() const {
  const auto extract = archive_extract_ns.load(std::memory_order_relaxed);
  const auto load = image_load_ns.load(std::memory_order_relaxed);
  const auto shrink = shrink_ns.load(std::memory_order_relaxed);
  const auto jpeg = jpeg_encode_ns.load(std::memory_order_relaxed);
  const auto tiles = tiles_encoded.load(std::memory_order_relaxed);
  const auto bytes = archive_bytes.load(std::memory_order_relaxed);
  const auto total = extract + load + shrink + jpeg;

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(3);
  out << "timings: extract=" << ns_to_s(extract) << "s"
      << " load=" << ns_to_s(load) << "s"
      << " shrink=" << ns_to_s(shrink) << "s"
      << " jpeg=" << ns_to_s(jpeg) << "s"
      << " (sum=" << ns_to_s(total) << "s)"
      << " tiles=" << tiles;
  if (bytes > 0) {
    out.precision(1);
    out << " archive_MiB=" << (static_cast<double>(bytes) / (1024.0 * 1024.0));
  }
  if (total > 0) {
    out.precision(0);
    out << " | share: extract=" << (100.0 * extract / total) << "%"
        << " load=" << (100.0 * load / total) << "%"
        << " shrink=" << (100.0 * shrink / total) << "%"
        << " jpeg=" << (100.0 * jpeg / total) << "%";
  }
  return out.str();
}

}  // namespace thumtoo
