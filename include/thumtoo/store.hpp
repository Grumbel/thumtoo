// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace thumtoo {

/// Hash algorithm ids seeded in index.hash_algo (stable across runs).
enum class HashAlgoId : int {
  Sha256 = 1,
  Sha1 = 2,
  Md5 = 3,
  Crc32 = 4,
};

/// Codec ids seeded in index.codec.
enum class CodecId : int {
  Jpeg = 1,
  Jxl = 2,
  Png = 3,
};

/// Index DB status values for blob.status (extensible).
enum class BlobStatus : int {
  Unknown = 0,
  Ok = 1,
  Missing = 2,
  Error = 3,
};

/// Redesign index/bulk/user stores (docs/DATABASE.md, docs/PLAN.md Phase A).
///
/// Legacy Client still uses Database + BlobStore. This type is the new spine:
/// integer blob ids, blob_hash digests, locators, three-file layout.
/// Not thread-safe; one writer discipline at a higher layer.
class Store {
 public:
  Store() = default;
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;
  Store(Store&&) noexcept;
  Store& operator=(Store&&) noexcept;
  ~Store();

  struct Paths {
    std::filesystem::path cache_root;  // index + bulk
    std::filesystem::path data_root;   // user (may equal cache_root for tests)
  };

  /// Open or create the three SQLite files. If index exists with an unsupported
  /// or legacy (pre-redesign) schema_version, it is removed and recreated; user
  /// DB is never deleted by this path.
  ///
  /// Layout:
  ///   cache_root/index.sqlite
  ///   cache_root/bulk.sqlite
  ///   data_root/user.sqlite
  static Store open(const Paths& paths);

  /// Convenience: cache_root for index+bulk, data_root = cache_root/user side
  /// under the same path (tests). Production should pass XDG data for user.
  static Store open(const std::filesystem::path& cache_root);

  [[nodiscard]] const std::filesystem::path& cache_root() const {
    return cache_root_;
  }
  [[nodiscard]] const std::filesystem::path& data_root() const {
    return data_root_;
  }
  [[nodiscard]] int index_schema_version() const { return index_schema_version_; }
  [[nodiscard]] bool opened() const { return index_ != nullptr; }

  // --- schema_meta on index ---
  [[nodiscard]] std::optional<std::string> meta_get(std::string_view key) const;
  void meta_set(std::string_view key, std::string_view value);

  // --- blob ---
  struct BlobRow {
    std::int64_t id = 0;
    std::optional<std::int64_t> size;
    BlobStatus status = BlobStatus::Unknown;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
  };

  /// Insert a new blob row; returns id.
  [[nodiscard]] std::int64_t insert_blob(std::optional<std::int64_t> size,
                                         BlobStatus status = BlobStatus::Unknown);

  [[nodiscard]] std::optional<BlobRow> find_blob(std::int64_t id) const;
  void set_blob_size(std::int64_t id, std::int64_t size);
  void set_blob_status(std::int64_t id, BlobStatus status);

  // --- blob_hash ---
  /// Store digest (raw bytes). Replaces existing digest for (blob, algo).
  void put_hash(std::int64_t blob_id, HashAlgoId algo,
                std::span<const std::uint8_t> digest);

  /// Lookup blob id by algorithm + raw digest.
  [[nodiscard]] std::optional<std::int64_t> find_blob_by_hash(
      HashAlgoId algo, std::span<const std::uint8_t> digest) const;

  [[nodiscard]] std::optional<std::vector<std::uint8_t>> get_hash(
      std::int64_t blob_id, HashAlgoId algo) const;

  /// Canonical public ref when SHA-256 is known: "blob:sha256:" + lowercase hex.
  [[nodiscard]] std::optional<std::string> blob_ref_sha256(
      std::int64_t blob_id) const;

  /// Parse "blob:sha256:<hex>" or bare 64-hex → raw 32 bytes; nullopt if invalid.
  [[nodiscard]] static std::optional<std::vector<std::uint8_t>> parse_sha256_digest(
      std::string_view ref_or_hex);

  /// Format raw 32-byte SHA-256 as "blob:sha256:" + hex.
  [[nodiscard]] static std::string format_blob_ref_sha256(
      std::span<const std::uint8_t> digest32);

  // --- locator ---
  struct LocatorRow {
    std::int64_t id = 0;
    std::string uri;
    std::optional<std::int64_t> blob_id;
    std::optional<std::int64_t> size;
    std::optional<std::int64_t> mtime_ns;
    std::int64_t updated_at = 0;
  };

  /// Insert or update by uri (unique). Returns locator id.
  std::int64_t upsert_locator(std::string_view uri,
                              std::optional<std::int64_t> blob_id,
                              std::optional<std::int64_t> size,
                              std::optional<std::int64_t> mtime_ns);

  void bind_locator_blob(std::string_view uri, std::int64_t blob_id);

  [[nodiscard]] std::optional<LocatorRow> find_locator(std::string_view uri) const;
  [[nodiscard]] std::vector<LocatorRow> list_locators_for_blob(
      std::int64_t blob_id, int limit = 100) const;

  [[nodiscard]] std::int64_t count_blobs() const;
  [[nodiscard]] std::int64_t count_locators() const;

 private:
  Store(sqlite3* index, sqlite3* bulk, sqlite3* user,
        std::filesystem::path cache_root, std::filesystem::path data_root,
        int index_schema_version);

  void exec_index(const char* sql) const;
  void exec_bulk(const char* sql) const;
  void exec_user(const char* sql) const;
  void migrate_or_init_index();
  void migrate_or_init_bulk();
  void migrate_or_init_user();
  void seed_lookups();

  static sqlite3* open_sqlite(const std::filesystem::path& path);
  static void close_sqlite(sqlite3*& db);

  sqlite3* index_ = nullptr;
  sqlite3* bulk_ = nullptr;
  sqlite3* user_ = nullptr;
  std::filesystem::path cache_root_;
  std::filesystem::path data_root_;
  int index_schema_version_ = 0;
};

}  // namespace thumtoo
