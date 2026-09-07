// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace thumtoo {

/** Cumulative CPU-path timings for prepare / tile generation (thread-safe). */
struct BuildStats {
  std::atomic<std::uint64_t> archive_extract_ns{0};
  std::atomic<std::uint64_t> image_load_ns{0};
  std::atomic<std::uint64_t> shrink_ns{0};
  std::atomic<std::uint64_t> jpeg_encode_ns{0};
  std::atomic<std::uint64_t> tiles_encoded{0};
  std::atomic<std::uint64_t> archive_bytes{0};

  void reset() {
    archive_extract_ns.store(0, std::memory_order_relaxed);
    image_load_ns.store(0, std::memory_order_relaxed);
    shrink_ns.store(0, std::memory_order_relaxed);
    jpeg_encode_ns.store(0, std::memory_order_relaxed);
    tiles_encoded.store(0, std::memory_order_relaxed);
    archive_bytes.store(0, std::memory_order_relaxed);
  }

  [[nodiscard]] std::string summary_line() const;
};

BuildStats& global_build_stats();

/** Adds elapsed wall time to \a counter on destruction. */
class ScopedNsAccumulator {
 public:
  explicit ScopedNsAccumulator(std::atomic<std::uint64_t>& counter)
      : counter_(counter), t0_(std::chrono::steady_clock::now()) {}
  ScopedNsAccumulator(const ScopedNsAccumulator&) = delete;
  ScopedNsAccumulator& operator=(const ScopedNsAccumulator&) = delete;
  ~ScopedNsAccumulator() {
    const auto dt = std::chrono::steady_clock::now() - t0_;
    const auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count();
    if (ns > 0) {
      counter_.fetch_add(static_cast<std::uint64_t>(ns),
                         std::memory_order_relaxed);
    }
  }

 private:
  std::atomic<std::uint64_t>& counter_;
  std::chrono::steady_clock::time_point t0_;
};

}  // namespace thumtoo
