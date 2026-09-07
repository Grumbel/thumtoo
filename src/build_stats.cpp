// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/build_stats.hpp"

#include <sstream>

namespace thumtoo {

BuildStats& global_build_stats() {
  static BuildStats s;
  return s;
}

void BuildStats::reset() {
  archive_extract_ns.store(0, std::memory_order_relaxed);
  image_load_ns.store(0, std::memory_order_relaxed);
  shrink_ns.store(0, std::memory_order_relaxed);
  jpeg_encode_ns.store(0, std::memory_order_relaxed);
  thumb_ns.store(0, std::memory_order_relaxed);
  jxl_encode_ns.store(0, std::memory_order_relaxed);
  levels_encoded.store(0, std::memory_order_relaxed);
  tiles_encoded.store(0, std::memory_order_relaxed);
  archive_bytes.store(0, std::memory_order_relaxed);
  wall_start = std::chrono::steady_clock::now();
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
  const auto thumb = thumb_ns.load(std::memory_order_relaxed);
  const auto jxl = jxl_encode_ns.load(std::memory_order_relaxed);
  const auto levels = levels_encoded.load(std::memory_order_relaxed);
  const auto tiles = tiles_encoded.load(std::memory_order_relaxed);
  const auto bytes = archive_bytes.load(std::memory_order_relaxed);
  const auto cpu_sum = extract + load + shrink + jpeg + thumb + jxl;

  const auto wall_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - wall_start)
          .count());

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(3);
  // wall = real elapsed; cpu-* = summed thread scopes (can exceed wall).
  out << "timings: wall=" << ns_to_s(wall_ns) << "s"
      << " | cpu: extract=" << ns_to_s(extract) << "s"
      << " load=" << ns_to_s(load) << "s"
      << " shrink=" << ns_to_s(shrink) << "s"
      << " jpeg=" << ns_to_s(jpeg) << "s"
      << " thumb=" << ns_to_s(thumb) << "s"
      << " jxl=" << ns_to_s(jxl) << "s"
      << " (cpu-sum=" << ns_to_s(cpu_sum) << "s)"
      << " levels=" << levels
      << " tiles=" << tiles;
  if (bytes > 0) {
    out.precision(1);
    out << " archive_MiB=" << (static_cast<double>(bytes) / (1024.0 * 1024.0));
  }
  if (cpu_sum > 0) {
    out.precision(0);
    out << " | cpu-share: extract=" << (100.0 * extract / cpu_sum) << "%"
        << " load=" << (100.0 * load / cpu_sum) << "%"
        << " shrink=" << (100.0 * shrink / cpu_sum) << "%"
        << " jpeg=" << (100.0 * jpeg / cpu_sum) << "%"
        << " thumb=" << (100.0 * thumb / cpu_sum) << "%"
        << " jxl=" << (100.0 * jxl / cpu_sum) << "%";
  }
  if (wall_ns > 0 && cpu_sum > 0) {
    out.precision(2);
    out << " | parallel~=" << (ns_to_s(cpu_sum) / ns_to_s(wall_ns)) << "x";
  }
  return out.str();
}

}  // namespace thumtoo
