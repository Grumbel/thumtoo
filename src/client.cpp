// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/client.hpp"
#include "thumtoo/constants.hpp"
#include "thumtoo/uri.hpp"

#include <chrono>
#include <condition_variable>
#include <random>
#include <sstream>

namespace thumtoo {
namespace {

std::string make_provisional_id() {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<std::uint64_t> dist;
  const auto a = dist(rng);
  const auto b = dist(rng);
  std::ostringstream os;
  os << kContentIdProvisionalPrefix;
  os << std::hex << a << b;
  return os.str();
}

std::optional<std::int64_t> file_size_bytes(const std::filesystem::path& p) {
  std::error_code ec;
  const auto sz = std::filesystem::file_size(p, ec);
  if (ec) return std::nullopt;
  return static_cast<std::int64_t>(sz);
}

std::optional<std::int64_t> file_mtime_ns(const std::filesystem::path& p) {
  std::error_code ec;
  const auto ft = std::filesystem::last_write_time(p, ec);
  if (ec) return std::nullopt;
  // Store file_clock ticks as opaque fingerprint (not wall-clock ns).
  return static_cast<std::int64_t>(ft.time_since_epoch().count());
}

}  // namespace

Client::Client(std::unique_ptr<Database> db, Executor executor)
    : db_(std::move(db)), executor_(std::move(executor)) {
  worker_ = std::thread([this] { worker_main(); });
}

Client::~Client() {
  {
    std::lock_guard lock(mu_);
    stop_ = true;
  }
  // Wake by pushing nothing — worker polls stop_ with short wait via queue push
  // of empty handling: join after setting stop and notifying via dummy job.
  enqueue(Job{});  // kind default ProbeSize with empty uri — ignored when stop_
  if (worker_.joinable()) worker_.join();
}

std::unique_ptr<Client> Client::open(const std::filesystem::path& cache_root,
                                     Executor executor) {
  auto db = std::make_unique<Database>(Database::open(cache_root));
  return std::unique_ptr<Client>(new Client(std::move(db), std::move(executor)));
}

std::optional<Size> Client::get_size(std::string_view uri) const {
  auto m = db_->meta_for_uri(uri);
  if (!m || !m->size) return std::nullopt;
  if (m->status == ContentStatus::Failed ||
      m->status == ContentStatus::Unsupported) {
    return std::nullopt;
  }
  return m->size;
}

std::optional<ContentMeta> Client::get_meta(std::string_view uri) const {
  return db_->meta_for_uri(uri);
}

void Client::enqueue(Job job) {
  std::lock_guard lock(mu_);
  queue_.push_back(std::move(job));
}

void Client::request_size(std::string uri, SizeCallback cb) {
  if (auto m = get_meta(uri)) {
    if (m->size && (m->status == ContentStatus::Ready ||
                    m->status == ContentStatus::Incomplete)) {
      if (cb) {
        auto size = m->size;
        executor_.post([cb = std::move(cb), uri, size]() mutable {
          cb(std::move(uri), size);
        });
      }
      return;
    }
  }

  // Ensure locator exists (provisional content) so cache-first browse sees it.
  if (!db_->find_locator(uri)) {
    Database::LocatorRow loc;
    loc.uri = uri;
    loc.content_id = make_provisional_id();
    if (auto path = path_from_file_uri(uri)) {
      loc.outer_path = path->string();
      loc.size = file_size_bytes(*path);
      loc.mtime_ns = file_mtime_ns(*path);
    }
    Database::ContentRow content;
    content.content_id = *loc.content_id;
    content.status = ContentStatus::Pending;
    db_->upsert_content(content);
    db_->upsert_locator(loc);
  }

  Job job;
  job.kind = JobKind::ProbeSize;
  job.uri = std::move(uri);
  job.size_cb = std::move(cb);
  enqueue(std::move(job));
}

void Client::prepare_paths(const std::vector<std::filesystem::path>& paths) {
  for (const auto& p : paths) {
    std::error_code ec;
    auto abs = std::filesystem::absolute(p, ec);
    if (ec) continue;
    const auto uri = file_uri_from_path(abs);
    if (db_->find_locator(uri)) continue;
    Database::LocatorRow loc;
    loc.uri = uri;
    loc.content_id = make_provisional_id();
    loc.outer_path = abs.string();
    loc.size = file_size_bytes(abs);
    loc.mtime_ns = file_mtime_ns(abs);
    Database::ContentRow content;
    content.content_id = *loc.content_id;
    content.status = ContentStatus::Pending;
    db_->upsert_content(content);
    db_->upsert_locator(loc);
    request_size(uri, {});
  }
}

void Client::drain() {
  for (;;) {
    {
      std::lock_guard lock(mu_);
      if (queue_.empty()) return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

void Client::worker_main() {
  for (;;) {
    Job job;
    {
      std::unique_lock lock(mu_);
      if (stop_ && queue_.empty()) return;
      if (queue_.empty()) {
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        continue;
      }
      job = std::move(queue_.front());
      queue_.erase(queue_.begin());
      if (stop_ && job.uri.empty()) return;
    }
    if (job.uri.empty()) continue;
    if (job.kind == JobKind::ProbeSize) handle_probe_size(job);
  }
}

void Client::handle_probe_size(Job& job) {
  // Phase 1 spike: no image codec yet. Mark plain files Incomplete with
  // error_code=probe_not_implemented so the pipeline is exercisable; real
  // dimension probe lands with the encode ladder.
  auto loc = db_->find_locator(job.uri);
  if (!loc || !loc->content_id) {
    if (job.size_cb) {
      auto cb = std::move(job.size_cb);
      auto uri = job.uri;
      executor_.post([cb = std::move(cb), uri = std::move(uri)]() mutable {
        cb(std::move(uri), std::nullopt);
      });
    }
    return;
  }

  Database::ContentRow row;
  row.content_id = *loc->content_id;
  if (auto existing = db_->find_content(row.content_id)) row = *existing;

  if (is_archive_uri(job.uri)) {
    row.status = ContentStatus::Unsupported;
    row.error_code = "archive_not_implemented";
  } else if (auto path = path_from_file_uri(job.uri)) {
    if (!std::filesystem::is_regular_file(*path)) {
      row.status = ContentStatus::Failed;
      row.error_code = "not_a_file";
    } else {
      row.status = ContentStatus::Incomplete;
      row.error_code = "probe_not_implemented";
      // Keep any prior width/height if present.
    }
  } else {
    row.status = ContentStatus::Unsupported;
    row.error_code = "uri_scheme_unsupported";
  }
  db_->upsert_content(row);

  std::optional<Size> size;
  if (row.width && row.height) size = Size{*row.width, *row.height};

  if (job.size_cb) {
    auto cb = std::move(job.size_cb);
    auto uri = job.uri;
    executor_.post([cb = std::move(cb), uri = std::move(uri), size]() mutable {
      cb(std::move(uri), size);
    });
  }
}

}  // namespace thumtoo
