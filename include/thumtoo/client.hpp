// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thumtoo/database.hpp"
#include "thumtoo/blob_store.hpp"
#include "thumtoo/executor.hpp"
#include "thumtoo/types.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace thumtoo {

/// In-process client: cache-only get_* + async request_* (DESIGN API sketch).
///
/// Threading: get_* are non-blocking SQLite reads and are intended for the GUI
/// thread. request_* enqueue work; completion callbacks are delivered via the
/// Executor installed at open() — never directly from the worker thread unless
/// the Executor runs inline (CLI default).
class Client {
 public:
  using SizeCallback = std::function<void(std::string uri, std::optional<Size>)>;
  using PixelsCallback =
      std::function<void(std::string uri, int max_edge, std::optional<PixelLevel>)>;
  using TileCallback = std::function<void(std::string uri, int scale, int x, int y,
                                          std::optional<TileBlob>)>;

  Client() = default;
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;
  Client(Client&&) = delete;
  Client& operator=(Client&&) = delete;
  ~Client();

  static std::unique_ptr<Client> open(const std::filesystem::path& cache_root,
                                      Executor executor = {});

  [[nodiscard]] Database& db() { return *db_; }
  [[nodiscard]] const Database& db() const { return *db_; }

  /// Cache-only; does not touch source volumes.
  [[nodiscard]] std::optional<Size> get_size(std::string_view uri) const;
  [[nodiscard]] std::optional<ContentMeta> get_meta(std::string_view uri) const;

  /// Cache-only: load best ladder level with edge <= max_edge (frame 0 default).
  [[nodiscard]] std::optional<PixelLevel> get_pixels(std::string_view uri,
                                                     int max_edge,
                                                     int frame_idx = 0) const;

  void request_size(std::string uri, SizeCallback cb);

  /// Ensure ladder exists (probe if needed), then return pixels via callback.
  void request_pixels(std::string uri, int max_edge, PixelsCallback cb,
                      int frame_idx = 0);

  /// Cache-only grid tile (Phase 4 / Galapix). See TILES.md.
  [[nodiscard]] bool has_tile(std::string_view uri, int scale, int x,
                              int y) const;
  [[nodiscard]] std::optional<TileBlob> get_tile(std::string_view uri, int scale,
                                                 int x, int y) const;

  /// Cache-only coverage: min/max scale present and native size when known.
  [[nodiscard]] std::optional<TileCoverage> get_tile_coverage(
      std::string_view uri) const;

  /// Ensure tile at (scale,x,y) exists; builds [scale..max] in one pass if missing.
  void request_tile(std::string uri, int scale, int x, int y, TileCallback cb);

  /// Prewarm pyramid [min_scale..max_scale] (max_scale < 0 → until single tile).
  void request_tile_pyramid(std::string uri, int min_scale = 0,
                            int max_scale = -1, TileCallback on_done = {});

  /// Register paths, schedule size probes. Returns how many probe jobs were
  /// enqueued (already-ready locators are skipped). Optional callback is
  /// invoked once per completed probe (same path as request_size).
  ///
  /// Archive paths (zip/cbz/rar/…) are expanded: TOC is refreshed, image
  /// members are registered as `file://…//archive:member` locators and probed
  /// up to kArchiveMaxPrepareTotalUncompressedBytes per archive.
  size_t prepare_paths(const std::vector<std::filesystem::path>& paths,
                       SizeCallback on_each = {});

  /// Cache-only TOC if present.
  [[nodiscard]] std::vector<Database::ArchiveEntryRow> get_archive_entries(
      std::string_view archive_uri) const;

  /// Read TOC from source (libarchive), store under archive_uri, return entries.
  std::vector<Database::ArchiveEntryRow> refresh_archive_toc(
      const std::filesystem::path& archive_path);

  /// PDF page count (1-based pages). nullopt if Poppler missing or open fails.
  [[nodiscard]] static std::optional<int> pdf_page_count(
      const std::filesystem::path& path);

  /// Rasterize one page (1-based) to RGB888; empty rgb on failure.
  struct PdfPageRaster {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;
  };
  [[nodiscard]] static std::optional<PdfPageRaster> pdf_rasterize_page(
      const std::filesystem::path& path, int page_1based, int max_edge);

  /// file:///abs.pdf//page:N (1-based).
  [[nodiscard]] static std::string pdf_page_uri(
      const std::filesystem::path& path, int page_1based);

  [[nodiscard]] static bool is_pdf_path(const std::filesystem::path& path);

  void drain();

  /// Tags attach to content_id (sha256:… preferred). URI resolves via locator.
  [[nodiscard]] std::vector<std::string> get_tags(std::string_view uri) const;
  /// Returns false if uri has no content_id yet.
  bool add_tag(std::string_view uri, std::string_view tag,
               std::string_view source = "user");
  bool remove_tag(std::string_view uri, std::string_view tag);

 private:
  explicit Client(std::unique_ptr<Database> db,
                  std::unique_ptr<BlobStore> blobs, Executor executor);

  enum class JobKind { ProbeSize, EnsurePixels, EnsureTiles };

  struct Job {
    JobKind kind = JobKind::ProbeSize;
    std::string uri;
    int max_edge = 0;
    int frame_idx = 0;
    int tile_scale = 0;
    int tile_x = 0;
    int tile_y = 0;
    int tile_min_scale = 0;
    int tile_max_scale = -1;  // <0 → until single-tile coverage
    bool tile_pyramid = false;  // true: generate range, no single-tile reply
    SizeCallback size_cb;
    PixelsCallback pixels_cb;
    TileCallback tile_cb;
  };

  void worker_main();
  void enqueue(Job job);
  void handle_probe_size(
      Job& job,
      const std::optional<std::vector<std::uint8_t>>& preextracted = std::nullopt);
  void handle_ensure_pixels(Job& job);
  void handle_ensure_tiles(Job& job);
  void store_tiles(const std::string& content_id,
                   const std::vector<TileBlob>& tiles);

  [[nodiscard]] std::optional<PixelLevel> load_level(
      const Database::LevelRow& row) const;

  std::unique_ptr<Database> db_;
  std::unique_ptr<BlobStore> blobs_;
  Executor executor_;

  std::mutex mu_;
  std::vector<Job> queue_;
  bool stop_ = false;
  int inflight_ = 0;
  std::thread worker_;
};

}  // namespace thumtoo
