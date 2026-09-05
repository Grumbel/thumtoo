// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/client.hpp"
#include "thumtoo/constants.hpp"
#include "thumtoo/uri.hpp"
#include "thumtoo/image.hpp"
#include "thumtoo/archive.hpp"
#include "thumtoo/blob_store.hpp"
#include "thumtoo/constants.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <random>
#include <fstream>
#include <sstream>
#include <string_view>

namespace thumtoo {
namespace {

std::string format_from_member(std::string_view member) {
  const auto slash = member.find_last_of("/\\");
  const auto name =
      slash == std::string_view::npos ? member : member.substr(slash + 1);
  const auto dot = name.find_last_of('.');
  if (dot == std::string_view::npos) return "unknown";
  std::string ext(name.substr(dot));
  for (char& ch : ext)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (ext == ".jpg" || ext == ".jpeg") return "jpeg";
  if (ext == ".png") return "png";
  if (ext == ".gif") return "gif";
  if (ext == ".bmp") return "bmp";
  if (ext == ".webp") return "webp";
  if (ext == ".jxl") return "jxl";
  if (ext == ".tif" || ext == ".tiff") return "tiff";
  return "unknown";
}


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

Client::Client(std::unique_ptr<Database> db, std::unique_ptr<BlobStore> blobs,
               Executor executor)
    : db_(std::move(db)), blobs_(std::move(blobs)), executor_(std::move(executor)) {
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
  auto blobs = std::make_unique<BlobStore>(BlobStore::open(cache_root));
  return std::unique_ptr<Client>(
      new Client(std::move(db), std::move(blobs), std::move(executor)));
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


std::optional<PixelLevel> Client::load_level(
    const Database::LevelRow& row) const {
  auto data = blobs_->get_level(row.content_id, row.max_edge, row.frame_idx);
  if (!data || data->empty()) return std::nullopt;
  PixelLevel out;
  out.max_edge = row.max_edge;
  out.frame_idx = row.frame_idx;
  if (row.width) out.width = *row.width;
  if (row.height) out.height = *row.height;
  if (row.codec) out.codec = *row.codec;
  out.bytes = std::move(*data);
  return out;
}

std::optional<PixelLevel> Client::get_pixels(std::string_view uri, int max_edge,
                                             int frame_idx) const {
  auto meta = db_->meta_for_uri(uri);
  if (!meta) return std::nullopt;
  auto row = db_->find_best_level(meta->content_id, max_edge, frame_idx);
  if (!row) return std::nullopt;
  return load_level(*row);
}

void Client::request_pixels(std::string uri, int max_edge, PixelsCallback cb,
                            int frame_idx) {
  if (auto px = get_pixels(uri, max_edge, frame_idx)) {
    if (cb) {
      executor_.post([cb = std::move(cb), uri, max_edge, px = std::move(*px)]() mutable {
        cb(std::move(uri), max_edge, std::move(px));
      });
    }
    return;
  }
  Job job;
  job.kind = JobKind::EnsurePixels;
  job.uri = std::move(uri);
  job.max_edge = max_edge;
  job.frame_idx = frame_idx;
  job.pixels_cb = std::move(cb);
  enqueue(std::move(job));
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

size_t Client::prepare_paths(const std::vector<std::filesystem::path>& paths,
                             SizeCallback on_each) {
  // Collect URIs that need a probe first so callers know the job total before
  // any completion callbacks fire (worker may run concurrently).
  struct Pending {
    std::string uri;
    bool need_register = false;
    Database::LocatorRow loc;
    Database::ContentRow content;
  };
  std::vector<Pending> pending;
  pending.reserve(paths.size());

  auto enqueue_plain = [&](const std::filesystem::path& abs) {
    const auto uri = file_uri_from_path(abs);
    if (auto existing = db_->find_locator(uri)) {
      if (auto meta = db_->meta_for_uri(uri)) {
        if (meta->status == ContentStatus::Ready && meta->size) return;
      }
      Pending item;
      item.uri = uri;
      pending.push_back(std::move(item));
      return;
    }
    Pending item;
    item.uri = uri;
    item.need_register = true;
    item.loc.uri = uri;
    item.loc.content_id = make_provisional_id();
    item.loc.outer_path = abs.string();
    item.loc.size = file_size_bytes(abs);
    item.loc.mtime_ns = file_mtime_ns(abs);
    item.content.content_id = *item.loc.content_id;
    item.content.status = ContentStatus::Pending;
    pending.push_back(std::move(item));
  };

  auto enqueue_archive_member = [&](const std::filesystem::path& abs,
                                    const Database::ArchiveEntryRow& entry) {
    const auto uri = archive_uri(abs, entry.member_path);
    if (auto existing = db_->find_locator(uri)) {
      if (auto meta = db_->meta_for_uri(uri)) {
        if (meta->status == ContentStatus::Ready && meta->size) return;
      }
      Pending item;
      item.uri = uri;
      pending.push_back(std::move(item));
      return;
    }
    Pending item;
    item.uri = uri;
    item.need_register = true;
    item.loc.uri = uri;
    item.loc.content_id = make_provisional_id();
    item.loc.outer_path = abs.string();
    item.loc.member_path = entry.member_path;
    item.loc.size = entry.uncompressed_size;
    item.content.content_id = *item.loc.content_id;
    item.content.status = ContentStatus::Pending;
    pending.push_back(std::move(item));
  };

  for (const auto& p : paths) {
    std::error_code ec;
    auto abs = std::filesystem::absolute(p, ec);
    if (ec) continue;
    if (!std::filesystem::is_regular_file(abs, ec) || ec) continue;

    if (is_likely_archive_path(abs)) {
      auto entries = refresh_archive_toc(abs);
      std::uint64_t budget = kArchiveMaxPrepareTotalUncompressedBytes;
      for (const auto& entry : entries) {
        if (!is_likely_image_member_path(entry.member_path)) continue;
        if (entry.uncompressed_size) {
          const auto sz =
              static_cast<std::uint64_t>(*entry.uncompressed_size);
          if (sz > budget) continue;  // skip oversized / over-budget member
          budget -= sz;
        }
        enqueue_archive_member(abs, entry);
      }
      continue;
    }

    enqueue_plain(abs);
  }

  for (auto& item : pending) {
    if (item.need_register) {
      db_->upsert_content(item.content);
      db_->upsert_locator(item.loc);
    }
    request_size(item.uri, on_each);
  }
  return pending.size();
}


std::vector<Database::ArchiveEntryRow> Client::get_archive_entries(
    std::string_view archive_uri) const {
  return db_->list_archive_entries(archive_uri);
}

std::vector<Database::ArchiveEntryRow> Client::refresh_archive_toc(
    const std::filesystem::path& archive_path) {
  auto toc = read_archive_toc(archive_path);
  if (!toc) return {};
  const auto uri = archive_uri(archive_path);
  std::vector<Database::ArchiveEntryRow> rows;
  rows.reserve(toc->size());
  for (const auto& m : *toc) {
    Database::ArchiveEntryRow r;
    r.archive_uri = uri;
    r.member_path = m.member_path;
    r.uncompressed_size = m.uncompressed_size;
    rows.push_back(std::move(r));
  }
  db_->replace_archive_entries(uri, rows);
  return rows;
}

void Client::drain() {
  for (;;) {
    {
      std::lock_guard lock(mu_);
      if (queue_.empty() && inflight_ == 0) return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

void Client::worker_main() {
  for (;;) {
    std::vector<Job> batch;
    Job single;
    bool use_batch = false;
    {
      std::unique_lock lock(mu_);
      if (stop_ && queue_.empty()) return;
      if (queue_.empty()) {
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        continue;
      }
      // Claim the job under the lock before removing it so drain() cannot
      // observe (empty queue && inflight==0) while work is still about to run.
      if (!queue_.front().uri.empty()) ++inflight_;
      single = std::move(queue_.front());
      queue_.erase(queue_.begin());
      if (stop_ && single.uri.empty()) return;

      // Coalesce further ProbeSize jobs for the same archive into one open.
      if (single.kind == JobKind::ProbeSize && !single.uri.empty()) {
        if (auto arch = parse_archive_uri(single.uri);
            arch && !arch->member_path.empty()) {
          batch.push_back(std::move(single));
          for (auto it = queue_.begin(); it != queue_.end();) {
            if (it->kind != JobKind::ProbeSize || it->uri.empty()) {
              ++it;
              continue;
            }
            auto other = parse_archive_uri(it->uri);
            if (!other || other->member_path.empty() ||
                other->archive_path != arch->archive_path) {
              ++it;
              continue;
            }
            ++inflight_;
            batch.push_back(std::move(*it));
            it = queue_.erase(it);
          }
          use_batch = true;
        }
      }
    }

    if (use_batch) {
      std::vector<std::string> members;
      members.reserve(batch.size());
      std::filesystem::path archive_path;
      for (const auto& j : batch) {
        auto arch = parse_archive_uri(j.uri);
        if (!arch) {
          members.emplace_back();
          continue;
        }
        archive_path = arch->archive_path;
        members.push_back(arch->member_path);
      }
      auto extracted =
          extract_archive_members(archive_path, members);
      for (size_t i = 0; i < batch.size(); ++i) {
        try {
          std::optional<std::vector<std::uint8_t>> pre;
          if (i < members.size() && !members[i].empty()) {
            if (auto it = extracted.find(members[i]); it != extracted.end()) {
              pre = std::move(it->second);
            }
          }
          handle_probe_size(batch[i], pre);
        } catch (...) {
        }
        {
          std::lock_guard lock(mu_);
          --inflight_;
        }
      }
      continue;
    }

    if (single.uri.empty()) continue;
    try {
      if (single.kind == JobKind::ProbeSize) handle_probe_size(single);
      else if (single.kind == JobKind::EnsurePixels) handle_ensure_pixels(single);
    } catch (...) {
      // Always release inflight_; status stays pending/failed for retry.
    }
    {
      std::lock_guard lock(mu_);
      --inflight_;
    }
  }
}

void Client::handle_probe_size(
    Job& job,
    const std::optional<std::vector<std::uint8_t>>& preextracted) {
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

  std::string old_id = *loc->content_id;
  Database::ContentRow row;
  if (auto existing = db_->find_content(old_id))
    row = *existing;
  else {
    row.content_id = old_id;
    row.status = ContentStatus::Pending;
  }

  std::optional<Size> size_out;

  if (auto arch = parse_archive_uri(job.uri)) {
    if (arch->member_path.empty()) {
      row.status = ContentStatus::Unsupported;
      row.error_code = "archive_root_not_image";
    } else {
      std::optional<std::vector<std::uint8_t>> bytes = preextracted;
      if (!bytes) {
        bytes = extract_archive_member(arch->archive_path, arch->member_path);
      }
      if (!bytes) {
        row.status = ContentStatus::Failed;
        row.error_code = "archive_extract_failed";
      } else {
        const auto hex = sha256_bytes_hex(bytes->data(), bytes->size());
        std::string new_id = old_id;
        if (!hex.empty()) new_id = std::string(kContentIdSha256Prefix) + hex;

        if (new_id != old_id) {
          if (auto existing = db_->find_content(new_id)) {
            row = *existing;
            db_->update_locator_content_id(job.uri, new_id);
            if (old_id.rfind(std::string(kContentIdProvisionalPrefix), 0) == 0) {
              db_->delete_content(old_id);
            }
          } else {
            row.content_id = new_id;
            db_->upsert_content(row);
            db_->update_locator_content_id(job.uri, new_id);
            if (old_id.rfind(std::string(kContentIdProvisionalPrefix), 0) == 0) {
              db_->delete_content(old_id);
            }
          }
        }

        auto probe = probe_image_buffer(bytes->data(), bytes->size(),
                                        format_from_member(arch->member_path));
        if (!probe) {
          row.status = ContentStatus::Unsupported;
          row.error_code = "unrecognized_image";
        } else {
          row.width = probe->size.width;
          row.height = probe->size.height;
          row.format = probe->format;
          row.error_code = std::nullopt;
          size_out = probe->size;
          auto levels = build_ladder_buffer(bytes->data(), bytes->size(),
                                            row.content_id, kDefaultJxlQuality);
          for (const auto& lvl : levels) {
            blobs_->put_level(row.content_id, lvl.max_edge, lvl.frame_idx,
                             lvl.width, lvl.height, lvl.codec, lvl.quality,
                             lvl.bytes.data(), lvl.bytes.size());
            Database::LevelRow lr;
            lr.content_id = row.content_id;
            lr.max_edge = lvl.max_edge;
            lr.frame_idx = lvl.frame_idx;
            lr.width = lvl.width;
            lr.height = lvl.height;
            lr.codec = lvl.codec;
            lr.quality = lvl.quality;
            lr.path = "blobs.sqlite";
            db_->upsert_level(lr);
          }
          row.status =
              levels.empty() ? ContentStatus::Incomplete : ContentStatus::Ready;
          if (levels.empty()) row.error_code = "ladder_encode_failed";
        }
      }
    }
  } else if (auto path = path_from_file_uri(job.uri)) {
    if (!std::filesystem::is_regular_file(*path)) {
      row.status = ContentStatus::Failed;
      row.error_code = "not_a_file";
    } else {
      auto probe = probe_image_file(*path);
      if (!probe) {
        row.status = ContentStatus::Unsupported;
        row.error_code = "unrecognized_image";
      } else {
        // Promote provisional id to sha256 when possible.
        std::string new_id = old_id;
        const auto hex = sha256_file_hex(*path);
        if (!hex.empty()) {
          new_id = std::string(kContentIdSha256Prefix) + hex;
        }

        if (new_id != old_id) {
          if (auto existing = db_->find_content(new_id)) {
            // Merge into existing content row; drop provisional.
            row = *existing;
            db_->update_locator_content_id(job.uri, new_id);
            if (old_id.rfind(std::string(kContentIdProvisionalPrefix), 0) == 0) {
              db_->delete_content(old_id);
            }
          } else {
            row.content_id = new_id;
            db_->upsert_content(row);  // insert under new id first
            db_->update_locator_content_id(job.uri, new_id);
            if (old_id.rfind(std::string(kContentIdProvisionalPrefix), 0) == 0) {
              db_->delete_content(old_id);
            }
          }
        }

        row.width = probe->size.width;
        row.height = probe->size.height;
        row.format = probe->format;
        row.error_code = std::nullopt;
        size_out = probe->size;

        auto levels =
            build_ladder(*path, row.content_id, kDefaultJxlQuality);
        for (const auto& lvl : levels) {
          blobs_->put_level(row.content_id, lvl.max_edge, lvl.frame_idx,
                             lvl.width, lvl.height, lvl.codec, lvl.quality,
                             lvl.bytes.data(), lvl.bytes.size());
          Database::LevelRow lr;
          lr.content_id = row.content_id;
          lr.max_edge = lvl.max_edge;
          lr.frame_idx = lvl.frame_idx;
          lr.width = lvl.width;
          lr.height = lvl.height;
          lr.codec = lvl.codec;
          lr.quality = lvl.quality;
          // Payload lives in blobs.sqlite; path kept only as a locator hint.
          lr.path = "blobs.sqlite";
          db_->upsert_level(lr);
        }
        row.status =
            levels.empty() ? ContentStatus::Incomplete : ContentStatus::Ready;
        if (levels.empty()) row.error_code = "ladder_encode_failed";
      }
    }
  } else {
    row.status = ContentStatus::Unsupported;
    row.error_code = "uri_scheme_unsupported";
  }

  db_->upsert_content(row);

  if (job.size_cb) {
    auto cb = std::move(job.size_cb);
    auto uri = job.uri;
    executor_.post([cb = std::move(cb), uri = std::move(uri), size_out]() mutable {
      cb(std::move(uri), size_out);
    });
  }
}



void Client::handle_ensure_pixels(Job& job) {
  // Populate size + ladder via the probe path, then load pixels from cache.
  Job probe;
  probe.kind = JobKind::ProbeSize;
  probe.uri = job.uri;
  handle_probe_size(probe);

  auto px = get_pixels(job.uri, job.max_edge, job.frame_idx);
  if (job.pixels_cb) {
    auto cb = std::move(job.pixels_cb);
    auto uri = job.uri;
    const int edge = job.max_edge;
    executor_.post([cb = std::move(cb), uri = std::move(uri), edge,
                    px = std::move(px)]() mutable {
      cb(std::move(uri), edge, std::move(px));
    });
  }
}


std::vector<std::string> Client::get_tags(std::string_view uri) const {
  auto loc = db_->find_locator(uri);
  if (!loc || !loc->content_id) return {};
  return db_->tags_for_content(*loc->content_id);
}

bool Client::add_tag(std::string_view uri, std::string_view tag,
                     std::string_view source) {
  auto loc = db_->find_locator(uri);
  if (!loc || !loc->content_id) return false;
  db_->add_tag(*loc->content_id, tag, source);
  return true;
}

bool Client::remove_tag(std::string_view uri, std::string_view tag) {
  auto loc = db_->find_locator(uri);
  if (!loc || !loc->content_id) return false;
  return db_->remove_tag(*loc->content_id, tag);
}

}  // namespace thumtoo
