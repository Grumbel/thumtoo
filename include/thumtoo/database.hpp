// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thumtoo/types.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <span>
#include <vector>

struct sqlite3;

namespace thumtoo {

/// Owns a single SQLite connection (WAL). Not thread-safe; use one writer queue
/// at a higher layer (DESIGN concurrency rules).
class Database {
 public:
  Database() = default;
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  Database(Database&&) noexcept;
  Database& operator=(Database&&) noexcept;
  ~Database();

  /// Open or create index at cache_root/index.sqlite and apply schema.
  /// Creates cache_root if needed.
  static Database open(const std::filesystem::path& cache_root);

  [[nodiscard]] const std::filesystem::path& cache_root() const {
    return cache_root_;
  }
  [[nodiscard]] const std::filesystem::path& db_path() const { return db_path_; }
  [[nodiscard]] int schema_version() const { return schema_version_; }

  [[nodiscard]] std::optional<std::string> meta_get(std::string_view key) const;
  void meta_set(std::string_view key, std::string_view value);

  [[nodiscard]] std::int64_t count_content() const;
  [[nodiscard]] std::int64_t count_locators() const;
  [[nodiscard]] std::int64_t count_levels() const;
  [[nodiscard]] std::int64_t count_tiles() const;
  [[nodiscard]] std::int64_t count_directory_snapshots() const;

  struct LocatorRow {
    std::string uri;
    std::optional<std::string> content_id;
    std::optional<std::string> outer_path;
    std::optional<std::string> member_path;
    std::optional<std::int64_t> size;
    std::optional<std::int64_t> mtime_ns;
  };

  [[nodiscard]] std::vector<LocatorRow> list_locators(int limit = 100) const;
  /// All locators pointing at this content_id (rename-safe identity).
  [[nodiscard]] std::vector<LocatorRow> list_locators_for_content_id(
      std::string_view content_id, int limit = 100) const;

  /// Cache-only: uri GLOB or prefix match (prefix is literal, * and ? not special).
  /// Matches uri LIKE prefix || '%' ESCAPE '\\']
  [[nodiscard]] std::vector<LocatorRow> list_locators_by_uri_prefix(
      std::string_view uri_prefix, int limit = 100) const;

  /// Cache-only: outer_path prefix (filesystem path under which files were indexed).
  [[nodiscard]] std::vector<LocatorRow> list_locators_by_outer_path_prefix(
      std::string_view path_prefix, int limit = 100) const;

  /// Cache-only: SQL LIKE on uri (caller supplies pattern; % and _ wildcards).
  [[nodiscard]] std::vector<LocatorRow> list_locators_like(
      std::string_view uri_like_pattern, int limit = 100) const;

  struct ContentRow {
    std::string content_id;
    std::optional<int> width;
    std::optional<int> height;
    std::optional<std::string> format;
    std::optional<std::int64_t> duration_ms;
    std::optional<int> still_count;
    ContentStatus status = ContentStatus::Pending;
    std::optional<std::string> error_code;
    /// Inline LQIP (e.g. ThumbHash); empty if unknown.
    std::vector<std::uint8_t> lqip;
    int lqip_kind = 0;  // kLqipKind*
  };

  [[nodiscard]] std::vector<ContentRow> list_content(int limit = 100) const;

  /// Insert or replace a locator (spike helper; full API later).
  void upsert_locator(const LocatorRow& row);

  /// Insert or replace a content row (spike helper).
  void upsert_content(const ContentRow& row);

  [[nodiscard]] std::optional<LocatorRow> find_locator(std::string_view uri) const;
  [[nodiscard]] std::optional<ContentRow> find_content(std::string_view content_id) const;

  /// Cache-only: ThumbHash (or other LQIP) stored on the content row.
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> get_lqip(
      std::string_view content_id) const;
  void set_lqip(std::string_view content_id, int kind,
                std::span<const std::uint8_t> bytes);


  /// Resolve uri -> content meta via locator join (cache only).
  /// Also accepts content-id URIs (sha256:… / sha1:…) directly.
  [[nodiscard]] std::optional<ContentMeta> meta_for_uri(std::string_view uri) const;
  [[nodiscard]] std::optional<ContentMeta> meta_for_content_id(
      std::string_view content_id) const;

  struct LevelRow {
    std::string content_id;
    int max_edge = 0;
    int frame_idx = 0;
    std::optional<std::int64_t> pts_ms;
    std::optional<int> width;
    std::optional<int> height;
    std::optional<std::string> codec;
    std::optional<int> quality;
    std::optional<std::string> path;
    /// PixelSource as int (0 = Unknown for legacy rows).
    int source = 0;
  };

  void upsert_level(const LevelRow& row);
  /// Remove one ladder level row (metadata).
  void delete_level(std::string_view content_id, int max_edge, int frame_idx);

  /// Point locator at a new content_id (after hash promotion).
  void update_locator_content_id(std::string_view uri, std::string_view content_id);

  /// Delete a content row (after merge/promotion).
  void delete_content(std::string_view content_id);

  /// Best level with frame_idx and max_edge <= requested (largest such edge).
  [[nodiscard]] std::optional<LevelRow> find_best_level(
      std::string_view content_id, int max_edge, int frame_idx = 0) const;

  /// Smallest stored level with max_edge >= min_edge (for downscale-from-cache).
  [[nodiscard]] std::optional<LevelRow> find_smallest_level_ge(
      std::string_view content_id, int min_edge, int frame_idx = 0) const;

  [[nodiscard]] std::vector<LevelRow> list_levels(std::string_view content_id,
                                                  int limit = 32) const;

  struct ArchiveEntryRow {
    std::string archive_uri;
    std::string member_path;
    std::optional<std::int64_t> uncompressed_size;
  };

  void replace_archive_entries(std::string_view archive_uri,
                               const std::vector<ArchiveEntryRow>& entries);
  [[nodiscard]] std::vector<ArchiveEntryRow> list_archive_entries(
      std::string_view archive_uri, int limit = 10000) const;

  // --- tags (content_id keyed; align with dirtoo sha256 identity) ---
  struct TagRow {
    std::string content_id;
    std::string tag;
    std::optional<std::string> source;
    std::optional<std::int64_t> created_at;
  };

  /// Insert or ignore; empty tag is a no-op.
  void add_tag(std::string_view content_id, std::string_view tag,
               std::string_view source = "user");
  bool remove_tag(std::string_view content_id, std::string_view tag);
  [[nodiscard]] std::vector<std::string> tags_for_content(
      std::string_view content_id) const;
  [[nodiscard]] std::vector<std::string> content_ids_for_tag(
      std::string_view tag, int limit = 1000) const;

  // --- grid tiles (Phase 4) ---
  struct TileRow {
    std::string content_id;
    int scale = 0;
    int x = 0;
    int y = 0;
    std::optional<int> width;
    std::optional<int> height;
    std::optional<std::string> codec;
    std::optional<int> quality;
    int source = 0;  ///< TileSource (0 = full)
  };

  void upsert_tile(const TileRow& row);
  void delete_tile(std::string_view content_id, int scale, int x, int y);
  [[nodiscard]] std::optional<TileRow> find_tile(std::string_view content_id,
                                                 int scale, int x,
                                                 int y) const;
  /// Returns false if no tiles exist for content_id.
  [[nodiscard]] bool tile_min_max_scale(std::string_view content_id,
                                        int& min_scale_out,
                                        int& max_scale_out) const;
  [[nodiscard]] std::vector<TileRow> list_tiles(std::string_view content_id,
                                                int limit = 10000) const;

  // --- maintenance / GC ---
  /// Delete tile *metadata* rows with scale < min_scale_keep. Returns rows removed.
  [[nodiscard]] std::int64_t delete_tiles_below_scale(int min_scale_keep);
  /// Content rows that have no locators (orphans after deletes/renames).
  [[nodiscard]] std::vector<std::string> list_orphan_content_ids(
      int limit = 100000) const;
  /// Locators whose outer_path is set but is not a regular file on disk.
  [[nodiscard]] std::vector<LocatorRow> list_dead_path_locators(
      int limit = 100000) const;
  void delete_locator(std::string_view uri);
  /// Drop content row and related levels/tiles/tags metadata (not blob store).
  void purge_content_metadata(std::string_view content_id);

  // --- text layers (semantic overlay cache) ---
  void upsert_text_layer(std::string_view content_id, int page_1based,
                         std::string_view layout_key,
                         double page_x0, double page_y0, double page_x1, double page_y1,
                         const std::vector<std::uint8_t>& payload);
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> find_text_layer(
      std::string_view content_id, int page_1based,
      std::string_view layout_key) const;
  void delete_text_layers(std::string_view content_id);

  void upsert_document_outline(std::string_view content_id,
                               std::string_view layout_key,
                               const std::vector<std::uint8_t>& payload);
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> find_document_outline(
      std::string_view content_id, std::string_view layout_key) const;

 private:
  explicit Database(sqlite3* db, std::filesystem::path cache_root,
                    std::filesystem::path db_path, int schema_version);

  void exec(const char* sql) const;
  void migrate_or_init();

  mutable std::recursive_mutex mu_;
  sqlite3* db_ = nullptr;
  std::filesystem::path cache_root_;
  std::filesystem::path db_path_;
  int schema_version_ = 0;
};

}  // namespace thumtoo
