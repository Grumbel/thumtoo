// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thumtoo/database.hpp"
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
  using MetaCallback =
      std::function<void(std::string uri, std::optional<ContentMeta>)>;

  Client() = default;
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;
  Client(Client&&) = delete;
  Client& operator=(Client&&) = delete;
  ~Client();

  /// Open cache and start the single writer/worker thread.
  static std::unique_ptr<Client> open(const std::filesystem::path& cache_root,
                                      Executor executor = {});

  [[nodiscard]] Database& db() { return *db_; }
  [[nodiscard]] const Database& db() const { return *db_; }

  /// Cache-only; does not touch source volumes.
  [[nodiscard]] std::optional<Size> get_size(std::string_view uri) const;
  [[nodiscard]] std::optional<ContentMeta> get_meta(std::string_view uri) const;

  /// Ensure a locator row exists and schedule a size probe if missing/pending.
  /// Callback runs through Executor when the worker finishes (or immediately
  /// if already Ready with size).
  void request_size(std::string uri, SizeCallback cb);

  /// Register filesystem paths as file:/// locators (pending) for later probe.
  /// Used by thumtoo-prepare; does not decode yet.
  void prepare_paths(const std::vector<std::filesystem::path>& paths);

  /// Block until the worker queue is empty (CLI / tests).
  void drain();

 private:
  explicit Client(std::unique_ptr<Database> db, Executor executor);

  enum class JobKind { ProbeSize };

  struct Job {
    JobKind kind = JobKind::ProbeSize;
    std::string uri;
    SizeCallback size_cb;
  };

  void worker_main();
  void enqueue(Job job);
  void handle_probe_size(Job& job);

  std::unique_ptr<Database> db_;
  Executor executor_;

  std::mutex mu_;
  std::vector<Job> queue_;
  bool stop_ = false;
  std::thread worker_;
};

}  // namespace thumtoo
