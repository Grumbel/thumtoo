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

/// media.kind values (docs/DATABASE.md).
enum class MediaKind : int {
  Unknown = 0,
  Image = 1,
  Document = 2,
  Video = 3,
  Audio = 4,
};

/// region.kind values.
enum class RegionKind : int {
  Full = 0,     ///< Single-frame image (mandatory for image media)
  Page = 1,     ///< Document page (key = 1-based page number string)
  Fragment = 2, ///< e.g. HTML id (future)
  TimeRange = 3,
};

/// media.status / similar.
enum class MediaStatus : int {
  Unknown = 0,
  Ready = 1,
  Pending = 2,
  Error = 3,
  Unsupported = 4,
};

/// Redesign index/bulk/user stores (docs/DATABASE.md, docs/PLAN.md).
///
/// Legacy Client still uses Database + BlobStore. This type is the new spine:
/// integer blob ids, blob_hash digests, locators, media/region/tiles.
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

  // --- media ---
  struct MediaRow {
    std::int64_t id = 0;
    std::int64_t blob_id = 0;
    MediaKind kind = MediaKind::Unknown;
    std::optional<int> width;
    std::optional<int> height;
    std::optional<std::int64_t> duration_ms;
    std::optional<int> page_count;
    std::optional<int> still_count;
    MediaStatus status = MediaStatus::Unknown;
    std::optional<std::string> error_code;
    std::int64_t updated_at = 0;
  };

  /// Insert media for blob+kind. Fails if (blob_id, kind) already exists.
  [[nodiscard]] std::int64_t insert_media(std::int64_t blob_id, MediaKind kind,
                                          std::optional<int> width = {},
                                          std::optional<int> height = {},
                                          MediaStatus status = MediaStatus::Unknown);

  /// Ensure image media + full region exist for blob; returns media_id.
  /// Creates media(kind=image) and region(kind=full, key="") if missing.
  [[nodiscard]] std::int64_t ensure_image_media(std::int64_t blob_id,
                                                std::optional<int> width = {},
                                                std::optional<int> height = {});

  [[nodiscard]] std::optional<MediaRow> find_media(std::int64_t media_id) const;
  [[nodiscard]] std::optional<MediaRow> find_media_for_blob(
      std::int64_t blob_id, MediaKind kind) const;

  void set_media_size(std::int64_t media_id, int width, int height);
  void set_media_page_count(std::int64_t media_id, int page_count);
  void set_media_status(std::int64_t media_id, MediaStatus status,
                        std::optional<std::string_view> error_code = {});

  // --- region ---
  struct RegionRow {
    std::int64_t id = 0;
    std::int64_t media_id = 0;
    RegionKind kind = RegionKind::Full;
    std::string key;
    std::optional<int> ordinal;
  };

  [[nodiscard]] std::int64_t insert_region(std::int64_t media_id, RegionKind kind,
                                           std::string_view key,
                                           std::optional<int> ordinal = {});

  /// Find or create region; returns region_id.
  [[nodiscard]] std::int64_t ensure_region(std::int64_t media_id, RegionKind kind,
                                           std::string_view key,
                                           std::optional<int> ordinal = {});

  [[nodiscard]] std::optional<RegionRow> find_region(std::int64_t region_id) const;
  [[nodiscard]] std::optional<RegionRow> find_region_by_key(
      std::int64_t media_id, RegionKind kind, std::string_view key) const;

  /// Full region for image media (kind=full, key empty).
  [[nodiscard]] std::optional<RegionRow> find_full_region(
      std::int64_t media_id) const;

  // --- tile (index metadata + bulk payload) ---
  struct TileRow {
    std::int64_t media_id = 0;
    std::int64_t region_id = 0;
    int scale = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    CodecId codec_id = CodecId::Jpeg;
    std::optional<int> quality;
  };

  /// Upsert tile metadata (index) and payload (bulk).
  void put_tile(const TileRow& meta, std::span<const std::uint8_t> data);

  [[nodiscard]] bool has_tile(std::int64_t media_id, std::int64_t region_id,
                              int scale, int x, int y) const;

  /// Index metadata only (no payload).
  [[nodiscard]] std::optional<TileRow> find_tile_meta(std::int64_t media_id,
                                                     std::int64_t region_id,
                                                     int scale, int x,
                                                     int y) const;

  /// Payload from bulk DB; nullopt if missing.
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> get_tile_data(
      std::int64_t media_id, std::int64_t region_id, int scale, int x,
      int y) const;

  void delete_tiles_for_region(std::int64_t media_id, std::int64_t region_id);

  [[nodiscard]] std::int64_t count_tiles() const;
  [[nodiscard]] std::int64_t count_media() const;
  [[nodiscard]] std::int64_t count_regions() const;

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
