// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/client.hpp"
#include "thumtoo/build_stats.hpp"
#include "thumtoo/constants.hpp"
#include "thumtoo/uri.hpp"
#include "thumtoo/network.hpp"
#include "thumtoo/image.hpp"
#include "thumtoo/archive.hpp"
#include "thumtoo/pdf.hpp"
#include "thumtoo/blob_store.hpp"

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
               Executor executor, unsigned worker_threads)
    : db_(std::move(db)), blobs_(std::move(blobs)), executor_(std::move(executor)) {
  unsigned n = worker_threads;
  if (n == 0) {
    n = std::thread::hardware_concurrency();
  }
  if (n == 0) n = 1;
  if (n > 32) n = 32;
  workers_.reserve(n);
  for (unsigned i = 0; i < n; ++i) {
    workers_.emplace_back([this] { worker_main(); });
  }
}

Client::~Client() {
  {
    std::lock_guard lock(mu_);
    stop_ = true;
    // Drop queued work so shutdown does not re-encode a backlog of previews.
    queue_.clear();
  }
  cv_.notify_all();
  for (auto& w : workers_) {
    if (w.joinable()) w.join();
  }
}

std::unique_ptr<Client> Client::open(const std::filesystem::path& cache_root,
                                     Executor executor, unsigned worker_threads) {
  auto db = std::make_unique<Database>(Database::open(cache_root));
  auto blobs = std::make_unique<BlobStore>(BlobStore::open(cache_root));
  return std::unique_ptr<Client>(
      new Client(std::move(db), std::move(blobs), std::move(executor),
                 worker_threads));
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

std::vector<Database::LocatorRow> Client::list_locators(int limit) const {
  return db_->list_locators(limit);
}

std::optional<Database::LocatorRow> Client::find_locator(std::string_view uri) const {
  return db_->find_locator(uri);
}

std::vector<Database::LocatorRow> Client::list_locators_by_uri_prefix(
    std::string_view uri_prefix, int limit) const {
  return db_->list_locators_by_uri_prefix(uri_prefix, limit);
}

std::vector<Database::LocatorRow> Client::list_locators_by_outer_path_prefix(
    std::string_view path_prefix, int limit) const {
  return db_->list_locators_by_outer_path_prefix(path_prefix, limit);
}

std::vector<Database::LocatorRow> Client::list_locators_like(
    std::string_view uri_like_pattern, int limit) const {
  return db_->list_locators_like(uri_like_pattern, limit);
}

std::optional<std::string> Client::resolve_content_id(std::string_view uri) const {
  if (is_content_id_uri(uri)) {
    return std::string(uri);
  }
  auto loc = db_->find_locator(uri);
  if (!loc || !loc->content_id) return std::nullopt;
  return *loc->content_id;
}

std::vector<Database::LocatorRow> Client::list_uris_for_content_id(
    std::string_view content_id, int limit) const {
  return db_->list_locators_for_content_id(content_id, limit);
}

std::optional<ContentMeta> Client::get_meta_for_content_id(
    std::string_view content_id) const {
  return db_->meta_for_content_id(content_id);
}


std::optional<std::vector<std::uint8_t>> Client::read_source_bytes(
    std::string_view uri_or_content_id) {
  if (is_http_uri(uri_or_content_id)) {
    return fetch_http_cached(uri_or_content_id);
  }
  if (is_content_id_uri(uri_or_content_id)) {
    const auto locs = list_uris_for_content_id(uri_or_content_id);
    for (const auto& loc : locs) {
      if (auto bytes = read_source_bytes(loc.uri)) {
        return bytes;
      }
    }
    return std::nullopt;
  }
  if (parse_pdf_uri(uri_or_content_id)) {
    // Pages are display rasters, not a single source blob here.
    return std::nullopt;
  }
  if (auto arch = parse_archive_uri(uri_or_content_id)) {
    if (arch->member_path.empty()) return std::nullopt;
    return member_bytes(arch->archive_path, arch->member_path, std::nullopt);
  }
  if (auto path = path_from_file_uri(uri_or_content_id)) {
    return read_file_bytes(*path, kArchiveMaxMemberUncompressedBytes);
  }
  return std::nullopt;
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



bool Client::has_tile(std::string_view uri, int scale, int x, int y) const {
  auto meta = db_->meta_for_uri(uri);
  if (!meta) return false;
  return db_->find_tile(meta->content_id, scale, x, y).has_value();
}

std::optional<TileBlob> Client::get_tile(std::string_view uri, int scale, int x,
                                         int y) const {
  auto meta = db_->meta_for_uri(uri);
  if (!meta) return std::nullopt;
  auto row = db_->find_tile(meta->content_id, scale, x, y);
  if (!row) return std::nullopt;
  auto bytes = blobs_->get_tile(meta->content_id, scale, x, y);
  if (!bytes) return std::nullopt;
  TileBlob t;
  t.scale = scale;
  t.x = x;
  t.y = y;
  if (row->width) t.width = *row->width;
  if (row->height) t.height = *row->height;
  if (row->codec) t.codec = *row->codec;
  else t.codec = kDefaultTileCodec;
  t.bytes = std::move(*bytes);
  return t;
}

std::optional<TileCoverage> Client::get_tile_coverage(
    std::string_view uri) const {
  auto meta = db_->meta_for_uri(uri);
  if (!meta) return std::nullopt;
  TileCoverage cov;
  if (meta->size) cov.size = *meta->size;
  int min_s = 0;
  int max_s = 0;
  if (db_->tile_min_max_scale(meta->content_id, min_s, max_s)) {
    cov.min_scale = min_s;
    cov.max_scale = max_s;
    return cov;
  }
  // No tiles stored yet: theoretical coverage from native size alone.
  if (!meta->size || meta->size->width <= 0 || meta->size->height <= 0) {
    return std::nullopt;
  }
  cov.min_scale = 0;
  int w = meta->size->width;
  int h = meta->size->height;
  int s = 0;
  while (w > kTileSize || h > kTileSize) {
    w = (w + 1) / 2;
    h = (h + 1) / 2;
    ++s;
  }
  cov.max_scale = s;
  return cov;
}

void Client::request_tile(std::string uri, int scale, int x, int y,
                          TileCallback cb) {
  if (auto t = get_tile(uri, scale, x, y)) {
    if (cb) {
      executor_.post([cb = std::move(cb), uri, scale, x, y,
                      t = std::move(*t)]() mutable {
        cb(std::move(uri), scale, x, y, std::move(t));
      });
    }
    return;
  }
  Job job;
  job.kind = JobKind::EnsureTiles;
  job.uri = std::move(uri);
  job.tile_scale = scale;
  job.tile_x = x;
  job.tile_y = y;
  job.tile_min_scale = scale;
  // Interactive: only this scale (not full pyramid). Use request_tile_pyramid to batch.
  job.tile_max_scale = scale;
  job.tile_pyramid = false;
  job.tile_cb = std::move(cb);
  enqueue(std::move(job));
}

void Client::request_tile_pyramid(std::string uri, int min_scale, int max_scale,
                                  TileCallback on_done) {
  Job job;
  job.kind = JobKind::EnsureTiles;
  job.uri = std::move(uri);
  job.tile_min_scale = min_scale;
  job.tile_max_scale = max_scale;
  job.tile_pyramid = true;
  job.tile_cb = std::move(on_done);
  enqueue(std::move(job));
}

void Client::enqueue(Job job) {
  {
    std::lock_guard lock(mu_);
    queue_.push_back(std::move(job));
  }
  cv_.notify_one();
}

std::string Client::extract_cache_key(const std::filesystem::path& archive,
                                      std::string_view member) {
  return archive.string() + "\n" + std::string(member);
}

void Client::extract_cache_put(const std::filesystem::path& archive,
                               std::string_view member,
                               std::vector<std::uint8_t> bytes) {
  if (bytes.empty()) return;
  std::lock_guard lock(extract_cache_mu_);
  const std::string key = extract_cache_key(archive, member);
  if (auto it = extract_cache_.find(key); it != extract_cache_.end()) {
    extract_cache_bytes_ -= it->second.size();
    extract_cache_bytes_ += bytes.size();
    it->second = std::move(bytes);
    return;
  }
  if (extract_cache_bytes_ + bytes.size() > kExtractCacheMaxBytes) {
    extract_cache_.clear();
    extract_cache_bytes_ = 0;
  }
  extract_cache_bytes_ += bytes.size();
  extract_cache_.emplace(key, std::move(bytes));
}

std::optional<std::vector<std::uint8_t>> Client::extract_cache_get(
    const std::filesystem::path& archive, std::string_view member) const {
  std::lock_guard lock(extract_cache_mu_);
  auto it = extract_cache_.find(extract_cache_key(archive, member));
  if (it == extract_cache_.end()) return std::nullopt;
  return it->second;
}

std::optional<std::vector<std::uint8_t>> Client::member_bytes(
    const std::filesystem::path& archive, std::string_view member,
    const std::optional<std::vector<std::uint8_t>>& preextracted) {
  if (preextracted && !preextracted->empty()) {
    extract_cache_put(archive, member, *preextracted);
    return *preextracted;
  }
  if (auto cached = extract_cache_get(archive, member)) {
    return cached;
  }
  auto bytes = extract_archive_member(archive, member);
  if (bytes && !bytes->empty()) {
    extract_cache_put(archive, member, *bytes);
  }
  return bytes;
}


std::optional<std::vector<std::uint8_t>> Client::fetch_http_cached(
    std::string_view url) {
  if (!is_http_uri(url)) return std::nullopt;
  const std::string key(url);
  {
    std::lock_guard lock(http_cache_mu_);
    if (auto it = http_cache_.find(key); it != http_cache_.end()) {
      return it->second;
    }
  }
  // Durable cache (survives process restart).
  if (blobs_) {
    if (auto disk = blobs_->get_http_body(key, kHttpCacheTtlSeconds)) {
      std::lock_guard lock(http_cache_mu_);
      if (http_cache_bytes_ + disk->size() > kHttpCacheMaxBytes) {
        http_cache_.clear();
        http_cache_bytes_ = 0;
      }
      http_cache_bytes_ += disk->size();
      http_cache_.emplace(key, *disk);
      return disk;
    }
  }
  auto bytes = http_get_bytes(url, kArchiveMaxMemberUncompressedBytes);
  if (!bytes || bytes->empty()) return std::nullopt;
  using namespace std::chrono;
  const auto now =
      duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
  if (blobs_) {
    try {
      blobs_->put_http_body(key, bytes->data(), bytes->size(), now);
    } catch (...) {
      // Disk full / SQLite error: still return in-memory body.
    }
  }
  {
    std::lock_guard lock(http_cache_mu_);
    if (auto it = http_cache_.find(key); it != http_cache_.end()) {
      return it->second;
    }
    if (http_cache_bytes_ + bytes->size() > kHttpCacheMaxBytes) {
      http_cache_.clear();
      http_cache_bytes_ = 0;
    }
    http_cache_bytes_ += bytes->size();
    http_cache_.emplace(key, *bytes);
  }
  return bytes;
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
    if (auto pdf = parse_pdf_uri(uri)) {
      loc.outer_path = pdf->pdf_path.string();
      loc.member_path = std::to_string(pdf->page);
      loc.size = file_size_bytes(pdf->pdf_path);
      loc.mtime_ns = file_mtime_ns(pdf->pdf_path);
    } else if (auto arch = parse_archive_uri(uri)) {
      loc.outer_path = arch->archive_path.string();
      loc.member_path = arch->member_path;
      loc.size = file_size_bytes(arch->archive_path);
      loc.mtime_ns = file_mtime_ns(arch->archive_path);
    } else if (auto path = path_from_file_uri(uri)) {
      loc.outer_path = path->string();
      loc.size = file_size_bytes(*path);
      loc.mtime_ns = file_mtime_ns(*path);
    } else if (is_http_uri(uri)) {
      // Network location — size/mtime filled after download.
      loc.outer_path = std::string(uri);
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

    if (is_likely_pdf_path(abs)) {
      auto count = pdf_page_count(abs);
      if (count && *count > 0) {
        // Cap prepare volume so huge books do not flood the queue.
        constexpr int kMaxPreparePages = 512;
        const int n = std::min(*count, kMaxPreparePages);
        for (int page = 1; page <= n; ++page) {
          const auto uri = pdf_page_uri(abs, page);
          if (auto existing = db_->find_locator(uri)) {
            if (auto meta = db_->meta_for_uri(uri)) {
              if (meta->status == ContentStatus::Ready && meta->size) continue;
            }
            Pending item;
            item.uri = uri;
            pending.push_back(std::move(item));
            continue;
          }
          Pending item;
          item.uri = uri;
          item.need_register = true;
          item.loc.uri = uri;
          item.loc.content_id = make_provisional_id();
          item.loc.outer_path = abs.string();
          item.loc.member_path = std::to_string(page);
          item.loc.size = file_size_bytes(abs);
          item.loc.mtime_ns = file_mtime_ns(abs);
          item.content.content_id = *item.loc.content_id;
          item.content.status = ContentStatus::Pending;
          pending.push_back(std::move(item));
        }
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


std::optional<int> Client::pdf_page_count(const std::filesystem::path& path) {
  return thumtoo::pdf_page_count(path);
}

std::optional<Client::PdfPageRaster> Client::pdf_rasterize_page(
    const std::filesystem::path& path, int page_1based, int max_edge) {
  auto r = thumtoo::pdf_rasterize_page(path, page_1based, max_edge);
  if (!r) return std::nullopt;
  PdfPageRaster out;
  out.width = r->width;
  out.height = r->height;
  out.rgb = std::move(r->rgb);
  return out;
}

std::string Client::pdf_page_uri(const std::filesystem::path& path,
                                 int page_1based) {
  return thumtoo::pdf_page_uri(path, page_1based);
}

bool Client::is_pdf_path(const std::filesystem::path& path) {
  return thumtoo::is_likely_pdf_path(path);
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
      cv_.wait_for(lock, std::chrono::milliseconds(50), [this] {
        return stop_ || !queue_.empty();
      });
      if (stop_ && queue_.empty()) return;
      if (queue_.empty()) {
        continue;
      }
      // Claim the job under the lock before removing it so drain() cannot
      // observe (empty queue && inflight==0) while work is still about to run.
      if (!queue_.front().uri.empty()) ++inflight_;
      single = std::move(queue_.front());
      queue_.erase(queue_.begin());
      if (stop_ && single.uri.empty()) return;

      // Coalesce same-archive jobs of the same kind into one libarchive pass.
      if (!single.uri.empty() &&
          (single.kind == JobKind::ProbeSize ||
           single.kind == JobKind::EnsureTiles ||
           single.kind == JobKind::EnsurePixels)) {
        if (auto arch = parse_archive_uri(single.uri);
            arch && !arch->member_path.empty()) {
          const JobKind batch_kind = single.kind;
          batch.push_back(std::move(single));
          for (auto it = queue_.begin(); it != queue_.end();) {
            if (it->kind != batch_kind || it->uri.empty()) {
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
      JobKind batch_kind = JobKind::ProbeSize;
      for (const auto& j : batch) {
        batch_kind = j.kind;
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
      for (const auto& kv : extracted) {
        extract_cache_put(archive_path, kv.first, kv.second);
      }
      for (size_t i = 0; i < batch.size(); ++i) {
        try {
          std::optional<std::vector<std::uint8_t>> pre;
          if (i < members.size() && !members[i].empty()) {
            if (auto it = extracted.find(members[i]); it != extracted.end()) {
              pre = std::move(it->second);
            }
          }
          if (batch_kind == JobKind::ProbeSize)
            handle_probe_size(batch[i], pre);
          else if (batch_kind == JobKind::EnsureTiles)
            handle_ensure_tiles(batch[i], pre);
          else if (batch_kind == JobKind::EnsurePixels)
            handle_ensure_pixels(batch[i], pre);
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
    {
      std::lock_guard lock(mu_);
      if (stop_) {
        // Shutdown: do not start new encode work.
        --inflight_;
        continue;
      }
    }
    try {
      if (single.kind == JobKind::ProbeSize) handle_probe_size(single);
      else if (single.kind == JobKind::EnsurePixels) handle_ensure_pixels(single);
      else if (single.kind == JobKind::EnsureTiles) handle_ensure_tiles(single);
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
  global_build_stats().probes_done.fetch_add(1, std::memory_order_relaxed);
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

  // Size already known (ladder may still be missing — Incomplete).
  // Do not re-hash / re-probe on every request_size.
  if (row.width && row.height
      && (row.status == ContentStatus::Ready
          || row.status == ContentStatus::Incomplete)) {
    size_out = Size{*row.width, *row.height};
    if (job.size_cb) {
      auto cb = std::move(job.size_cb);
      auto uri = job.uri;
      executor_.post([cb = std::move(cb), uri = std::move(uri), size_out]() mutable {
        cb(std::move(uri), size_out);
      });
    }
    return;
  }

  if (auto pdf = parse_pdf_uri(job.uri)) {
    if (!std::filesystem::is_regular_file(pdf->pdf_path)) {
      row.status = ContentStatus::Failed;
      row.error_code = "not_a_file";
    } else {
      auto layout = pdf_page_layout_size(pdf->pdf_path, pdf->page);
      if (!layout) {
        row.status = ContentStatus::Failed;
        row.error_code = "pdf_page_failed";
      } else {
        // Content id: file bytes + page so each page is distinct under rename.
        std::string new_id = old_id;
        const auto hex = sha256_file_hex(pdf->pdf_path);
        if (!hex.empty()) {
          new_id = std::string(kContentIdSha256Prefix) + hex + ":page:"
                   + std::to_string(pdf->page);
        }
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
        row.width = layout->width;
        row.height = layout->height;
        row.format = "pdf";
        row.error_code = std::nullopt;
        size_out = *layout;
        row.status = ContentStatus::Incomplete;
      }
    }
  } else if (auto arch = parse_archive_uri(job.uri)) {
    if (arch->member_path.empty()) {
      row.status = ContentStatus::Unsupported;
      row.error_code = "archive_root_not_image";
    } else {
      std::optional<std::vector<std::uint8_t>> bytes =
          member_bytes(arch->archive_path, arch->member_path, preextracted);
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
          // Size probe only — ladder encode runs on EnsurePixels / request_pixels.
          row.status = ContentStatus::Incomplete;
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
        // Size probe only — ladder encode runs on EnsurePixels / request_pixels.
        row.status = ContentStatus::Incomplete;
      }
    }
  } else if (is_http_uri(job.uri)) {
    if (!http_fetch_available()) {
      row.status = ContentStatus::Unsupported;
      row.error_code = "http_fetch_unavailable";
    } else {
      auto bytes = fetch_http_cached(job.uri);
      if (!bytes) {
        row.status = ContentStatus::Failed;
        row.error_code = "http_fetch_failed";
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
        auto probe = probe_image_buffer(bytes->data(), bytes->size(), "unknown");
        if (!probe) {
          row.status = ContentStatus::Unsupported;
          row.error_code = "unrecognized_image";
        } else {
          row.width = probe->size.width;
          row.height = probe->size.height;
          row.format = probe->format;
          row.error_code = std::nullopt;
          size_out = probe->size;
          row.status = ContentStatus::Incomplete;
        }
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



void Client::handle_ensure_pixels(
    Job& job, const std::optional<std::vector<std::uint8_t>>& preextracted) {
  global_build_stats().pixel_jobs.fetch_add(1, std::memory_order_relaxed);
  // Fast path: ladder already present.
  if (auto px = get_pixels(job.uri, job.max_edge, job.frame_idx)) {
    if (job.pixels_cb) {
      auto cb = std::move(job.pixels_cb);
      auto uri = job.uri;
      const int edge = job.max_edge;
      executor_.post([cb = std::move(cb), uri = std::move(uri), edge,
                      px = std::move(px)]() mutable {
        cb(std::move(uri), edge, std::move(px));
      });
    }
    return;
  }

  // Ensure native size is known (size-only; does not encode ladder).
  {
    Job probe;
    probe.kind = JobKind::ProbeSize;
    probe.uri = job.uri;
    handle_probe_size(probe);
  }

  if (auto px = get_pixels(job.uri, job.max_edge, job.frame_idx)) {
    if (job.pixels_cb) {
      auto cb = std::move(job.pixels_cb);
      auto uri = job.uri;
      const int edge = job.max_edge;
      executor_.post([cb = std::move(cb), uri = std::move(uri), edge,
                      px = std::move(px)]() mutable {
        cb(std::move(uri), edge, std::move(px));
      });
    }
    return;
  }

  // Encode display ladder now that size/content_id are settled.
  auto loc = db_->find_locator(job.uri);
  if (!loc || !loc->content_id) {
    if (job.pixels_cb) {
      auto cb = std::move(job.pixels_cb);
      auto uri = job.uri;
      const int edge = job.max_edge;
      executor_.post([cb = std::move(cb), uri = std::move(uri), edge]() mutable {
        cb(std::move(uri), edge, std::nullopt);
      });
    }
    return;
  }

  Database::ContentRow row;
  if (auto existing = db_->find_content(*loc->content_id)) {
    row = *existing;
  } else {
    row.content_id = *loc->content_id;
    row.status = ContentStatus::Pending;
  }

  // Single durable preview ≤ job.max_edge (not a full multi-edge ladder).
  // Larger / other sizes: another request_pixels, downscale from cache, or tiles.
  const int edge_limit = job.max_edge > 0 ? job.max_edge : kLadderEdges.back();

  if (auto loc_early = db_->find_locator(job.uri);
      loc_early && loc_early->content_id) {
    if (auto larger = db_->find_smallest_level_ge(
            *loc_early->content_id, edge_limit, job.frame_idx)) {
      if (auto bytes = blobs_->get_level(larger->content_id, larger->max_edge,
                                         larger->frame_idx)) {
        if (auto lvl = downscale_preview_jxl(
                bytes->data(), bytes->size(), *loc_early->content_id, edge_limit,
                kDefaultJxlQuality)) {
          const std::string& cid = *loc_early->content_id;
          blobs_->put_level(cid, lvl->max_edge, lvl->frame_idx, lvl->width,
                            lvl->height, lvl->codec, lvl->quality,
                            lvl->bytes.data(), lvl->bytes.size());
          Database::LevelRow lr;
          lr.content_id = cid;
          lr.max_edge = lvl->max_edge;
          lr.frame_idx = lvl->frame_idx;
          lr.width = lvl->width;
          lr.height = lvl->height;
          lr.codec = lvl->codec;
          lr.quality = lvl->quality;
          lr.path = "blobs.sqlite";
          db_->upsert_level(lr);
          if (auto px = get_pixels(job.uri, job.max_edge, job.frame_idx)) {
            if (job.pixels_cb) {
              auto cb = std::move(job.pixels_cb);
              auto uri = job.uri;
              const int edge = job.max_edge;
              executor_.post([cb = std::move(cb), uri = std::move(uri), edge,
                              px = std::move(px)]() mutable {
                cb(std::move(uri), edge, std::move(px));
              });
            }
            return;
          }
        }
      }
    }
  }

  if (auto pdf = parse_pdf_uri(job.uri)) {
    auto raster = pdf_rasterize_page(pdf->pdf_path, pdf->page, edge_limit);
    if (raster && !raster->rgb.empty()) {
      auto levels = build_ladder_rgb(raster->rgb.data(), raster->width,
                                     raster->height, row.content_id,
                                     kDefaultJxlQuality, edge_limit);
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
      else row.error_code = std::nullopt;
      db_->upsert_content(row);
    }
  } else if (auto arch = parse_archive_uri(job.uri)) {
    if (!arch->member_path.empty()) {
      auto bytes = member_bytes(arch->archive_path, arch->member_path, preextracted);
      if (bytes && !bytes->empty()) {
        auto levels =
            build_ladder_buffer(bytes->data(), bytes->size(), row.content_id,
                                kDefaultJxlQuality, edge_limit);
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
        else row.error_code = std::nullopt;
        db_->upsert_content(row);
      }
    }
  } else if (is_http_uri(job.uri)) {
    auto bytes = fetch_http_cached(job.uri);
    if (bytes && !bytes->empty()) {
      auto levels =
          build_ladder_buffer(bytes->data(), bytes->size(), row.content_id,
                              kDefaultJxlQuality, edge_limit);
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
      else row.error_code = std::nullopt;
      db_->upsert_content(row);
    }
  } else if (auto path = path_from_file_uri(job.uri)) {
    if (std::filesystem::is_regular_file(*path)) {
      auto levels = build_ladder(*path, row.content_id, kDefaultJxlQuality,
                                 edge_limit);
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
      else row.error_code = std::nullopt;
      db_->upsert_content(row);
    }
  }

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



void Client::store_tiles(const std::string& content_id,
                         const std::vector<TileBlob>& tiles) {
  for (const auto& t : tiles) {
    blobs_->put_tile(content_id, t.scale, t.x, t.y, t.width, t.height, t.codec,
                     kDefaultTileQuality, t.bytes.data(), t.bytes.size());
    Database::TileRow tr;
    tr.content_id = content_id;
    tr.scale = t.scale;
    tr.x = t.x;
    tr.y = t.y;
    tr.width = t.width;
    tr.height = t.height;
    tr.codec = t.codec;
    tr.quality = kDefaultTileQuality;
    db_->upsert_tile(tr);
  }
}

void Client::handle_ensure_tiles(
    Job& job, const std::optional<std::vector<std::uint8_t>>& preextracted) {
  global_build_stats().tile_jobs.fetch_add(1, std::memory_order_relaxed);
  auto reply_one = [&](std::optional<TileBlob> t) {
    if (!job.tile_cb) return;
    auto cb = std::move(job.tile_cb);
    auto uri = job.uri;
    const int scale = job.tile_scale;
    const int x = job.tile_x;
    const int y = job.tile_y;
    executor_.post([cb = std::move(cb), uri = std::move(uri), scale, x, y,
                    t = std::move(t)]() mutable {
      cb(std::move(uri), scale, x, y, std::move(t));
    });
  };

  // Pyramid prewarm: no specific tile coordinate.
  auto reply_pyramid_done = [&](bool ok) {
    if (!job.tile_cb) return;
    auto cb = std::move(job.tile_cb);
    auto uri = job.uri;
    executor_.post([cb = std::move(cb), uri = std::move(uri), ok]() mutable {
      if (ok)
        cb(std::move(uri), 0, 0, 0, TileBlob{});
      else
        cb(std::move(uri), 0, 0, 0, std::nullopt);
    });
  };

  if (!job.tile_pyramid) {
    if (auto t = get_tile(job.uri, job.tile_scale, job.tile_x, job.tile_y)) {
      reply_one(std::move(t));
      return;
    }
  }

  // Size probe first (sets content_id + dimensions).
  {
    Job probe;
    probe.kind = JobKind::ProbeSize;
    probe.uri = job.uri;
    handle_probe_size(probe);
  }

  if (!job.tile_pyramid) {
    if (auto t = get_tile(job.uri, job.tile_scale, job.tile_x, job.tile_y)) {
      reply_one(std::move(t));
      return;
    }
  }

  auto loc = db_->find_locator(job.uri);
  if (!loc || !loc->content_id) {
    if (job.tile_pyramid) reply_pyramid_done(false);
    else reply_one(std::nullopt);
    return;
  }

  const std::string content_id = *loc->content_id;
  const int min_scale = job.tile_pyramid ? job.tile_min_scale : job.tile_scale;
  const int max_scale = job.tile_max_scale;

  // Interactive request_tile: encode only the requested cell.
  if (!job.tile_pyramid) {
    std::optional<TileBlob> cell;
    if (auto arch = parse_archive_uri(job.uri)) {
      if (!arch->member_path.empty()) {
        auto bytes =
            member_bytes(arch->archive_path, arch->member_path, preextracted);
        if (bytes && !bytes->empty()) {
          cell = build_tile_cell_buffer(bytes->data(), bytes->size(),
                                        job.tile_scale, job.tile_x, job.tile_y,
                                        kDefaultTileQuality);
        }
      }
    } else if (auto pdf = parse_pdf_uri(job.uri)) {
      // Region-rasterize only this cell. Supports negative scale (sharper than
      // layout DPI) without allocating a full-page high-DPI buffer.
      cell = pdf_build_tile_cell(pdf->pdf_path, pdf->page, job.tile_scale,
                                 job.tile_x, job.tile_y, kDefaultTileQuality);
    } else if (is_http_uri(job.uri)) {
      auto bytes = fetch_http_cached(job.uri);
      if (bytes && !bytes->empty()) {
        cell = build_tile_cell_buffer(bytes->data(), bytes->size(),
                                      job.tile_scale, job.tile_x, job.tile_y,
                                      kDefaultTileQuality);
      }
    } else if (auto path = path_from_file_uri(job.uri)) {
      if (std::filesystem::is_regular_file(*path)) {
        cell = build_tile_cell(*path, job.tile_scale, job.tile_x, job.tile_y,
                               kDefaultTileQuality);
      }
    }
    if (cell && !cell->bytes.empty()) {
      store_tiles(content_id, std::vector<TileBlob>{*cell});
    }
    reply_one(get_tile(job.uri, job.tile_scale, job.tile_x, job.tile_y));
    return;
  }

  // Pyramid prewarm: full scale range / grids.
  std::vector<TileBlob> tiles;
  if (auto arch = parse_archive_uri(job.uri)) {
    if (!arch->member_path.empty()) {
      auto bytes = member_bytes(arch->archive_path, arch->member_path, preextracted);
      if (bytes && !bytes->empty()) {
        tiles = build_tile_pyramid_buffer(bytes->data(), bytes->size(), min_scale,
                                          max_scale, kDefaultTileQuality);
      }
    }
  } else if (auto pdf = parse_pdf_uri(job.uri)) {
    auto layout = pdf_page_layout_size(pdf->pdf_path, pdf->page);
    if (layout && layout->width > 0 && layout->height > 0) {
      const int edge = std::max(layout->width, layout->height);
      auto raster = pdf_rasterize_page(pdf->pdf_path, pdf->page, edge);
      if (raster && !raster->rgb.empty()) {
        tiles = build_tile_pyramid_rgb(raster->rgb.data(), raster->width,
                                       raster->height, min_scale, max_scale,
                                       kDefaultTileQuality);
      }
    }
  } else if (is_http_uri(job.uri)) {
    auto bytes = fetch_http_cached(job.uri);
    if (bytes && !bytes->empty()) {
      tiles = build_tile_pyramid_buffer(bytes->data(), bytes->size(), min_scale,
                                        max_scale, kDefaultTileQuality);
    }
  } else if (auto path = path_from_file_uri(job.uri)) {
    if (std::filesystem::is_regular_file(*path)) {
      tiles = build_tile_pyramid(*path, min_scale, max_scale, kDefaultTileQuality);
    }
  }

  if (!tiles.empty()) {
    store_tiles(content_id, tiles);
  }
  reply_pyramid_done(!tiles.empty());
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
