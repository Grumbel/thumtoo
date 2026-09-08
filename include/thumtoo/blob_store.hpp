// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thumtoo/database.hpp"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace thumtoo {

/// Separate SQLite file for ladder payload (DESIGN: avoid millions of tiny files).
/// Metadata remains in index.sqlite; this holds the encoded bytes only.
class BlobStore {
 public:
  BlobStore() = default;
  BlobStore(const BlobStore&) = delete;
  BlobStore& operator=(const BlobStore&) = delete;
  BlobStore(BlobStore&&) noexcept;
  BlobStore& operator=(BlobStore&&) noexcept;
  ~BlobStore();

  static BlobStore open(const std::filesystem::path& cache_root);

  [[nodiscard]] const std::filesystem::path& db_path() const { return db_path_; }

  void put_level(std::string_view content_id, int max_edge, int frame_idx,
                 int width, int height, std::string_view codec, int quality,
                 const std::uint8_t* data, std::size_t size);

  [[nodiscard]] std::optional<std::vector<std::uint8_t>> get_level(
      std::string_view content_id, int max_edge, int frame_idx) const;

  [[nodiscard]] std::int64_t count_levels() const;

  void put_tile(std::string_view content_id, int scale, int x, int y,
                int width, int height, std::string_view codec, int quality,
                const std::uint8_t* data, std::size_t size);

  void delete_tile(std::string_view content_id, int scale, int x, int y);
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> get_tile(
      std::string_view content_id, int scale, int x, int y) const;

  [[nodiscard]] std::int64_t count_tiles() const;

  /// Durable HTTP(S) response body keyed by URL (DESIGN retrieval).
  void put_http_body(std::string_view url, const std::uint8_t* data,
                    std::size_t size, std::int64_t fetched_at_unix_s);
  /// Returns body if present and not older than max_age_s (0 = any age).
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> get_http_body(
      std::string_view url, std::int64_t max_age_s = 0) const;
  [[nodiscard]] std::int64_t count_http_bodies() const;

 private:
  explicit BlobStore(sqlite3* db, std::filesystem::path db_path);
  void exec(const char* sql) const;
  void migrate_or_init();

  mutable std::recursive_mutex mu_;
  sqlite3* db_ = nullptr;
  std::filesystem::path db_path_;
};

}  // namespace thumtoo
