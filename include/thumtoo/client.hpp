// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thumtoo/database.hpp"
#include "thumtoo/archive.hpp"
#include "thumtoo/blob_store.hpp"
#include "thumtoo/executor.hpp"
#include "thumtoo/types.hpp"
#include "thumtoo/text.hpp"
#include "thumtoo/pdf.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <deque>
#include <list>
#include <vector>

namespace thumtoo {

/// Host feature probes (biltoo may #ifdef these when linking older trees).
#ifndef THUMTOO_API_INTEREST_EPOCH
#define THUMTOO_API_INTEREST_EPOCH 1
#endif
#ifndef THUMTOO_API_OVERVIEW_PIXELS
#define THUMTOO_API_OVERVIEW_PIXELS 1
#endif
#ifndef THUMTOO_API_SET_INTEREST
#define THUMTOO_API_SET_INTEREST 1
#endif
#ifndef THUMTOO_API_REQUEST_RASTER
#define THUMTOO_API_REQUEST_RASTER 1
#endif

/// In-process client: cache-only get_* + async request_* (DESIGN API sketch).
///
/// Threading: get_* are non-blocking SQLite reads and are intended for the GUI
/// thread. request_* enqueue work onto a pool of worker threads (default:
/// hardware_concurrency). Completion callbacks are delivered via the Executor
/// installed at open() — never directly from a worker unless the Executor runs
/// inline (CLI default). SQLite is used in WAL mode with a busy timeout so
/// concurrent workers can share the connection.
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

  /// \param worker_threads 0 → std::thread::hardware_concurrency() (min 1, max 32).
  static std::unique_ptr<Client> open(const std::filesystem::path& cache_root,
                                      Executor executor = {},
                                      unsigned worker_threads = 0);

  [[nodiscard]] Database& db() { return *db_; }
  [[nodiscard]] const Database& db() const { return *db_; }

  /// Cache-only; does not touch source volumes.
  [[nodiscard]] std::optional<Size> get_size(std::string_view uri) const;
  [[nodiscard]] std::optional<ContentMeta> get_meta(std::string_view uri) const;

  /// Cache-only: locator rows known to this cache (browse without source I/O).
  [[nodiscard]] std::vector<Database::LocatorRow> list_locators(int limit = 100) const;
  [[nodiscard]] std::optional<Database::LocatorRow> find_locator(
      std::string_view uri) const;

  /// Cache-only listing helpers (see Database::list_locators_*).
  [[nodiscard]] std::vector<Database::LocatorRow> list_locators_by_uri_prefix(
      std::string_view uri_prefix, int limit = 100) const;
  [[nodiscard]] std::vector<Database::LocatorRow> list_locators_by_outer_path_prefix(
      std::string_view path_prefix, int limit = 100) const;
  [[nodiscard]] std::vector<Database::LocatorRow> list_locators_like(
      std::string_view uri_like_pattern, int limit = 100) const;

  /// Resolve a location or content-id URI to the durable content_id (cache only).
  /// Returns nullopt if the locator is unknown or not yet hashed.
  [[nodiscard]] std::optional<std::string> resolve_content_id(
      std::string_view uri) const;

  /// Locators that share this content_id (same bytes, different paths).
  [[nodiscard]] std::vector<Database::LocatorRow> list_uris_for_content_id(
      std::string_view content_id, int limit = 100) const;

  /// Cache-only meta by content_id (same as get_meta("sha256:…")).
  [[nodiscard]] std::optional<ContentMeta> get_meta_for_content_id(
      std::string_view content_id) const;

  /// Load original media bytes for a location or content-id URI.
  /// **Source I/O** (not cache-only): regular files and archive members.
  /// Content-id tries each known locator. PDF pages return nullopt.
  /// http(s) uses libcurl when THUMTOO_HAVE_CURL is enabled.
  /// Rejects payloads larger than kArchiveMaxMemberUncompressedBytes.
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> read_source_bytes(
      std::string_view uri_or_content_id);

  /// Cache-only: best stored **soft** preview with long-edge ≤ max_edge
  /// (frame 0 default). Does not build levels. Soft storage is capped at
  /// kMaxSoftLadderEdge (512); a 256-level is returned for larger max_edge
  /// until a higher soft level has been ensured.
  [[nodiscard]] std::optional<PixelLevel> get_pixels(std::string_view uri,
                                                     int max_edge,
                                                     int frame_idx = 0) const;

  /**
   * Cache-only: build a full-frame preview from stored grid tiles.
   * Picks a pyramid scale whose long edge is ≥ min(max_edge, native), requires
   * every cell at that scale to be present, composites, then shrinks to
   * max_edge. PixelSource::TileSynth. No source I/O and no soft-ladder clamp —
   * suitable for 512–kBatchMaxEdge (and higher) when the pyramid exists.
   */
  [[nodiscard]] std::optional<PixelLevel> get_pixels_from_tiles(
      std::string_view uri, int max_edge) const;

  /// Cache-only: inline LQIP (ThumbHash) on the content row — no blob I/O.
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> get_lqip(
      std::string_view uri) const;

  /// If LQIP missing, try a cheap file/buffer thumbnail encode (worker-safe).
  /// Returns get_lqip afterward. No-op when already present.
  std::optional<std::vector<std::uint8_t>> ensure_lqip(std::string_view uri);

  /// Queue background LQIP fill (worker only). Used after a durable thumbnail
  /// so successive opens get a soft underlay — never blocks tile replies.
  void request_lqip(std::string uri);

  void request_size(std::string uri, SizeCallback cb);

  /// Ensure a soft ladder level exists (probe if needed), then invoke  cb.
  ///
  /// Contract:
  /// -  max_edge is clamped to kMaxSoftLadderEdge (512). Larger values do not
  ///   create full-page 1024/2048 JXL levels — use request_tile or a consumer
  ///   full decode for high resolution.
  /// - If a cached level already **covers** the (clamped) request (~90% of
  ///   long edge in actual pixels), the callback runs immediately.
  /// - A smaller cached level (e.g. 256 when asking for 512) does **not**
  ///   short-circuit; EnsurePixels upgrades the soft ladder.
  /// - Callback may still deliver only the best soft level if encode fails.
  void request_pixels(std::string uri, int max_edge, PixelsCallback cb,
                      int frame_idx = 0);

  /**
   * FastBatch / overview path (PIXEL_PIPELINE Lane A).
   * max_edge is clamped to kBatchMaxEdge (1024). Prefers cache (soft +
   * TileSynth). On miss: shrink-decode source once (JpegShrink), reply with
   * that level; durable soft is still only written ≤ kMaxSoftLadderEdge.
   */
  void request_overview_pixels(std::string uri, int max_edge,
                               PixelsCallback cb);

  /**
   * Cache-only unified raster lookup (PIXEL_PIPELINE).
   * SoftOnly: soft ladder only.
   * PreferCache / Overview: get_pixels (soft + TileSynth fallthrough).
   */
  [[nodiscard]] std::optional<PixelLevel> get_raster(const RasterRequest& req) const;

  /**
   * Unified async raster: SoftOnly → request_pixels; Overview/PreferCache with
   * edge > soft max → request_overview_pixels; else request_pixels.
   */
  void request_raster(RasterRequest req, PixelsCallback cb);

  /// Cache-only grid tile (Phase 4 / Galapix). See TILES.md.
  [[nodiscard]] bool has_tile(std::string_view uri, int scale, int x,
                              int y) const;
  [[nodiscard]] std::optional<TileBlob> get_tile(std::string_view uri, int scale,
                                                 int x, int y) const;

  /// Cache-only coverage: min/max scale present and native size when known.
  [[nodiscard]] std::optional<TileCoverage> get_tile_coverage(
      std::string_view uri) const;

  /// Ensure tile at (scale,x,y) exists; builds [scale..max] in one pass if missing.
  /// Async: always enqueued (never does blob I/O on the caller thread).
  /// Cache hits are resolved on a worker; callback via Executor.
  void request_tile(std::string uri, int scale, int x, int y, TileCallback cb);

  /// One interactive worker job for many cells of the same URI (shared size
  /// probe / shrink ladder). \a cb is invoked once per coordinate.
  struct TileCoord {
    int scale = 0;
    int x = 0;
    int y = 0;
  };
  /// \a on_cell is invoked once per coordinate (index matches \a coords).
  /// Prefer this over a shared TileCallback that must re-match scale/x/y —
  /// missed matches left Galapix JobHandles REQUESTED forever.
  using TileBatchCallback =
      std::function<void(std::size_t index, std::optional<TileBlob> tile)>;
  void request_tiles(std::string uri, std::vector<TileCoord> coords,
                     TileBatchCallback on_cell);

  void invalidate_tile(std::string_view uri, int scale, int x, int y);

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

  /// PDF page count (1-based pages). nullopt if no backend can open the file.
  [[nodiscard]] static std::optional<int> pdf_page_count(
      const std::filesystem::path& path,
      PdfBackend backend = PdfBackend::Default);

  /// Rasterize one page (1-based) to RGB888; empty rgb on failure.
  struct PdfPageRaster {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;
  };
  [[nodiscard]] static std::optional<PdfPageRaster> pdf_rasterize_page(
      const std::filesystem::path& path, int page_1based, int max_edge,
      PdfBackend backend = PdfBackend::Default);

  /// file:///abs.pdf//page:N (or //mupdf-page:N).
  [[nodiscard]] static std::string pdf_page_uri(
      const std::filesystem::path& path, int page_1based,
      PdfBackend backend = PdfBackend::Default);

  [[nodiscard]] static bool is_pdf_path(const std::filesystem::path& path);

  void drain();

  /**
   * Interest epoch for cancel-on-scroll (PIXEL_PIPELINE §6).
   * New request_* jobs stamp the current epoch. bump_interest_epoch()
   * increments the epoch and drops queued jobs stamped with an older value
   * (callbacks get nullopt / empty so hosts can clear inflight state).
   * In-flight workers may still finish; hosts should ignore stale epochs.
   */
  [[nodiscard]] std::uint64_t interest_epoch() const;

  /// Snapshot of worker queue pressure (PIXEL_PIPELINE host metrics).
  struct QueueStats {
    std::size_t pending = 0;
    int inflight = 0;
    int focus_full_inflight = 0;
    std::uint64_t interest_epoch = 0;
  };
  [[nodiscard]] QueueStats queue_stats() const;
  /// Increment epoch and purge stale queued jobs. Returns the new epoch.
  std::uint64_t bump_interest_epoch();
  /// Drop all queued jobs (any epoch); does not touch in-flight work.
  /// Returns how many jobs were removed from the queue.
  std::size_t cancel_pending();
  /// Drop queued jobs whose uri matches (exact). Returns removed count.
  std::size_t cancel_uri(std::string_view uri);

  /**
   * Replace the interest snapshot (PIXEL_PIPELINE §6.1).
   * Bumps the interest epoch (cancels stale queued work), then enqueues
   * background work for the new set:
   * - Near / Speculative: request_overview_pixels at min(edge, kBatchMaxEdge)
   * - Primary: same for now; FocusFull (tiles) is a later phase
   * Callbacks are optional; nullopt is fine for pure scheduling.
   * @return the new interest epoch
   */
  std::uint64_t set_interest(std::vector<InterestItem> items);

  /// Tags attach to content_id (sha256:… preferred). URI resolves via locator.
  [[nodiscard]] std::vector<std::string> get_tags(std::string_view uri) const;

  /// Cache-only text layer (nullopt if not stored).
  [[nodiscard]] std::optional<PageTextLayer> get_page_text_layer(
      std::string_view uri) const;

  /// Extract if missing, always store when content_id is known. Source I/O.
  std::optional<PageTextLayer> ensure_page_text_layer(std::string_view uri);

  /// Cache-only outline.
  [[nodiscard]] std::optional<DocumentOutline> get_document_outline(
      std::string_view uri) const;

  /// Extract + cache outline when content_id known.
  std::optional<DocumentOutline> ensure_document_outline(std::string_view uri);

  /// Returns false if uri has no content_id yet.
  bool add_tag(std::string_view uri, std::string_view tag,
               std::string_view source = "user");
  bool remove_tag(std::string_view uri, std::string_view tag);

 private:
  explicit Client(std::unique_ptr<Database> db,
                  std::unique_ptr<BlobStore> blobs, Executor executor,
                  unsigned worker_threads);

  /// Load/refresh TOC-ordered image members for FastBatch cursor (source I/O on miss).
  void ensure_archive_cursor(const std::filesystem::path& archive_path);

  /// Plan TOC-ordered extract window; advances cursor after successful extract
  /// when advance=true (worker path).
  [[nodiscard]] std::vector<std::string> plan_and_maybe_advance_cursor(
      const std::filesystem::path& archive_path,
      const std::vector<std::string>& interest_members,
      bool advance_after = false);

  enum class JobKind { ProbeSize, EnsurePixels, EnsureTiles, EnsureLqip };


  struct Job {
    JobKind kind = JobKind::ProbeSize;
    std::uint64_t epoch = 0;  // interest epoch at enqueue time
    std::string uri;
    int max_edge = 0;
    int frame_idx = 0;
    /// EnsurePixels: true → FastBatch overview (reply ≤ kBatchMaxEdge).
    bool overview = false;
    int tile_scale = 0;
    int tile_x = 0;
    int tile_y = 0;
    int tile_min_scale = 0;
    int tile_max_scale = -1;  // <0 → until single-tile coverage
    bool tile_pyramid = false;  // true: generate range, no single-tile reply
    /// When true, interactive RGB cells are replied without JPEG/SQLite write
    /// (batch paints the whole view first; durable store can follow later).
    bool skip_durable = false;
    /// Batch already probed size; skip handle_probe_size in children.
    bool skip_probe = false;
    /// Non-empty: interactive multi-cell batch for the same uri.
    std::vector<TileCoord> tile_batch;
    TileBatchCallback tile_batch_cb;
    SizeCallback size_cb;
    PixelsCallback pixels_cb;
    TileCallback tile_cb;
  };

  void worker_main();
  /// Deliver nullopt/empty callbacks for a job removed from the queue.
  void reply_cancelled_job(Job& job);
  /// \param front true → LIFO (interactive tiles); false → FIFO (bulk).
  void enqueue(Job job, bool front = false);
  void handle_probe_size(
      Job& job,
      const std::optional<std::vector<std::uint8_t>>& preextracted = std::nullopt);
  void handle_ensure_pixels(
      Job& job,
      const std::optional<std::vector<std::uint8_t>>& preextracted = std::nullopt);
  void handle_ensure_tiles(
      Job& job,
      const std::optional<std::vector<std::uint8_t>>& preextracted = std::nullopt);
  void handle_ensure_lqip(Job& job);
  void store_tiles(const std::string& content_id,
                   const std::vector<TileBlob>& tiles);
  /// Drop JpegShrink (Q1) soft levels once a durable tile pyramid exists.
  void invalidate_q1_levels(const std::string& content_id);

  static constexpr std::size_t kExtractCacheMaxBytes = 512ull * 1024ull * 1024ull;
  /// Same budget shared conceptually; HTTP bodies use a separate map.
  static constexpr std::size_t kHttpCacheMaxBytes = 512ull * 1024ull * 1024ull;
  [[nodiscard]] static std::string extract_cache_key(
      const std::filesystem::path& archive, std::string_view member);
  void extract_cache_put(const std::filesystem::path& archive,
                         std::string_view member,
                         std::vector<std::uint8_t> bytes);
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> extract_cache_get(
      const std::filesystem::path& archive, std::string_view member) const;
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> member_bytes(
      const std::filesystem::path& archive, std::string_view member,
      const std::optional<std::vector<std::uint8_t>>& preextracted = std::nullopt);

  /// GET with in-process cache (session only; not durable across runs).
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> fetch_http_cached(
      std::string_view url);

  [[nodiscard]] std::optional<PixelLevel> load_level(
      const Database::LevelRow& row) const;

  std::unique_ptr<Database> db_;
  std::unique_ptr<BlobStore> blobs_;
  Executor executor_;

  mutable std::mutex mu_;  // also locked from const interest_epoch()
  std::condition_variable cv_;
  std::deque<Job> queue_;
  bool stop_ = false;
  int inflight_ = 0;
  /// FocusFull tile-pyramid jobs currently running (worker claimed).
  int focus_full_inflight_ = 0;
  std::uint64_t interest_epoch_ = 1;
  std::vector<std::thread> workers_;

  mutable std::mutex archive_cursor_mu_;
  std::unordered_map<std::string, ArchiveCursor> archive_cursors_;

  mutable std::mutex extract_cache_mu_;

  /// LRU: front = most recently used. Values hold bytes + list iterator.
  /// Cache maps are mutable so const get() can touch the LRU order.
  struct ExtractCacheEntry {
    std::vector<std::uint8_t> bytes;
    std::list<std::string>::iterator lru_it;
  };
  mutable std::list<std::string> extract_cache_lru_;
  mutable std::unordered_map<std::string, ExtractCacheEntry> extract_cache_;
  mutable std::size_t extract_cache_bytes_ = 0;

  mutable std::mutex http_cache_mu_;
  std::unordered_map<std::string, std::vector<std::uint8_t>> http_cache_;
  std::size_t http_cache_bytes_ = 0;
};

}  // namespace thumtoo
