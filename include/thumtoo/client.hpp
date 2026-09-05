// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thumtoo/database.hpp"
#include "thumtoo/blob_store.hpp"
#include "thumtoo/executor.hpp"
#include "thumtoo/types.hpp"

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

  /// Register paths, schedule size probes. Returns how many probe jobs were
  /// enqueued (already-ready locators are skipped). Optional callback is
  /// invoked once per completed probe (same path as request_size).
  size_t prepare_paths(const std::vector<std::filesystem::path>& paths,
                       SizeCallback on_each = {});

  /// Cache-only TOC if present.
  [[nodiscard]] std::vector<Database::ArchiveEntryRow> get_archive_entries(
      std::string_view archive_uri) const;

  /// Read TOC from source (libarchive), store under archive_uri, return entries.
  std::vector<Database::ArchiveEntryRow> refresh_archive_toc(
      const std::filesystem::path& archive_path);

  void drain();

 private:
  explicit Client(std::unique_ptr<Database> db,
                  std::unique_ptr<BlobStore> blobs, Executor executor);

  enum class JobKind { ProbeSize, EnsurePixels };

  struct Job {
    JobKind kind = JobKind::ProbeSize;
    std::string uri;
    int max_edge = 0;
    int frame_idx = 0;
    SizeCallback size_cb;
    PixelsCallback pixels_cb;
  };

  void worker_main();
  void enqueue(Job job);
  void handle_probe_size(Job& job);
  void handle_ensure_pixels(Job& job);

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
