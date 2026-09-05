// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thumtoo/database.hpp"

#include <cstdint>
#include <filesystem>
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

 private:
  explicit BlobStore(sqlite3* db, std::filesystem::path db_path);
  void exec(const char* sql) const;
  void migrate_or_init();

  sqlite3* db_ = nullptr;
  std::filesystem::path db_path_;
};

}  // namespace thumtoo
