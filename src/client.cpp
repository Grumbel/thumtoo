// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/client.hpp"
#include "thumtoo/text.hpp"
#include "thumtoo/build_stats.hpp"
#include "thumtoo/constants.hpp"
#include "thumtoo/uri.hpp"
#include "thumtoo/network.hpp"
#include "thumtoo/image.hpp"
#include "thumtoo/lqip.hpp"
#include "thumtoo/archive.hpp"
#include "thumtoo/pdf.hpp"
#include "thumtoo/djvu.hpp"
#include "thumtoo/epub.hpp"
#include "thumtoo/format.hpp"
#include "thumtoo/layout.hpp"

#include "thumtoo/debug_overlay.hpp"

#include <vips/vips.h>

#include <algorithm>
#include <cmath>
#include <atomic>
#include <deque>
#include <unordered_map>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <condition_variable>
#include <random>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <span>
#include <cstring>

namespace thumtoo {

namespace {

/** Set THUMTOO_DEBUG=1 (or non-empty non-0) for stderr task traces. */
bool env_flag_on(const char* name) {
  const char* e = std::getenv(name);
  if (!e || !e[0] || e[0] == '0') return false;
  // explicit off spellings
  if (e[0] == 'f' || e[0] == 'F' || e[0] == 'n' || e[0] == 'N') return false;
  return true;
}

bool debug_enabled() {
  // Re-check each call so late export THUMTOO_DEBUG=1 still works in a shell.
  return env_flag_on("THUMTOO_DEBUG") || env_flag_on("BILTOO_THUMTOO_DEBUG");
}

std::FILE* debug_file() {
  static std::FILE* fp = []() -> std::FILE* {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    const char* home = std::getenv("HOME");
    std::string dir;
    if (xdg && xdg[0]) dir = std::string(xdg) + "/thumtoo";
    else if (home && home[0]) dir = std::string(home) + "/.cache/thumtoo";
    else dir = "/tmp/thumtoo-debug";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string path = dir + "/debug.log";
    std::FILE* f = std::fopen(path.c_str(), "a");
    if (f) {
      std::fprintf(f, "---- thumtoo debug session ----\n");
      std::fflush(f);
    }
    return f;
  }();
  return fp;
}

void dbg(const char* fmt, ...) {
  if (!debug_enabled()) return;
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  std::fputs("thumtoo: ", stderr);
  std::fputs(buf, stderr);
  std::fputc('\n', stderr);
  std::fflush(stderr);
  if (std::FILE* f = debug_file()) {
    std::fputs("thumtoo: ", f);
    std::fputs(buf, f);
    std::fputc('\n', f);
    std::fflush(f);
  }
}

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

Client::Client(std::unique_ptr<Store> store, Executor executor,
               unsigned worker_threads)
    : store_(std::move(store)), executor_(std::move(executor)) {
  if (debug_enabled()) {
    dbg("Client constructed — THUMTOO_DEBUG active (stderr task traces on)");
  }
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
                                     Executor executor, unsigned worker_threads,
                                     const std::filesystem::path& data_root) {
  // Store at cache_root/; migrate dual-path layouts when present.
  // Legacy Database/BlobStore are no longer opened by Client.
  migrate_dual_path_to_store_root(cache_root);
  Store::Paths sp;
  sp.cache_root = redesign_store_root(cache_root);
  sp.data_root = data_root.empty() ? cache_root : data_root;
  auto store = std::make_unique<Store>(Store::open(sp));
  if (debug_enabled()) {
    dbg("Client::open cache=%s store=%s data=%s",
        cache_root.string().c_str(),
        sp.cache_root.string().c_str(),
        sp.data_root.string().c_str());
  }
  return std::unique_ptr<Client>(
      new Client(std::move(store), std::move(executor), worker_threads));
}

std::optional<ContentMeta> Client::meta_from_store(std::string_view uri) const {
  if (!store_) return std::nullopt;
  auto loc = store_->find_locator(uri);
  if (!loc || !loc->blob_id) return std::nullopt;

  auto ref = store_->blob_ref_sha256(*loc->blob_id);
  if (!ref || !ref->starts_with("blob:")) return std::nullopt;
  // "blob:sha256:…" → legacy-style "sha256:…"
  ContentMeta cm;
  cm.content_id = ref->substr(5);

  if (auto pdf = parse_pdf_uri(uri)) {
    auto media =
        store_->find_media_for_blob(*loc->blob_id, MediaKind::Document);
    if (!media) return std::nullopt;
    auto region = store_->find_region_by_key(
        media->id, RegionKind::Page, std::to_string(pdf->page));
    if (!region) return std::nullopt;
    cm.content_id += ":page:" + std::to_string(pdf->page);
    cm.format = "pdf";
    cm.status = ContentStatus::Incomplete;
    if (!store_->list_tile_scales(media->id, region->id).empty()) {
      cm.status = ContentStatus::Ready;
    }
    // Prefer durable media dims (warm cache must not re-open the PDF).
    // Layout probe only when the Store has no size yet.
    if (media->width && media->height) {
      cm.size = Size{*media->width, *media->height};
    } else if (auto layout =
                   pdf_page_layout_size(pdf->pdf_path, pdf->page, pdf->backend)) {
      cm.size = *layout;
    }
    return cm;
  }

  if (auto dj = parse_djvu_uri(uri)) {
    auto media =
        store_->find_media_for_blob(*loc->blob_id, MediaKind::Document);
    if (!media) return std::nullopt;
    auto region = store_->find_region_by_key(
        media->id, RegionKind::Page, std::to_string(dj->page));
    if (!region) return std::nullopt;
    cm.content_id += ":page:" + std::to_string(dj->page);
    cm.format = "djvu";
    cm.status = ContentStatus::Incomplete;
    if (!store_->list_tile_scales(media->id, region->id).empty()) {
      cm.status = ContentStatus::Ready;
    }
    if (media->width && media->height) {
      cm.size = Size{*media->width, *media->height};
    } else if (auto layout = djvu_page_layout_size(dj->djvu_path, dj->page)) {
      cm.size = *layout;
    }
    return cm;
  }

  if (auto ep = parse_epub_uri(uri)) {
    auto media =
        store_->find_media_for_blob(*loc->blob_id, MediaKind::Document);
    if (!media) return std::nullopt;
    auto region = store_->find_region_by_key(
        media->id, RegionKind::Page, std::to_string(ep->page));
    if (!region) return std::nullopt;
    // Tile target key is sha256:…:page:N (same as PDF/DjVu).
    cm.content_id += ":page:" + std::to_string(ep->page);
    cm.format = "epub";
    cm.status = ContentStatus::Incomplete;
    if (!store_->list_tile_scales(media->id, region->id).empty()) {
      cm.status = ContentStatus::Ready;
    }
    if (media->width && media->height) {
      cm.size = Size{*media->width, *media->height};
    } else if (auto layout =
                   epub_page_layout_size(ep->epub_path, ep->page, ep->layout)) {
      cm.size = *layout;
    }
    return cm;
  }

  if (auto media =
          store_->find_media_for_blob(*loc->blob_id, MediaKind::Image)) {
    if (media->width && media->height) {
      cm.size = Size{*media->width, *media->height};
    }
    cm.format = "image";
    cm.status = ContentStatus::Incomplete;
    if (auto full = store_->find_full_region(media->id)) {
      if (!store_->list_tile_scales(media->id, full->id).empty()) {
        cm.status = ContentStatus::Ready;
      }
    }
    return cm;
  }
  return std::nullopt;
}

std::optional<Size> Client::get_size(std::string_view uri) const {
  if (auto sm = meta_from_store(uri)) {
    return sm->size;
  }
  return std::nullopt;
}

std::optional<ContentMeta> Client::get_meta(std::string_view uri) const {
  return meta_from_store(uri);
}

void Client::replace_directory_snapshot(
    const DirectorySnapshotRow& snap,
    const std::vector<DirectoryEntryRow>& entries) {
  if (!store_) return;
  store_->replace_directory_snapshot(snap, entries);
}

std::optional<Client::DirectorySnapshotRow> Client::find_directory_snapshot(
    std::string_view dir_uri) const {
  if (!store_) return std::nullopt;
  return store_->find_directory_snapshot(dir_uri);
}

std::vector<Client::DirectoryEntryRow> Client::list_directory_entries(
    std::string_view dir_uri, int limit) const {
  if (!store_) return {};
  return store_->list_directory_entries(dir_uri, limit);
}

void Client::delete_directory_snapshot(std::string_view dir_uri) {
  if (!store_) return;
  store_->delete_directory_snapshot(dir_uri);
}

std::size_t Client::refresh_directory_snapshot(
    const std::filesystem::path& dir_path) {
  if (!store_) return 0;
  std::error_code ec;
  if (!std::filesystem::is_directory(dir_path, ec)) return 0;

  const auto abs = std::filesystem::absolute(dir_path, ec);
  if (ec) return 0;
  const std::string dir_uri = file_uri_from_path(abs);

  DirectorySnapshotRow snap;
  snap.dir_uri = dir_uri;
  snap.listed_at = 0;  // Store fills now
  snap.incomplete = false;
  {
    auto ftime = std::filesystem::last_write_time(abs, ec);
    if (!ec) {
      const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          ftime.time_since_epoch())
                          .count();
      snap.mtime_ns = static_cast<std::int64_t>(ns);
    }
  }

  std::vector<DirectoryEntryRow> entries;
  for (std::filesystem::directory_iterator it(abs, ec), end; !ec && it != end;
       it.increment(ec)) {
    const auto& entry = *it;
    DirectoryEntryRow row;
    row.dir_uri = dir_uri;
    row.name = entry.path().filename().string();
    std::error_code e2;
    row.is_dir = entry.is_directory(e2);
    if (!row.is_dir && entry.is_regular_file(e2)) {
      row.size = static_cast<std::int64_t>(entry.file_size(e2));
    }
    auto ctime = entry.last_write_time(e2);
    if (!e2) {
      row.mtime_ns = static_cast<std::int64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              ctime.time_since_epoch())
              .count());
    }
    row.child_uri = file_uri_from_path(entry.path());
    entries.push_back(std::move(row));
  }

  store_->replace_directory_snapshot(snap, entries);
  return entries.size();
}

Client::LocatorRow Client::locator_row_from_store(const Store::LocatorRow& sl) const {
  Client::LocatorRow r{};
  r.uri = sl.uri;
  if (sl.size) {
    r.size = std::optional<std::int64_t>(*sl.size);
  }
  if (sl.mtime_ns) {
    r.mtime_ns = std::optional<std::int64_t>(*sl.mtime_ns);
  }
  if (sl.outer_path) r.outer_path = *sl.outer_path;
  if (sl.member_path) r.member_path = *sl.member_path;
  if (auto m = meta_from_store(sl.uri)) {
    r.content_id = std::move(m->content_id);
  }
  return r;
}

std::vector<Client::LocatorRow> Client::list_locators(int limit) const {
  if (!store_) return {};
  std::vector<Client::LocatorRow> out;
  for (const auto& sl : store_->list_locators(limit)) {
    out.push_back(locator_row_from_store(sl));
  }
  return out;
}

std::optional<Client::LocatorRow> Client::find_locator(std::string_view uri) const {
  if (!store_) return std::nullopt;
  auto sl = store_->find_locator(uri);
  if (!sl) return std::nullopt;
  return locator_row_from_store(*sl);
}

std::vector<Client::LocatorRow> Client::list_locators_by_uri_prefix(
    std::string_view uri_prefix, int limit) const {
  if (!store_ || uri_prefix.empty()) return {};
  std::vector<Client::LocatorRow> out;
  for (const auto& sl : store_->list_locators_by_uri_prefix(uri_prefix, limit)) {
    out.push_back(locator_row_from_store(sl));
  }
  return out;
}

std::vector<Client::LocatorRow> Client::list_locators_by_outer_path_prefix(
    std::string_view path_prefix, int limit) const {
  if (!store_ || path_prefix.empty()) return {};
  std::vector<Client::LocatorRow> out;
  for (const auto& sl :
       store_->list_locators_by_outer_path_prefix(path_prefix, limit)) {
    out.push_back(locator_row_from_store(sl));
  }
  // Also match file:// URI prefix for locators without outer_path filled yet.
  if (out.empty()) {
    std::error_code ec;
    auto abs = std::filesystem::absolute(
        std::filesystem::path(std::string(path_prefix)), ec);
    if (ec) abs = std::filesystem::path(std::string(path_prefix));
    return list_locators_by_uri_prefix(file_uri_from_path(abs.lexically_normal()),
                                       limit);
  }
  return out;
}

std::vector<Client::LocatorRow> Client::list_locators_like(
    std::string_view uri_like_pattern, int limit) const {
  if (!store_ || uri_like_pattern.empty()) return {};
  std::vector<Client::LocatorRow> out;
  for (const auto& sl : store_->list_locators_like(uri_like_pattern, limit)) {
    out.push_back(locator_row_from_store(sl));
  }
  return out;
}

std::optional<std::string> Client::resolve_content_id(std::string_view uri) const {
  if (is_content_id_uri(uri)) {
    return std::string(uri);
  }
  if (auto sm = meta_from_store(uri)) return sm->content_id;
  return std::nullopt;
}

std::vector<Client::LocatorRow> Client::list_uris_for_content_id(
    std::string_view content_id, int limit) const {
  if (!store_ || content_id.empty()) return {};
  // STORE_ONLY: content_id "sha256:<hex>" or "sha256:<hex>:page:N" → Store blob.
  constexpr std::string_view kSha = "sha256:";
  if (!content_id.starts_with(kSha) || content_id.size() < kSha.size() + 64) {
    return {};
  }
  const std::string_view hex = content_id.substr(kSha.size(), 64);
  auto digest = Store::parse_sha256_digest(hex);
  if (!digest) return {};
  auto blob_id = store_->find_blob_by_hash(HashAlgoId::Sha256, *digest);
  if (!blob_id) return {};
  auto store_locs = store_->list_locators_for_blob(*blob_id, limit);
  std::vector<Client::LocatorRow> out;
  out.reserve(store_locs.size());
  for (const auto& sl : store_locs) {
    auto r = locator_row_from_store(sl);
    r.content_id = std::string(content_id.substr(0, kSha.size() + 64));
    out.push_back(std::move(r));
  }
  return out;
}

std::optional<ContentMeta> Client::get_meta_for_content_id(
    std::string_view content_id) const {
  // Pure sha256 content_id → first Store locator for that blob.
  if (!store_) return std::nullopt;
  constexpr std::string_view kSha = "sha256:";
  if (!content_id.starts_with(kSha) || content_id.size() < kSha.size() + 64) {
    return std::nullopt;
  }
  const std::string_view hex = content_id.substr(kSha.size(), 64);
  auto digest = Store::parse_sha256_digest(hex);
  if (!digest) return std::nullopt;
  auto blob_id = store_->find_blob_by_hash(HashAlgoId::Sha256, *digest);
  if (!blob_id) return std::nullopt;
  auto locs = store_->list_locators_for_blob(*blob_id, 1);
  if (locs.empty()) return std::nullopt;
  return meta_from_store(locs.front().uri);
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
  if (parse_pdf_uri(uri_or_content_id) || parse_pdf_image_uri(uri_or_content_id) ||
      parse_djvu_uri(uri_or_content_id)) {
    // Pages / embedded extracts are display rasters, not a single source blob.
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


namespace {
/// Soft ladder adequacy vs request (request may exceed soft max for tile path).
bool soft_level_covers(const PixelLevel& px, int max_edge) {
  const int want = max_edge > 0 ? max_edge : kMaxSoftLadderEdge;
  // Soft durable levels never exceed kMaxSoftLadderEdge; for larger wants the
  // soft level can only "cover" if we are still in the soft band.
  const int need = std::min(want, kMaxSoftLadderEdge);
  const int long_px = std::max(px.width, px.height);
  return long_px >= (need * 9) / 10;
}

/// True when decoded pixels meet the *full* requested edge (tile or soft).
bool pixels_cover_edge(const PixelLevel& px, int max_edge) {
  const int want = max_edge > 0 ? max_edge : kMaxSoftLadderEdge;
  const int long_px = std::max(px.width, px.height);
  return long_px >= (want * 9) / 10;
}
}  // namespace


namespace {
void maybe_overlay_pixels(std::optional<PixelLevel>& px, std::string_view uri,
                          int max_edge) {
  if (px) {
    debug_overlay_pixel_level(*px, uri, max_edge);
  }
}
}  // namespace

std::optional<PixelLevel> Client::get_pixels(std::string_view uri, int max_edge,
                                             int frame_idx,
                                             bool allow_tile_synth) const {
  (void)frame_idx;
  auto meta = meta_from_store(uri);
  if (!meta) {
    if (debug_enabled()) {
      dbg("get_pixels MISS uri=%s edge=%d (no meta)", std::string(uri).c_str(),
          max_edge);
    }
    return std::nullopt;
  }
  // Prefer TileSynth when allowed (PreferCache / overview / filmstrip).
  if (allow_tile_synth) {
    const int want = max_edge > 0 ? max_edge : kMaxSoftLadderEdge;
    if (auto from_tiles = get_pixels_from_tiles(uri, want)) {
      if (debug_enabled()) {
        dbg("get_pixels HIT uri=%s edge=%d src=tile_synth level=%dx%d",
            std::string(uri).c_str(), max_edge, from_tiles->width,
            from_tiles->height);
      }
      maybe_overlay_pixels(from_tiles, uri, max_edge);
      return from_tiles;
    }
  }
  if (debug_enabled()) {
    dbg("get_pixels MISS uri=%s edge=%d (no tiles)", std::string(uri).c_str(),
        max_edge);
  }
  return std::nullopt;
}


std::optional<PixelLevel> Client::get_pixels_from_tiles(std::string_view uri,
                                                        int max_edge) const {
  image_library_init();
  auto meta = meta_from_store(uri);
  if (!meta || !meta->size) return std::nullopt;
  const int nw = meta->size->width;
  const int nh = meta->size->height;
  if (nw <= 0 || nh <= 0) return std::nullopt;

  int min_s = 0, max_s = 0;
  auto tgt = store_tile_target_for_content_id(meta->content_id);
  if (!tgt) return std::nullopt;
  auto scales = store_->list_tile_scales(tgt->media_id, tgt->region_id);
  if (scales.empty()) return std::nullopt;
  min_s = scales.front();
  max_s = scales.back();

  const int want = max_edge > 0 ? max_edge : kBatchMaxEdge;
  // Prefer coarsest scale whose long edge still covers `want` (fewer tiles).
  int best_s = min_s;
  for (int s = min_s; s <= max_s; ++s) {
    const int long_at_s = std::max(dim_at_tile_scale(nw, s),
                                   dim_at_tile_scale(nh, s));
    if (long_at_s >= (want * 9) / 10) {
      best_s = s;
    }
  }

  const int sw = dim_at_tile_scale(nw, best_s);
  const int sh = dim_at_tile_scale(nh, best_s);
  const int nx = (sw + kTileSize - 1) / kTileSize;
  const int ny = (sh + kTileSize - 1) / kTileSize;
  if (nx <= 0 || ny <= 0 || nx * ny > 4096) {
    return std::nullopt;
  }

  std::vector<std::uint8_t> canvas(static_cast<std::size_t>(sw) * static_cast<std::size_t>(sh) * 3u, 0);
  for (int ty = 0; ty < ny; ++ty) {
    for (int tx = 0; tx < nx; ++tx) {
      auto tile = get_tile(uri, best_s, tx, ty);
      if (!tile || tile->bytes.empty()) {
        return std::nullopt;  // incomplete pyramid at this scale
      }
      VipsImage* im = nullptr;
      if (vips_jpegload_buffer(
              const_cast<std::uint8_t*>(tile->bytes.data()),
              static_cast<size_t>(tile->bytes.size()), &im, nullptr) != 0 ||
          !im) {
        return std::nullopt;
      }
      if (vips_image_wio_input(im) != 0) {
        g_object_unref(im);
        return std::nullopt;
      }
      VipsImage* rgb = nullptr;
      if (im->Type != VIPS_INTERPRETATION_sRGB || im->Bands != 3 ||
          im->BandFmt != VIPS_FORMAT_UCHAR) {
        if (vips_colourspace(im, &rgb, VIPS_INTERPRETATION_sRGB, nullptr) != 0 ||
            !rgb) {
          g_object_unref(im);
          return std::nullopt;
        }
        g_object_unref(im);
        im = rgb;
        rgb = nullptr;
        if (im->BandFmt != VIPS_FORMAT_UCHAR) {
          VipsImage* u8 = nullptr;
          if (vips_cast_uchar(im, &u8, nullptr) != 0 || !u8) {
            g_object_unref(im);
            return std::nullopt;
          }
          g_object_unref(im);
          im = u8;
        }
        if (vips_image_wio_input(im) != 0) {
          g_object_unref(im);
          return std::nullopt;
        }
      }
      const int tw = std::min(kTileSize, sw - tx * kTileSize);
      const int th = std::min(kTileSize, sh - ty * kTileSize);
      const int cw = std::min(tw, im->Xsize);
      const int ch = std::min(th, im->Ysize);
      const int bands = im->Bands;
      if (bands < 3) {
        g_object_unref(im);
        return std::nullopt;
      }
      for (int y = 0; y < ch; ++y) {
        const std::uint8_t* src =
            static_cast<const std::uint8_t*>(VIPS_IMAGE_ADDR(im, 0, y));
        std::uint8_t* dst =
            canvas.data() +
            (static_cast<std::size_t>(ty * kTileSize + y) *
                 static_cast<std::size_t>(sw) +
             static_cast<std::size_t>(tx * kTileSize)) *
                3u;
        if (bands == 3) {
          std::memcpy(dst, src, static_cast<std::size_t>(cw) * 3u);
        } else {
          for (int x = 0; x < cw; ++x) {
            dst[x * 3 + 0] = src[x * bands + 0];
            dst[x * 3 + 1] = src[x * bands + 1];
            dst[x * 3 + 2] = src[x * bands + 2];
          }
        }
      }
      g_object_unref(im);
    }
  }

  int out_w = sw;
  int out_h = sh;
  std::vector<std::uint8_t> out_rgb = std::move(canvas);
  const int long_px = std::max(sw, sh);
  if (want > 0 && long_px > want) {
    VipsImage* in = vips_image_new_from_memory(
        out_rgb.data(), out_rgb.size(), sw, sh, 3, VIPS_FORMAT_UCHAR);
    if (!in) return std::nullopt;
    in->Type = VIPS_INTERPRETATION_sRGB;
    VipsImage* small = nullptr;
    const double scale = static_cast<double>(want) / static_cast<double>(long_px);
    if (vips_resize(in, &small, scale, nullptr) != 0 || !small) {
      g_object_unref(in);
      return std::nullopt;
    }
    g_object_unref(in);
    if (vips_image_wio_input(small) != 0) {
      g_object_unref(small);
      return std::nullopt;
    }
    out_w = small->Xsize;
    out_h = small->Ysize;
    out_rgb.resize(static_cast<std::size_t>(out_w) * static_cast<std::size_t>(out_h) * 3u);
    for (int y = 0; y < out_h; ++y) {
      std::memcpy(out_rgb.data() + static_cast<std::size_t>(y * out_w) * 3u,
                  VIPS_IMAGE_ADDR(small, 0, y),
                  static_cast<std::size_t>(out_w) * 3u);
    }
    g_object_unref(small);
  }

  auto levels = build_ladder_rgb(out_rgb.data(), out_w, out_h, meta->content_id,
                                 /*jxl_quality=*/70, std::max(out_w, out_h));
  if (levels.empty()) return std::nullopt;
  PixelLevel px;
  px.max_edge = levels[0].max_edge;
  px.frame_idx = 0;
  px.width = levels[0].width;
  px.height = levels[0].height;
  px.codec = levels[0].codec;
  px.bytes = std::move(levels[0].bytes);
  px.source = PixelSource::TileSynth;
  debug_overlay_pixel_level(px, uri, max_edge);
  return px;
}

namespace {

struct LqipKey {
  std::int64_t blob_id = 0;
  int page_1based = 0;  // 0 = whole blob
};

std::optional<LqipKey> lqip_key_lookup(const Store& store, std::string_view uri) {
  if (auto pdf = parse_pdf_uri(uri)) {
    // Prefer page URI locator, else document file locator.
    if (auto loc = store.find_locator(uri); loc && loc->blob_id)
      return LqipKey{*loc->blob_id, pdf->page};
    const auto fu = file_uri_from_path(pdf->pdf_path);
    if (auto loc = store.find_locator(fu); loc && loc->blob_id)
      return LqipKey{*loc->blob_id, pdf->page};
    return std::nullopt;
  }
  if (auto pimg = parse_pdf_image_uri(uri)) {
    if (auto loc = store.find_locator(uri); loc && loc->blob_id)
      return LqipKey{*loc->blob_id, 0};
    const auto fu = file_uri_from_path(pimg->pdf_path);
    if (auto loc = store.find_locator(fu); loc && loc->blob_id)
      return LqipKey{*loc->blob_id, 0};
    return std::nullopt;
  }
  if (auto dj = parse_djvu_uri(uri)) {
    if (auto loc = store.find_locator(uri); loc && loc->blob_id)
      return LqipKey{*loc->blob_id, dj->page};
    const auto fu = file_uri_from_path(dj->djvu_path);
    if (auto loc = store.find_locator(fu); loc && loc->blob_id)
      return LqipKey{*loc->blob_id, dj->page};
    return std::nullopt;
  }
  if (auto ep = parse_epub_uri(uri)) {
    if (auto loc = store.find_locator(uri); loc && loc->blob_id)
      return LqipKey{*loc->blob_id, ep->page};
    const auto fu = file_uri_from_path(ep->epub_path);
    if (auto loc = store.find_locator(fu); loc && loc->blob_id)
      return LqipKey{*loc->blob_id, ep->page};
    return std::nullopt;
  }
  if (auto loc = store.find_locator(uri); loc && loc->blob_id)
    return LqipKey{*loc->blob_id, 0};
  return std::nullopt;
}

std::optional<LqipKey> lqip_key_ensure(Store& store, std::string_view uri) {
  if (auto hit = lqip_key_lookup(store, uri)) return hit;
  std::filesystem::path file;
  int page = 0;
  if (auto pdf = parse_pdf_uri(uri)) {
    file = pdf->pdf_path;
    page = pdf->page;
  } else if (auto pimg = parse_pdf_image_uri(uri)) {
    file = pimg->pdf_path;
  } else if (auto dj = parse_djvu_uri(uri)) {
    file = dj->djvu_path;
    page = dj->page;
  } else if (auto ep = parse_epub_uri(uri)) {
    file = ep->epub_path;
    page = ep->page;
  } else if (auto path = path_from_file_uri(uri)) {
    file = *path;
  } else {
    return std::nullopt;
  }
  if (!std::filesystem::is_regular_file(file)) return std::nullopt;
  const auto hex = sha256_file_hex(file);
  if (hex.empty()) return std::nullopt;
  auto digest = Store::parse_sha256_digest(hex);
  if (!digest) return std::nullopt;
  std::int64_t blob_id = 0;
  if (auto existing = store.find_blob_by_hash(HashAlgoId::Sha256, *digest)) {
    blob_id = *existing;
  } else {
    blob_id = store.insert_blob(file_size_bytes(file), BlobStatus::Ok);
    store.put_hash(blob_id, HashAlgoId::Sha256, *digest);
  }
  const auto fu = file_uri_from_path(file);
  (void)store.upsert_locator(fu, blob_id, file_size_bytes(file),
                             file_mtime_ns(file), file.string());
  return LqipKey{blob_id, page};
}

struct EncodedLqip {
  int kind = kLqipKindNone;
  std::vector<std::uint8_t> bytes;
};

EncodedLqip encode_lqip_from_rgb(const std::uint8_t* rgb, int w, int h) {
  EncodedLqip out;
  if (!rgb || w <= 0 || h <= 0) return out;
  // Prefer Handsum (fixed 147 B) when encode succeeds; else ThumbHash.
  auto hs = handsum_encode_rgb888(rgb, w, h);
  if (!hs.empty()) {
    out.kind = kLqipKindHandsum;
    out.bytes = std::move(hs);
    return out;
  }
  auto th = thumbhash_encode_rgb888(rgb, w, h, 32);
  if (!th.empty()) {
    out.kind = kLqipKindThumbHash;
    out.bytes = std::move(th);
  }
  return out;
}

EncodedLqip encode_lqip_for_uri(std::string_view uri) {
  if (auto pdf = parse_pdf_uri(uri)) {
    auto r = pdf_rasterize_page(pdf->pdf_path, pdf->page, /*max_edge=*/32,
                                pdf->backend);
    if (r && !r->rgb.empty())
      return encode_lqip_from_rgb(r->rgb.data(), r->width, r->height);
    return {};
  }
  if (auto pimg = parse_pdf_image_uri(uri)) {
    auto r = pdf_rasterize_embedded_image(pimg->pdf_path, pimg->image,
                                          /*max_edge=*/32);
    if (r && !r->rgb.empty())
      return encode_lqip_from_rgb(r->rgb.data(), r->width, r->height);
    return {};
  }
  if (auto dj = parse_djvu_uri(uri)) {
    auto r = djvu_rasterize_page(dj->djvu_path, dj->page, /*max_edge=*/32);
    if (r && !r->rgb.empty())
      return encode_lqip_from_rgb(r->rgb.data(), r->width, r->height);
    return {};
  }
  if (auto ep = parse_epub_uri(uri)) {
    auto r = epub_rasterize_page(ep->epub_path, ep->page, ep->layout,
                                 /*max_edge=*/32);
    if (r && !r->rgb.empty())
      return encode_lqip_from_rgb(r->rgb.data(), r->width, r->height);
    return {};
  }
  if (auto path = path_from_file_uri(uri);
      path && std::filesystem::is_regular_file(*path) && !is_archive_uri(uri) &&
      !is_pdf_page_uri(uri) && !is_pdf_image_uri(uri) &&
      !is_epub_layout_uri(uri)) {
    // File path: ThumbHash helper (internal downscale); Handsum needs RGB load.
    EncodedLqip out;
    out.bytes = lqip_thumbhash_from_file(*path);
    if (!out.bytes.empty()) out.kind = kLqipKindThumbHash;
    return out;
  }
  return {};
}

}  // namespace

std::optional<std::vector<std::uint8_t>> Client::get_lqip(
    std::string_view uri) const {
  if (!store_ || uri.empty()) return std::nullopt;
  auto key = lqip_key_lookup(*store_, uri);
  if (!key) return std::nullopt;
  return store_->get_blob_lqip(key->blob_id, key->page_1based);
}

std::optional<std::vector<std::uint8_t>> Client::ensure_lqip(
    std::string_view uri) {
  if (!store_ || uri.empty()) return std::nullopt;
  if (auto hit = get_lqip(uri)) return hit;

  auto key = lqip_key_ensure(*store_, uri);
  if (!key) return std::nullopt;

  // Cheap path: ThumbHash/Handsum from an already-decoded soft/tile overview
  // (≤64 long edge). Avoids a second full-source vips_thumbnail for file://.
  if (auto px = get_pixels(uri, /*max_edge=*/64, /*frame_idx=*/0,
                           /*allow_tile_synth=*/true);
      px && !px->bytes.empty()) {
    auto bytes = lqip_thumbhash_from_buffer(px->bytes.data(), px->bytes.size());
    if (!bytes.empty()) {
      const int kind =
          bytes.size() == 147 ? kLqipKindHandsum : kLqipKindThumbHash;
      store_->put_blob_lqip(key->blob_id, kind, bytes, key->page_1based);
      return bytes;
    }
  }

  // Expensive path: rasterize/thumbnail the source again.
  EncodedLqip enc = encode_lqip_for_uri(uri);
  if (enc.bytes.empty()) {
    if (auto src = read_source_bytes(uri); src && !src->empty()) {
      enc.bytes = lqip_thumbhash_from_buffer(src->data(), src->size());
      if (!enc.bytes.empty()) enc.kind = kLqipKindThumbHash;
    }
  }
  if (enc.bytes.empty()) return std::nullopt;
  const int kind =
      enc.kind != kLqipKindNone ? enc.kind : kLqipKindThumbHash;
  store_->put_blob_lqip(key->blob_id, kind, enc.bytes, key->page_1based);
  return enc.bytes;
}


void Client::request_pixels(std::string uri, int max_edge, PixelsCallback cb,
                            int frame_idx) {
  // Cap soft ladder requests — deeper zoom is tiles, not full-page JXL.
  if (max_edge > kMaxSoftLadderEdge) {
    max_edge = kMaxSoftLadderEdge;
  }
  // Soft ladder only — never accept TileSynth as a soft hit (that path is
  // JPEG-heavy and serial per cell). Missing soft → EnsurePixels encodes ladder.
  if (auto px = get_pixels(uri, max_edge, frame_idx, /*allow_tile_synth=*/false)) {
    if (soft_level_covers(*px, max_edge)
        && px->source != PixelSource::TileSynth) {
      // Soft already durable — fill LQIP from that blob on a worker if missing.
      // Does not block this reply; ensure_lqip prefers soft levels over source.
      if (!get_lqip(uri)) {
        request_lqip(uri);
      }
      if (cb) {
        executor_.post([cb = std::move(cb), uri, max_edge,
                        px = std::move(*px)]() mutable {
          cb(std::move(uri), max_edge, std::move(px));
        });
      }
      return;
    }
    // Inadequate soft level — fall through to EnsurePixels to grow ladder.
  }
  // Ensure locator exists (same as request_size) so EnsurePixels → ProbeSize
  // can resolve //pdfimage: / //page: without a prior scheduleProbe race.
  Job job;
  job.kind = JobKind::EnsurePixels;
  job.uri = std::move(uri);
  job.max_edge = max_edge;
  job.frame_idx = frame_idx;
  job.pixels_cb = std::move(cb);
  enqueue(std::move(job));
}



void Client::request_overview_pixels(std::string uri, int max_edge,
                                       PixelsCallback cb) {
  // FastBatch cap — not soft durable max.
  if (max_edge <= 0) {
    max_edge = kBatchMaxEdge;
  }
  if (max_edge > kBatchMaxEdge) {
    max_edge = kBatchMaxEdge;
  }
  // Cache hit: soft, or TileSynth via get_pixels fallthrough.
  if (auto px = get_pixels(uri, max_edge, 0)) {
    const int long_px = std::max(px->width, px->height);
    if (long_px >= (max_edge * 9) / 10 ||
        px->source == PixelSource::TileSynth ||
        px->source == PixelSource::Full) {
      if (!get_lqip(uri)) {
        request_lqip(uri);
      }
      if (cb) {
        executor_.post([cb = std::move(cb), uri, max_edge,
                        px = std::move(*px)]() mutable {
          cb(std::move(uri), max_edge, std::move(px));
        });
      }
      return;
    }
  }
  // Ensure locator (same path as request_pixels).
  Job job;
  job.kind = JobKind::EnsurePixels;
  job.uri = std::move(uri);
  job.max_edge = max_edge;
  job.frame_idx = 0;
  job.overview = true;
  job.pixels_cb = std::move(cb);
  enqueue(std::move(job));
}

bool Client::has_tile(std::string_view uri, int scale, int x, int y) const {
  auto sm = meta_from_store(uri);
  if (!sm) return false;
  const std::string& content_id = sm->content_id;
  if (auto tgt = store_tile_target_for_content_id(content_id)) {
    return store_->has_tile(tgt->media_id, tgt->region_id, scale, x, y);
  }
  return false;
}

std::optional<TileBlob> Client::get_tile(std::string_view uri, int scale, int x,
                                         int y) const {
  auto sm = meta_from_store(uri);
  if (!sm) return std::nullopt;
  if (auto tgt = store_tile_target_for_content_id(sm->content_id)) {
    auto bytes =
        store_->get_tile_data(tgt->media_id, tgt->region_id, scale, x, y);
    if (!bytes) return std::nullopt;
    auto row =
        store_->find_tile_meta(tgt->media_id, tgt->region_id, scale, x, y);
    TileBlob t;
    t.scale = scale;
    t.x = x;
    t.y = y;
    if (row) {
      t.width = row->width;
      t.height = row->height;
      if (row->codec_id == CodecId::Jxl) t.codec = "jxl";
      else if (row->codec_id == CodecId::Png) t.codec = "png";
      else t.codec = kDefaultTileCodec;
    } else {
      t.codec = kDefaultTileCodec;
    }
    t.bytes = std::move(*bytes);
    t.source = TileSource::Full;
    debug_overlay_tile(t, uri);
    return t;
  }
  return std::nullopt;
}

std::optional<Client::StoreTileTarget> Client::store_tile_target_for_content_id(
    std::string_view content_id) const {
  if (!store_ || content_id.empty()) return std::nullopt;

  constexpr std::string_view kSha = "sha256:";
  if (!content_id.starts_with(kSha) || content_id.size() < kSha.size() + 64) {
    return std::nullopt;
  }

  const std::string_view rest(content_id.data() + kSha.size(),
                              content_id.size() - kSha.size());
  const std::string_view hex = rest.substr(0, 64);
  for (char c : hex) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) {
      return std::nullopt;
    }
  }

  std::optional<int> page_1based;
  if (rest.size() > 64) {
    constexpr std::string_view kPage = ":page:";
    if (rest.size() > 64 + kPage.size() &&
        rest.substr(64, kPage.size()) == kPage) {
      try {
        page_1based = std::stoi(std::string(rest.substr(64 + kPage.size())));
      } catch (...) {
        return std::nullopt;
      }
      if (*page_1based < 1) return std::nullopt;
    } else {
      return std::nullopt;
    }
  }

  auto digest = Store::parse_sha256_digest(hex);
  if (!digest || digest->size() != 32) return std::nullopt;

  auto blob_id = store_->find_blob_by_hash(HashAlgoId::Sha256, *digest);
  if (!blob_id) return std::nullopt;

  if (page_1based) {
    auto media = store_->find_media_for_blob(*blob_id, MediaKind::Document);
    if (!media) return std::nullopt;
    auto region = store_->find_region_by_key(
        media->id, RegionKind::Page, std::to_string(*page_1based));
    if (!region) return std::nullopt;
    return StoreTileTarget{media->id, region->id};
  }

  auto media = store_->find_media_for_blob(*blob_id, MediaKind::Image);
  if (!media) return std::nullopt;
  auto full = store_->find_full_region(media->id);
  if (!full) return std::nullopt;
  return StoreTileTarget{media->id, full->id};
}

std::optional<TileCoverage> Client::get_tile_coverage(
    std::string_view uri) const {
  auto meta = meta_from_store(uri);
  if (!meta) return std::nullopt;
  TileCoverage cov;
  if (meta->size) cov.size = *meta->size;
  if (auto tgt = store_tile_target_for_content_id(meta->content_id)) {
    auto scales = store_->list_tile_scales(tgt->media_id, tgt->region_id);
    if (!scales.empty()) {
      cov.min_scale = scales.front();
      cov.max_scale = scales.back();
      return cov;
    }
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
    w = dim_at_tile_scale(w, 1);
    h = dim_at_tile_scale(h, 1);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    ++s;
  }
  cov.max_scale = s;
  return cov;
}


std::optional<PixelLevel> Client::get_full_pixels(std::string_view uri,
                                                   int max_edge) const {
  if (uri.empty()) return std::nullopt;
  int want = max_edge > 0 ? max_edge : kFullMaxEdge;
  if (want > kFullMaxEdge) want = kFullMaxEdge;
  // Prefer a level that *covers* the request. Source::Full alone is not enough —
  // soft ladder rows were sometimes tagged Full (edge >= soft raster long edge),
  // so 512px "Full" short-circuited request_full forever.
  if (auto px = get_pixels(uri, want, 0)) {
    if (pixels_cover_edge(*px, want)) {
      return px;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

void Client::request_full_pixels(std::string uri, int max_edge,
                                 PixelsCallback cb) {
  if (max_edge <= 0) {
    max_edge = kFullMaxEdge;
  }
  if (max_edge > kFullMaxEdge) {
    max_edge = kFullMaxEdge;
  }
  if (auto px = get_full_pixels(uri, max_edge)) {
    // Coverage only — never accept mis-tagged Full soft levels.
    if (pixels_cover_edge(*px, max_edge)) {
      if (cb) {
        executor_.post([cb = std::move(cb), uri, max_edge,
                        px = std::move(*px)]() mutable {
          cb(std::move(uri), max_edge, std::move(px));
        });
      }
      return;
    }
  }
  // Ensure locator (same as overview / soft).
  Job job;
  job.kind = JobKind::EnsurePixels;
  job.uri = std::move(uri);
  job.max_edge = max_edge;
  job.frame_idx = 0;
  job.overview = false;
  job.full_native = true;
  job.pixels_cb = std::move(cb);
  enqueue(std::move(job));
}

std::optional<PixelLevel> Client::get_raster(const RasterRequest& req) const {
  if (req.uri.empty()) return std::nullopt;
  int edge = req.max_edge;
  if (req.policy == RasterPolicy::SoftOnly) {
    if (edge <= 0) edge = kMaxSoftLadderEdge;
    if (edge > kMaxSoftLadderEdge) edge = kMaxSoftLadderEdge;
    // Soft ladder only — no TileSynth (see get_pixels allow_tile_synth).
    return get_pixels(req.uri, edge, req.frame_idx, /*allow_tile_synth=*/false);
  }
  if (req.policy == RasterPolicy::Full) {
    return get_full_pixels(req.uri, edge);
  }
  if (edge <= 0) {
    edge = (req.policy == RasterPolicy::Overview) ? kBatchMaxEdge
                                                  : kMaxSoftLadderEdge;
  }
  // PreferCache / Overview: get_pixels already tries soft then TileSynth.
  return get_pixels(req.uri, edge, req.frame_idx);
}

void Client::request_raster(RasterRequest req, PixelsCallback cb) {
  if (req.uri.empty()) {
    if (cb) {
      executor_.post([cb = std::move(cb)]() mutable {
        cb({}, 0, std::nullopt);
      });
    }
    return;
  }
  int edge = req.max_edge;
  if (req.policy == RasterPolicy::SoftOnly) {
    if (edge <= 0) edge = kMaxSoftLadderEdge;
    request_pixels(std::move(req.uri), edge, std::move(cb), req.frame_idx);
    return;
  }
  if (req.policy == RasterPolicy::Full) {
    request_full_pixels(std::move(req.uri), edge, std::move(cb));
    return;
  }
  if (edge <= 0) {
    edge = kBatchMaxEdge;
  }
  if (edge > kMaxSoftLadderEdge || req.policy == RasterPolicy::Overview) {
    request_overview_pixels(std::move(req.uri), edge, std::move(cb));
  } else {
    request_pixels(std::move(req.uri), edge, std::move(cb), req.frame_idx);
  }
}


void Client::invalidate_tile(std::string_view /*uri*/, int /*scale*/, int /*x*/,
                             int /*y*/) {
  // Store has region-level delete only; single-cell invalidation is a no-op
  // until a per-tile delete lands on Store.
}

void Client::request_tile(std::string uri, int scale, int x, int y,
                          TileCallback cb) {
  // Warm path: same as request_size — serve durable Store hits without waiting
  // on the worker queue (second open felt slow: every cell paid queue latency).
  // Blob read stays here; host JPEG decode still runs on the Executor thread.
  if (cb) {
    if (auto t = get_tile(uri, scale, x, y)) {
      if (debug_enabled()) {
        dbg("request_tile HIT uri=%s scale=%d cell=%d,%d", uri.c_str(), scale, x,
            y);
      }
      executor_.post([cb = std::move(cb), uri, scale, x, y,
                      t = std::move(*t)]() mutable {
        cb(std::move(uri), scale, x, y, std::move(t));
      });
      return;
    }
  }
  if (debug_enabled()) {
    dbg("request_tile QUEUE uri=%s scale=%d cell=%d,%d", uri.c_str(), scale, x,
        y);
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
  // FIFO: every issued cell eventually runs. LIFO starved older archive/grid
  // cells forever under continuous pan/zoom (Galapix saw permanent REQUESTED).
  enqueue(std::move(job), /*front=*/false);
}

void Client::request_tiles(std::string uri, std::vector<TileCoord> coords,
                           TileBatchCallback on_cell) {
  if (coords.empty() || !on_cell) {
    return;
  }
  if (coords.size() == 1) {
    request_tile(std::move(uri), coords[0].scale, coords[0].x, coords[0].y,
                 [on_cell = std::move(on_cell)](std::string, int, int, int,
                                                std::optional<TileBlob> tb) {
                   on_cell(0, std::move(tb));
                 });
    return;
  }
  // Serve durable hits immediately; only enqueue cells that still need encode.
  std::vector<TileCoord> misses;
  misses.reserve(coords.size());
  for (std::size_t i = 0; i < coords.size(); ++i) {
    const auto& c = coords[i];
    if (auto t = get_tile(uri, c.scale, c.x, c.y)) {
      executor_.post([on_cell, i, t = std::move(*t)]() mutable {
        on_cell(i, std::move(t));
      });
    } else {
      // Preserve original indices via a small job per miss (correct completion
      // index). Batch-miss encode path still goes through the worker.
      misses.push_back(c);
      const std::size_t idx = i;
      const int sc = c.scale, tx = c.x, ty = c.y;
      Job job;
      job.kind = JobKind::EnsureTiles;
      job.uri = uri;
      job.tile_scale = sc;
      job.tile_x = tx;
      job.tile_y = ty;
      job.tile_min_scale = sc;
      job.tile_max_scale = sc;
      job.tile_pyramid = false;
      job.tile_cb = [on_cell, idx](std::string, int, int, int,
                                   std::optional<TileBlob> tb) {
        on_cell(idx, std::move(tb));
      };
      enqueue(std::move(job), /*front=*/false);
    }
  }
  if (debug_enabled() && !misses.empty()) {
    dbg("request_tiles uri=%s hits=%zu misses=%zu", uri.c_str(),
        coords.size() - misses.size(), misses.size());
  }
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

void Client::enqueue(Job job, bool front) {
  {
    std::lock_guard lock(mu_);
    if (job.epoch == 0) {
      job.epoch = interest_epoch_;
    }
    // Drop older pending single-cell EnsureTiles for the same uri/scale/x/y so
    // superseded work never reaches a worker (Galapix cannot cancel queued
    // jobs). Applies for both FIFO and LIFO enqueue. Batch jobs
    // (tile_batch non-empty) are left alone — they complete every index.
    if (job.kind == JobKind::EnsureTiles && !job.tile_pyramid &&
        job.tile_batch.empty()) {
      for (auto it = queue_.begin(); it != queue_.end();) {
        if (it->kind == JobKind::EnsureTiles && !it->tile_pyramid &&
            it->tile_batch.empty() && it->uri == job.uri &&
            it->tile_scale == job.tile_scale && it->tile_x == job.tile_x &&
            it->tile_y == job.tile_y) {
          TileCallback cb = std::move(it->tile_cb);
          std::string uri = it->uri;
          int const sc = it->tile_scale;
          int const x = it->tile_x;
          int const y = it->tile_y;
          it = queue_.erase(it);
          if (cb) {
            executor_.post(
                [cb = std::move(cb), uri = std::move(uri), sc, x, y]() mutable {
                  cb(std::move(uri), sc, x, y, std::nullopt);
                });
          }
        } else {
          ++it;
        }
      }
    }
    // Tile pyramids: supersede only the same URI. Do not drop other URIs'
    // pyramids (thumtoo-prepare --tiles queued N jobs and kFocusFullMaxConcurrent=1
    // cancelled N-1 as immediate "miss"). Worker still serializes running
    // FocusFull via focus_full_inflight_ / claim rotation.
    if (job.kind == JobKind::EnsureTiles && job.tile_pyramid) {
      for (auto it = queue_.begin(); it != queue_.end();) {
        if (it->kind == JobKind::EnsureTiles && it->tile_pyramid
            && it->uri == job.uri) {
          reply_cancelled_job(*it);
          it = queue_.erase(it);
        } else {
          ++it;
        }
      }
    }
    if (front) {
      queue_.push_front(std::move(job));
    } else {
      queue_.push_back(std::move(job));
    }
  }
  cv_.notify_one();
}

void Client::ensure_archive_cursor(const std::filesystem::path& archive_path) {
  const std::string key = archive_path.lexically_normal().string();
  {
    std::lock_guard lock(archive_cursor_mu_);
    auto it = archive_cursors_.find(key);
    if (it != archive_cursors_.end() && !it->second.ordered_members.empty()) {
      return;
    }
  }
  // TOC read outside lock (source I/O).
  std::vector<std::string> ordered;
  if (auto toc = read_archive_toc(archive_path)) {
    ordered.reserve(toc->size());
    for (const auto& m : *toc) {
      if (is_likely_image_member_path(m.member_path)) {
        ordered.push_back(m.member_path);
      }
    }
  }
  std::lock_guard lock(archive_cursor_mu_);
  auto& cur = archive_cursors_[key];
  cur.archive_path = archive_path;
  if (cur.ordered_members.empty() && !ordered.empty()) {
    cur.ordered_members = std::move(ordered);
    cur.next_index = 0;
  } else if (!ordered.empty() && cur.ordered_members != ordered) {
    // TOC changed (rare); reset.
    cur.ordered_members = std::move(ordered);
    cur.next_index = 0;
  }
}

std::vector<std::string> Client::plan_and_maybe_advance_cursor(
    const std::filesystem::path& archive_path,
    const std::vector<std::string>& interest_members,
    bool advance_after) {
  ensure_archive_cursor(archive_path);
  std::vector<std::string> ordered;
  std::size_t next_index = 0;
  const std::string key = archive_path.lexically_normal().string();
  {
    std::lock_guard lock(archive_cursor_mu_);
    auto it = archive_cursors_.find(key);
    if (it != archive_cursors_.end()) {
      ordered = it->second.ordered_members;
      next_index = it->second.next_index;
    }
  }
  auto planned = plan_archive_batch_window(ordered, interest_members, next_index,
                                           kBatchWindowMembers);
  if (advance_after && !interest_members.empty()) {
    // Advance past the last interest member that appears in TOC order.
    std::optional<std::size_t> last;
    for (const auto& m : interest_members) {
      if (auto idx = archive_member_toc_index(ordered, m)) {
        if (!last || *idx > *last) last = idx;
      }
    }
    if (last) {
      std::lock_guard lock(archive_cursor_mu_);
      auto it = archive_cursors_.find(key);
      if (it != archive_cursors_.end()) {
        it->second.next_index = *last + 1;
        if (it->second.next_index > it->second.ordered_members.size()) {
          it->second.next_index = it->second.ordered_members.size();
        }
      }
    }
  }
  return planned;
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
    extract_cache_bytes_ -= it->second.bytes.size();
    extract_cache_bytes_ += bytes.size();
    it->second.bytes = std::move(bytes);
    // Move to front (most recently used).
    extract_cache_lru_.erase(it->second.lru_it);
    extract_cache_lru_.push_front(key);
    it->second.lru_it = extract_cache_lru_.begin();
    return;
  }
  // Evict least-recently used until the new entry fits (or cache empty).
  while (!extract_cache_.empty() &&
         extract_cache_bytes_ + bytes.size() > kExtractCacheMaxBytes) {
    const std::string& victim = extract_cache_lru_.back();
    auto vit = extract_cache_.find(victim);
    if (vit != extract_cache_.end()) {
      extract_cache_bytes_ -= vit->second.bytes.size();
      extract_cache_.erase(vit);
    }
    extract_cache_lru_.pop_back();
  }
  // Single entry larger than budget: store it alone (still useful once).
  if (bytes.size() > kExtractCacheMaxBytes) {
    extract_cache_.clear();
    extract_cache_lru_.clear();
    extract_cache_bytes_ = 0;
  }
  extract_cache_lru_.push_front(key);
  ExtractCacheEntry ent;
  ent.bytes = std::move(bytes);
  ent.lru_it = extract_cache_lru_.begin();
  extract_cache_bytes_ += ent.bytes.size();
  extract_cache_.emplace(key, std::move(ent));
}

std::optional<std::vector<std::uint8_t>> Client::extract_cache_get(
    const std::filesystem::path& archive, std::string_view member) const {
  std::lock_guard lock(extract_cache_mu_);
  auto it = extract_cache_.find(extract_cache_key(archive, member));
  if (it == extract_cache_.end()) return std::nullopt;
  // Touch LRU (maps are mutable).
  extract_cache_lru_.erase(it->second.lru_it);
  extract_cache_lru_.push_front(it->first);
  it->second.lru_it = extract_cache_lru_.begin();
  return it->second.bytes;
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

  // Sequential archives (tar, solid RAR/7z): one libarchive/unarr pass for a
  // TOC-ordered window around the interest member, fill extract cache, return
  // the requested member. Avoids N independent full-stream walks for neighbors.
  if (archive_access_class(archive) == ArchiveAccess::Sequential) {
    const std::string member_s(member);
    auto planned =
        plan_and_maybe_advance_cursor(archive, {member_s}, /*advance_after=*/true);
    if (planned.empty()) {
      planned.push_back(member_s);
    }
    auto from_disk = extract_archive_members(archive, planned);
    for (auto& kv : from_disk) {
      extract_cache_put(archive, kv.first, kv.second);
    }
    if (auto it = from_disk.find(member_s); it != from_disk.end()) {
      return it->second;
    }
    // Path equality may differ (./ prefix); fall back to cache get after put.
    if (auto cached = extract_cache_get(archive, member)) {
      return cached;
    }
    return std::nullopt;
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
  // Durable cache (survives process restart) via Store bulk.
  if (store_) {
    if (auto row = store_->get_http_body(key)) {
      const auto now = static_cast<std::int64_t>(
          std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count());
      if (row->fetched_at + kHttpCacheTtlSeconds >= now && !row->data.empty()) {
        std::lock_guard lock(http_cache_mu_);
        if (http_cache_bytes_ + row->data.size() > kHttpCacheMaxBytes) {
          http_cache_.clear();
          http_cache_bytes_ = 0;
        }
        http_cache_bytes_ += row->data.size();
        http_cache_.emplace(key, row->data);
        return row->data;
      }
    }
  }
  auto bytes = http_get_bytes(url, kArchiveMaxMemberUncompressedBytes);
  if (!bytes || bytes->empty()) return std::nullopt;
  using namespace std::chrono;
  const auto now =
      duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
  if (store_) {
    try {
      store_->put_http_body(
          key, std::span<const std::uint8_t>(bytes->data(), bytes->size()),
          static_cast<std::int64_t>(now));
    } catch (...) {
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
        SizeReply reply;
        reply.size = m->size;
        reply.lqip = get_lqip(uri);
        executor_.post([cb = std::move(cb), uri, reply = std::move(reply)]() mutable {
          cb(std::move(uri), std::move(reply));
        });
      }
      return;
    }
  }

  // Ensure locator exists (provisional content) so cache-first browse sees it.
  Job job;
  job.kind = JobKind::ProbeSize;
  job.uri = std::move(uri);
  job.size_cb = std::move(cb);
  // Size is the layout pass — run ahead of EnsurePixels / tiles / LQIP.
  enqueue(std::move(job), /*front=*/true);
}

size_t Client::prepare_paths(const std::vector<std::filesystem::path>& paths,
                             SizeCallback on_each) {
  // STORE_ONLY: no provisional locators — enqueue request_size for unknowns.
  // Collect URIs that need a probe first so callers know the job total before
  // any completion callbacks fire (worker may run concurrently).
  struct Pending {
    std::string uri;
    bool need_register = false;
    Client::LocatorRow loc;
  };
  std::vector<Pending> pending;
  pending.reserve(paths.size());

  auto enqueue_plain = [&](const std::filesystem::path& abs) {
    const auto uri = file_uri_from_path(abs);
    Pending item;
    item.uri = uri;
    item.need_register = true;
    item.loc.uri = uri;
    item.loc.content_id = make_provisional_id();
    item.loc.outer_path = abs.string();
    item.loc.size = file_size_bytes(abs);
    item.loc.mtime_ns = file_mtime_ns(abs);
    pending.push_back(std::move(item));
  };

  auto enqueue_archive_member = [&](const std::filesystem::path& abs,
                                    const Client::ArchiveEntryRow& entry) {
    const auto uri = archive_uri(abs, entry.member_path);
    Pending item;
    item.uri = uri;
    item.need_register = true;
    item.loc.uri = uri;
    item.loc.content_id = make_provisional_id();
    item.loc.outer_path = abs.string();
    item.loc.member_path = entry.member_path;
    item.loc.size = entry.uncompressed_size;
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
      auto count = document_page_count(abs, DocumentKind::Pdf);
      if (count && *count > 0) {
        // Cap prepare volume so huge books do not flood the queue.
        constexpr int kMaxPreparePages = 512;
        const int n = std::min(*count, kMaxPreparePages);
        for (int page = 1; page <= n; ++page) {
          const auto uri = pdf_page_uri(abs, page);
          Pending item;
          item.uri = uri;
          item.need_register = true;
          item.loc.uri = uri;
          item.loc.content_id = make_provisional_id();
          item.loc.outer_path = abs.string();
          item.loc.member_path = std::to_string(page);
          item.loc.size = file_size_bytes(abs);
          item.loc.mtime_ns = file_mtime_ns(abs);
          pending.push_back(std::move(item));
        }
      }
      continue;
    }

    if (is_likely_djvu_path(abs)) {
      auto count = document_page_count(abs, DocumentKind::Djvu);
      if (count && *count > 0) {
        constexpr int kMaxPreparePages = 512;
        const int n = std::min(*count, kMaxPreparePages);
        for (int page = 1; page <= n; ++page) {
          const auto uri = djvu_page_uri(abs, page);
          Pending item;
          item.uri = uri;
          item.need_register = true;
          item.loc.uri = uri;
          item.loc.content_id = make_provisional_id();
          item.loc.outer_path = abs.string();
          item.loc.member_path = std::to_string(page);
          item.loc.size = file_size_bytes(abs);
          item.loc.mtime_ns = file_mtime_ns(abs);
          pending.push_back(std::move(item));
        }
      }
      continue;
    }


    if (is_likely_epub_path(abs)) {
      const auto layout = default_epub_layout();
      auto count = document_page_count(abs, DocumentKind::Epub, &layout);
      if (count && *count > 0) {
        constexpr int kMaxPreparePages = 512;
        const int n = std::min(*count, kMaxPreparePages);
        for (int page = 1; page <= n; ++page) {
          const auto uri = epub_page_uri(abs, page, layout);
          Pending item;
          item.uri = uri;
          item.need_register = true;
          item.loc.uri = uri;
          item.loc.content_id = make_provisional_id();
          item.loc.outer_path = abs.string();
          item.loc.member_path = std::to_string(page);
          item.loc.size = file_size_bytes(abs);
          item.loc.mtime_ns = file_mtime_ns(abs);
          pending.push_back(std::move(item));
        }
      }
      continue;
    }

    enqueue_plain(abs);
  }

  for (auto& item : pending) {
    (void)item.need_register;  // Store probe registers on size job
    request_size(item.uri, on_each);
  }
  return pending.size();
}


std::vector<Client::ArchiveEntryRow> Client::get_archive_entries(
    std::string_view archive_uri) const {
  std::vector<Client::ArchiveEntryRow> rows;

  // Store container_member TOC.
  auto resolve_container = [&]() -> std::optional<std::int64_t> {
    if (auto loc = store_->find_locator(archive_uri)) {
      if (loc->blob_id) return *loc->blob_id;
    }
    if (auto arch = parse_archive_uri(archive_uri)) {
      const auto root = thumtoo::archive_uri(arch->archive_path);
      if (auto loc = store_->find_locator(root)) {
        if (loc->blob_id) return *loc->blob_id;
      }
      const auto file_uri =
          file_uri_from_path(arch->archive_path.lexically_normal());
      if (auto loc = store_->find_locator(file_uri)) {
        if (loc->blob_id) return *loc->blob_id;
      }
    }
    return std::nullopt;
  };

  auto cid = resolve_container();
  if (!cid) return rows;
  try {
    auto members = store_->list_container_members(*cid);
    rows.reserve(members.size());
    for (const auto& m : members) {
      if (m.is_directory) continue;
      Client::ArchiveEntryRow r;
      r.archive_uri = std::string(archive_uri);
      r.member_path = m.member_path;
      r.uncompressed_size = m.uncompressed_size;
      rows.push_back(std::move(r));
    }
  } catch (...) {
  }
  return rows;
}

std::vector<Client::ArchiveEntryRow> Client::refresh_archive_toc(
    const std::filesystem::path& archive_path) {
  auto toc = read_archive_toc(archive_path);
  if (!toc) return {};
  const auto uri = archive_uri(archive_path);
  std::vector<Client::ArchiveEntryRow> rows;
  rows.reserve(toc->size());
  for (const auto& m : *toc) {
    Client::ArchiveEntryRow r;
    r.archive_uri = uri;
    r.member_path = m.member_path;
    r.uncompressed_size = m.uncompressed_size;
    rows.push_back(std::move(r));
  }
  // Store container_member TOC (no member hash until probe/read).
  if (auto cid = ensure_store_container_blob(archive_path)) {
    try {
      std::vector<Store::ContainerMemberRow> members;
      members.reserve(rows.size());
      for (const auto& r : rows) {
        Store::ContainerMemberRow cm;
        cm.container_id = *cid;
        cm.member_path = r.member_path;
        cm.is_directory = false;
        cm.uncompressed_size = r.uncompressed_size;
        members.push_back(std::move(cm));
      }
      store_->replace_container_members(*cid, members);
    } catch (const std::exception& ex) {
      if (debug_enabled()) {
        dbg("refresh_archive_toc Store mirror failed: %s", ex.what());
      }
    }
  }
  return rows;
}

std::optional<int> Client::refresh_document_index(
    const std::filesystem::path& path, DocumentKind kind,
    const EpubLayout* layout) {
  if (path.empty()) return std::nullopt;
  std::error_code ec;
  const auto abs = std::filesystem::weakly_canonical(path, ec);
  const auto& use = ec ? path : abs;
  if (!std::filesystem::is_regular_file(use, ec)) return std::nullopt;

  EpubLayout epub_layout = layout ? *layout : default_epub_layout();
  std::string layout_key;
  std::optional<int> count;
  switch (kind) {
    case DocumentKind::Pdf:
      count = thumtoo::pdf_page_count(use);
      break;
    case DocumentKind::Djvu:
      count = thumtoo::djvu_page_count(use);
      break;
    case DocumentKind::Epub:
      layout_key = format_epub_layout_params(epub_layout);
      count = thumtoo::epub_page_count(use, epub_layout);
      break;
  }
  if (!count || *count <= 0) return std::nullopt;
  (void)layout_key;
  return count;
}

std::optional<int> Client::document_page_count(
    const std::filesystem::path& path, DocumentKind kind,
    const EpubLayout* layout) {
  if (path.empty()) return std::nullopt;
  std::error_code ec;
  const auto abs = std::filesystem::weakly_canonical(path, ec);
  const auto& use = ec ? path : abs;
  if (!std::filesystem::is_regular_file(use, ec)) return std::nullopt;

  EpubLayout epub_layout = layout ? *layout : default_epub_layout();
  std::string layout_key;
  if (kind == DocumentKind::Epub) {
    layout_key = format_epub_layout_params(epub_layout);
  }
  return refresh_document_index(use, kind, layout ? layout : &epub_layout);
}


std::optional<int> Client::pdf_page_count(const std::filesystem::path& path,
                                         PdfBackend backend) {
  return thumtoo::pdf_page_count(path, backend);
}

std::optional<Client::PdfPageRaster> Client::pdf_rasterize_page(
    const std::filesystem::path& path, int page_1based, int max_edge,
    PdfBackend backend) {
  auto r = thumtoo::pdf_rasterize_page(path, page_1based, max_edge, backend);
  if (!r) return std::nullopt;
  PdfPageRaster out;
  out.width = r->width;
  out.height = r->height;
  out.rgb = std::move(r->rgb);
  return out;
}

std::string Client::pdf_page_uri(const std::filesystem::path& path,
                                 int page_1based, PdfBackend backend) {
  return thumtoo::pdf_page_uri(path, page_1based, backend);
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


std::uint64_t Client::interest_epoch() const {
  std::lock_guard lock(mu_);
  return interest_epoch_;
}

Client::QueueStats Client::queue_stats() const {
  std::lock_guard lock(mu_);
  QueueStats s;
  s.pending = queue_.size();
  s.inflight = inflight_;
  s.focus_full_inflight = focus_full_inflight_;
  s.interest_epoch = interest_epoch_;
  return s;
}

void Client::reply_cancelled_job(Job& job) {
  if (job.kind == JobKind::ProbeSize && job.size_cb) {
    auto cb = std::move(job.size_cb);
    auto uri = job.uri;
    executor_.post([cb = std::move(cb), uri = std::move(uri)]() mutable {
      cb(std::move(uri), SizeReply{});
    });
  } else if (job.kind == JobKind::EnsurePixels && job.pixels_cb) {
    auto cb = std::move(job.pixels_cb);
    auto uri = job.uri;
    const int edge = job.max_edge;
    executor_.post([cb = std::move(cb), uri = std::move(uri), edge]() mutable {
      cb(std::move(uri), edge, std::nullopt);
    });
  } else if (job.kind == JobKind::EnsureTiles) {
    if (!job.tile_batch.empty() && job.tile_batch_cb) {
      auto cb = std::move(job.tile_batch_cb);
      const std::size_t n = job.tile_batch.size();
      executor_.post([cb = std::move(cb), n]() mutable {
        for (std::size_t i = 0; i < n; ++i) {
          cb(i, std::nullopt);
        }
      });
    } else if (job.tile_cb) {
      auto cb = std::move(job.tile_cb);
      auto uri = job.uri;
      const int sc = job.tile_scale;
      const int x = job.tile_x;
      const int y = job.tile_y;
      executor_.post(
          [cb = std::move(cb), uri = std::move(uri), sc, x, y]() mutable {
            cb(std::move(uri), sc, x, y, std::nullopt);
          });
    }
  }
  // EnsureLqip has no host callback.
}

std::uint64_t Client::bump_interest_epoch() {
  std::vector<Job> dropped;
  std::uint64_t live = 0;
  {
    std::lock_guard lock(mu_);
    ++interest_epoch_;
    if (interest_epoch_ == 0) {
      interest_epoch_ = 1;
    }
    live = interest_epoch_;
    for (auto it = queue_.begin(); it != queue_.end();) {
      if (it->epoch != 0 && it->epoch < live) {
        dropped.push_back(std::move(*it));
        it = queue_.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (auto& j : dropped) {
    reply_cancelled_job(j);
  }
  return live;
}

std::size_t Client::cancel_pending() {
  std::vector<Job> dropped;
  {
    std::lock_guard lock(mu_);
    dropped.reserve(queue_.size());
    for (auto& j : queue_) {
      dropped.push_back(std::move(j));
    }
    queue_.clear();
  }
  for (auto& j : dropped) {
    reply_cancelled_job(j);
  }
  return dropped.size();
}

std::size_t Client::cancel_uri(std::string_view uri) {
  std::vector<Job> dropped;
  {
    std::lock_guard lock(mu_);
    for (auto it = queue_.begin(); it != queue_.end();) {
      if (it->uri == uri) {
        dropped.push_back(std::move(*it));
        it = queue_.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (auto& j : dropped) {
    reply_cancelled_job(j);
  }
  return dropped.size();
}

Client::PurgeStats Client::purge_uri(std::string_view uri, bool dry_run) {
  PurgeStats out;
  if (!store_ || uri.empty()) return out;
  auto st = store_->forget_uri(uri, dry_run);
  if (st.locator_removed) out.removed_uris.emplace_back(uri);
  if (st.blob_purged) out.purged_content_ids.emplace_back(std::string(uri));
  out.tiles_deleted = st.tiles_deleted;
  return out;
}

Client::PurgeStats Client::purge_path(const std::filesystem::path& path,
                                      bool dry_run) {
  PurgeStats out;
  if (!store_ || path.empty()) return out;
  std::error_code ec;
  auto abs = std::filesystem::absolute(path, ec);
  if (ec) abs = path;
  const auto file_uri = file_uri_from_path(abs.lexically_normal());
  auto st = store_->forget_uri(file_uri, dry_run);
  if (st.locator_removed) out.removed_uris.push_back(file_uri);
  if (st.blob_purged) out.purged_content_ids.push_back(file_uri);
  out.tiles_deleted = st.tiles_deleted;
  // Also drop archive-root style URI for the same path.
  const auto arch = archive_uri(abs);
  if (arch != file_uri) {
    auto st2 = store_->forget_uri(arch, dry_run);
    if (st2.locator_removed) out.removed_uris.push_back(arch);
    out.tiles_deleted += st2.tiles_deleted;
  }
  return out;
}

Client::PurgeStats Client::purge_uri_prefix(std::string_view uri_prefix,
                                           bool dry_run) {
  PurgeStats out;
  if (!store_ || uri_prefix.empty()) return out;
  auto st = store_->forget_uri_prefix(uri_prefix, dry_run);
  out.tiles_deleted = st.tiles_deleted;
  out.levels_deleted = st.locators_removed;  // locators removed (field name is legacy)
  if (st.blobs_purged > 0) {
    out.purged_content_ids.resize(static_cast<std::size_t>(st.blobs_purged));
  }
  if (st.locators_removed > 0) {
    out.removed_uris.resize(static_cast<std::size_t>(st.locators_removed));
  }
  return out;
}

std::uint64_t Client::set_interest(std::vector<InterestItem> items) {
  // Cancel stale work first so the new snapshot owns the queue.
  const std::uint64_t epoch = bump_interest_epoch();
  if (debug_enabled()) {
    dbg("set_interest epoch=%llu items=%zu queue_before cancel",
        static_cast<unsigned long long>(epoch), items.size());
  }

  // Deduplicate by uri, keep highest role and max edge.
  struct Agg {
    int edge = 0;
    InterestRole role = InterestRole::Speculative;
  };
  std::unordered_map<std::string, Agg> by_uri;
  by_uri.reserve(items.size());
  for (auto& it : items) {
    if (it.uri.empty()) continue;
    auto& a = by_uri[it.uri];
    if (static_cast<int>(it.role) > static_cast<int>(a.role)) {
      a.role = it.role;
    }
    if (it.target_long_edge > a.edge) {
      a.edge = it.target_long_edge;
    }
  }

  // Primary first, then Near, then Speculative — enqueue front for Primary.
  std::vector<std::pair<std::string, Agg>> ordered;
  ordered.reserve(by_uri.size());
  for (auto& kv : by_uri) {
    ordered.emplace_back(kv.first, kv.second);
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const auto& a, const auto& b) {
              return static_cast<int>(a.second.role) >
                     static_cast<int>(b.second.role);
            });

  // Speculative work only when the queue is not already busy with
  // Primary/Near (PIXEL_PIPELINE idle policy).
  std::size_t queue_len = 0;
  {
    std::lock_guard lock(mu_);
    queue_len = queue_.size();
  }

  for (const auto& [uri, agg] : ordered) {
    if (agg.role == InterestRole::Speculative &&
        queue_len >= kSpeculativeEnqueueWhenQueueBelow) {
      continue;
    }
    int edge = agg.edge > 0 ? agg.edge : kBatchMaxEdge;
    if (edge > kBatchMaxEdge) {
      edge = kBatchMaxEdge;
    }
    // FastBatch overview for all roles (Primary sorted first into the queue).
    request_overview_pixels(uri, edge, {});
    ++queue_len;  // approximate; request may no-op on cache hit
    // FocusFull: Primary also builds the durable tile pyramid (Q2 base).
    if (agg.role == InterestRole::Primary) {
      request_tile_pyramid(uri, /*min_scale=*/0, /*max_scale=*/-1, {});
      ++queue_len;
    }
  }
  return epoch;
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
      // Skip jobs cancelled by epoch while they sat in the queue.
      std::vector<Job> stale_jobs;
      while (!queue_.empty()) {
        if (queue_.front().epoch != 0 &&
            queue_.front().epoch < interest_epoch_) {
          stale_jobs.push_back(std::move(queue_.front()));
          queue_.erase(queue_.begin());
          continue;
        }
        break;
      }
      if (!stale_jobs.empty()) {
        lock.unlock();
        for (auto& s : stale_jobs) {
          reply_cancelled_job(s);
        }
        lock.lock();
        continue;
      }
      if (queue_.empty()) {
        continue;
      }
      // Claim priority: interactive EnsureTiles (Galapix zoom) first, then
      // ProbeSize (layout), then FIFO. Blind ProbeSize-first starved tile
      // jobs under size floods and left viewers stuck on coarse stand-ins.
      {
        auto prefer = queue_.end();
        for (auto it = queue_.begin(); it != queue_.end(); ++it) {
          if (it->kind == JobKind::EnsureTiles && !it->tile_pyramid) {
            prefer = it;
            break;
          }
        }
        if (prefer == queue_.end()) {
          for (auto it = queue_.begin(); it != queue_.end(); ++it) {
            if (it->kind == JobKind::ProbeSize) {
              prefer = it;
              break;
            }
          }
        }
        if (prefer != queue_.end() && prefer != queue_.begin()) {
          Job job = std::move(*prefer);
          queue_.erase(prefer);
          queue_.push_front(std::move(job));
        }
      }
      // Do not claim a new FocusFull while one is already running (cap).
      if (queue_.front().kind == JobKind::EnsureTiles &&
          queue_.front().tile_pyramid &&
          focus_full_inflight_ >= kFocusFullMaxConcurrent) {
        // Rotate to the back so other work can proceed.
        Job blocked = std::move(queue_.front());
        queue_.erase(queue_.begin());
        queue_.push_back(std::move(blocked));
        continue;
      }
      if (!queue_.front().uri.empty()) ++inflight_;
      if (queue_.front().kind == JobKind::EnsureTiles &&
          queue_.front().tile_pyramid) {
        ++focus_full_inflight_;
      }
      single = std::move(queue_.front());
      queue_.erase(queue_.begin());
      if (stop_ && single.uri.empty()) return;

      // Coalesce same-archive jobs of the same kind into one libarchive pass.
      if (!single.uri.empty() &&
          (single.kind == JobKind::ProbeSize ||
           single.kind == JobKind::EnsureTiles ||
           single.kind == JobKind::EnsurePixels ||
           single.kind == JobKind::EnsureLqip)) {
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
            // Do not mix interest epochs in one extract pass.
            if (it->epoch != batch.front().epoch) {
              ++it;
              continue;
            }
            ++inflight_;
            if (it->kind == JobKind::EnsureTiles && it->tile_pyramid) {
              ++focus_full_inflight_;
            }
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

      // Warm paths: never open the archive when DB/blob already has the answer.
      // Previously we always extract_archive_members() first, so a fully-cached
      // RAR gallery paid full decompress cost on every open (tiles, levels, size).
      std::vector<char> need_extract(batch.size(), 1);
      if (batch_kind == JobKind::EnsureTiles) {
        for (size_t i = 0; i < batch.size(); ++i) {
          auto& j = batch[i];
          if (!j.tile_pyramid &&
              get_tile(j.uri, j.tile_scale, j.tile_x, j.tile_y)) {
            need_extract[i] = 0;
            try {
              handle_ensure_tiles(j, std::nullopt);
            } catch (...) {
            }
            std::lock_guard lock(mu_);
            --inflight_;
          }
        }
      } else if (batch_kind == JobKind::EnsurePixels) {
        for (size_t i = 0; i < batch.size(); ++i) {
          auto& j = batch[i];
          if (get_pixels(j.uri, j.max_edge, j.frame_idx)) {
            need_extract[i] = 0;
            try {
              handle_ensure_pixels(j, std::nullopt);
            } catch (...) {
            }
            std::lock_guard lock(mu_);
            --inflight_;
          }
        }
      } else if (batch_kind == JobKind::ProbeSize) {
        for (size_t i = 0; i < batch.size(); ++i) {
          auto& j = batch[i];
          // Size already known → handle_probe_size returns without source bytes.
          if (auto sz = get_size(j.uri); sz && sz->width > 0 && sz->height > 0) {
            need_extract[i] = 0;
            try {
              handle_probe_size(j, std::nullopt);
            } catch (...) {
            }
            std::lock_guard lock(mu_);
            --inflight_;
          }
        }
      }

      // Prefer in-process extract cache (filled by a prior pass) so we do not
      // open a solid RAR twice and so post-extract encode can run in parallel.
      // FastBatch cursor: TOC-order + window cap so scroll does not open the
      // whole archive for an unbounded interest set in one pass.
      std::unordered_map<std::string, std::vector<std::uint8_t>> extracted;
      std::vector<std::string> interest_need;
      interest_need.reserve(batch.size());
      for (size_t i = 0; i < batch.size(); ++i) {
        if (!need_extract[i]) continue;
        if (i >= members.size() || members[i].empty()) continue;
        if (auto cached = extract_cache_get(archive_path, members[i])) {
          extracted.emplace(members[i], std::move(*cached));
        } else {
          interest_need.push_back(members[i]);
        }
      }

      std::vector<std::string> extract_members;
      if (!interest_need.empty() && !archive_path.empty()) {
        // Plan TOC-ordered window; advance cursor past last extracted on success.
        extract_members =
            plan_and_maybe_advance_cursor(archive_path, interest_need, false);
        // Always include every interest member that is inside the planned
        // window; if planner returned empty (no TOC yet), fall back to interest.
        if (extract_members.empty()) {
          extract_members = interest_need;
          if (extract_members.size() >
              static_cast<size_t>(kBatchWindowMembers)) {
            extract_members.resize(static_cast<size_t>(kBatchWindowMembers));
          }
        }
        auto from_disk = extract_archive_members(archive_path, extract_members);
        for (auto& kv : from_disk) {
          extract_cache_put(archive_path, kv.first, kv.second);
          extracted[kv.first] = std::move(kv.second);
        }
        if (!from_disk.empty()) {
          // Advance cursor using the planned list (even if some members missed).
          (void)plan_and_maybe_advance_cursor(archive_path, extract_members,
                                              true);
        }
      }

      // Build work list: one entry per remaining job with optional preextracted.
      struct BatchItem {
        size_t index = 0;
        std::optional<std::vector<std::uint8_t>> pre;
      };
      std::vector<BatchItem> items;
      items.reserve(batch.size());
      for (size_t i = 0; i < batch.size(); ++i) {
        if (!need_extract[i]) continue;
        BatchItem it;
        it.index = i;
        if (i < members.size() && !members[i].empty()) {
          if (auto found = extracted.find(members[i]); found != extracted.end()) {
            it.pre = found->second;  // copy: parallel workers may share source
          }
        }
        items.push_back(std::move(it));
      }

      // One archive batch used to run every pyramid on *this* worker after the
      // sequential extract — other pool threads sat idle (low CPU, long wall).
      // Parallelize encode/probe work; extract stays single-threaded above.
      auto run_one = [&](BatchItem& it) {
        try {
          Job& j = batch[it.index];
          if (batch_kind == JobKind::ProbeSize)
            handle_probe_size(j, it.pre);
          else if (batch_kind == JobKind::EnsureTiles)
            handle_ensure_tiles(j, it.pre);
          else if (batch_kind == JobKind::EnsurePixels)
            handle_ensure_pixels(j, it.pre);
          else if (batch_kind == JobKind::EnsureLqip)
            handle_ensure_lqip(j);
        } catch (...) {
        }
        {
          std::lock_guard lock(mu_);
          --inflight_;
          if (batch[it.index].kind == JobKind::EnsureTiles &&
              batch[it.index].tile_pyramid && focus_full_inflight_ > 0) {
            --focus_full_inflight_;
          }
        }
      };

      if (items.size() <= 1) {
        for (auto& it : items) run_one(it);
      } else {
        std::atomic<std::size_t> next{0};
        const unsigned helpers = std::min(
            static_cast<unsigned>(items.size()),
            std::max(1u, static_cast<unsigned>(workers_.size())));
        std::vector<std::thread> pool;
        pool.reserve(helpers);
        for (unsigned t = 0; t < helpers; ++t) {
          pool.emplace_back([&] {
            for (;;) {
              const std::size_t k = next.fetch_add(1, std::memory_order_relaxed);
              if (k >= items.size()) return;
              run_one(items[k]);
            }
          });
        }
        for (auto& th : pool) th.join();
      }
      continue;
    }

    if (single.uri.empty()) continue;
    {
      std::lock_guard lock(mu_);
      if (stop_) {
        // Shutdown: do not start new encode work.
        --inflight_;
        if (single.kind == JobKind::EnsureTiles && single.tile_pyramid &&
            focus_full_inflight_ > 0) {
          --focus_full_inflight_;
        }
        continue;
      }
    }
    try {
      if (single.kind == JobKind::ProbeSize) handle_probe_size(single);
      else if (single.kind == JobKind::EnsurePixels) handle_ensure_pixels(single);
      else if (single.kind == JobKind::EnsureTiles) handle_ensure_tiles(single);
      else if (single.kind == JobKind::EnsureLqip) handle_ensure_lqip(single);
    } catch (...) {
      // Always release inflight_; status stays pending/failed for retry.
    }
    {
      std::lock_guard lock(mu_);
      --inflight_;
      if (single.kind == JobKind::EnsureTiles && single.tile_pyramid) {
        if (focus_full_inflight_ > 0) {
          --focus_full_inflight_;
        }
      }
    }
  }
}


void Client::request_lqip(std::string uri) {
  if (uri.empty()) return;
  if (get_lqip(uri)) return;
  // Last-resort job: ensure_lqip may still full-decode the source if no
  // soft/tiles exist yet. Prefer opportunistic fill from ensure_pixels/tiles.
  // Keep at back of queue so ProbeSize / EnsureTiles stay ahead.
  Job job;
  job.kind = JobKind::EnsureLqip;
  job.uri = std::move(uri);
  {
    std::lock_guard lock(mu_);
    queue_.push_back(std::move(job));
  }
  cv_.notify_one();
}

void Client::handle_ensure_lqip(Job& job) {
  // Decode/Handsum only — must not run on the GUI thread.
  (void)ensure_lqip(job.uri);
}


void Client::handle_probe_size_store(Job& job) {
  auto reply_empty = [&]() {
    if (!job.size_cb) return;
    auto cb = std::move(job.size_cb);
    auto uri = job.uri;
    executor_.post([cb = std::move(cb), uri = std::move(uri)]() mutable {
      cb(std::move(uri), SizeReply{});
    });
  };
  // Cache-only LQIP on the size reply (empty on cold probe). Do not enqueue
  // EnsureLqip here: encode_lqip_for_uri re-thumbnails the source and is too
  // expensive for a path that only needed dimensions. LQIP is filled
  // opportunistically when soft/tiles already decoded (ensure_pixels) or via
  // an explicit host ensure_lqip / request_lqip.
  auto reply_size = [&](Size sz) {
    if (!job.size_cb) return;
    auto cb = std::move(job.size_cb);
    auto uri = job.uri;
    SizeReply reply;
    reply.size = sz;
    reply.lqip = get_lqip(uri);
    executor_.post([cb = std::move(cb), uri = std::move(uri),
                    reply = std::move(reply)]() mutable {
      cb(std::move(uri), std::move(reply));
    });
  };
  if (!store_) {
    reply_empty();
    return;
  }
  // Cache hit on Store (includes PDF page size via meta_from_store).
  if (auto sm = meta_from_store(job.uri); sm && sm->size) {
    reply_size(*sm->size);
    return;
  }

  try {
    if (auto pdf = parse_pdf_uri(job.uri)) {
      if (!std::filesystem::is_regular_file(pdf->pdf_path)) {
        reply_empty();
        return;
      }
      auto layout =
          pdf_page_layout_size(pdf->pdf_path, pdf->page, pdf->backend);
      if (!layout) {
        reply_empty();
        return;
      }
      const auto hex = sha256_file_hex(pdf->pdf_path);
      if (hex.empty()) {
        reply_empty();
        return;
      }
      auto digest = Store::parse_sha256_digest(hex);
      if (!digest) {
        reply_empty();
        return;
      }
      const auto byte_size = file_size_bytes(pdf->pdf_path);
      const auto mtime = file_mtime_ns(pdf->pdf_path);
      std::int64_t blob_id = 0;
      if (auto existing =
              store_->find_blob_by_hash(HashAlgoId::Sha256, *digest)) {
        blob_id = *existing;
        if (byte_size) store_->set_blob_size(blob_id, *byte_size);
        store_->set_blob_status(blob_id, BlobStatus::Ok);
      } else {
        blob_id = store_->insert_blob(byte_size, BlobStatus::Ok);
        store_->put_hash(blob_id, HashAlgoId::Sha256, *digest);
      }
      store_->upsert_locator(job.uri, blob_id, byte_size, mtime,
                             pdf->pdf_path.string(), std::to_string(pdf->page));
      const auto media_id = store_->ensure_document_media(blob_id, {});
      (void)store_->ensure_page_region(media_id, pdf->page);
      // Stash page pixel size on document media for meta_from_store (last page
      // wins for multi-page; page-specific size still comes from layout below).
      store_->set_media_size(media_id, layout->width, layout->height);
      reply_size(*layout);
      return;
    }

    if (auto dj = parse_djvu_uri(job.uri)) {
      if (!std::filesystem::is_regular_file(dj->djvu_path)) {
        reply_empty();
        return;
      }
      auto layout = djvu_page_layout_size(dj->djvu_path, dj->page);
      if (!layout) {
        reply_empty();
        return;
      }
      const auto hex = sha256_file_hex(dj->djvu_path);
      if (hex.empty()) {
        reply_empty();
        return;
      }
      auto digest = Store::parse_sha256_digest(hex);
      if (!digest) {
        reply_empty();
        return;
      }
      const auto byte_size = file_size_bytes(dj->djvu_path);
      const auto mtime = file_mtime_ns(dj->djvu_path);
      std::int64_t blob_id = 0;
      if (auto existing =
              store_->find_blob_by_hash(HashAlgoId::Sha256, *digest)) {
        blob_id = *existing;
        if (byte_size) store_->set_blob_size(blob_id, *byte_size);
        store_->set_blob_status(blob_id, BlobStatus::Ok);
      } else {
        blob_id = store_->insert_blob(byte_size, BlobStatus::Ok);
        store_->put_hash(blob_id, HashAlgoId::Sha256, *digest);
      }
      store_->upsert_locator(job.uri, blob_id, byte_size, mtime,
                             dj->djvu_path.string(), std::to_string(dj->page));
      const auto media_id = store_->ensure_document_media(blob_id, {});
      (void)store_->ensure_page_region(media_id, dj->page);
      store_->set_media_size(media_id, layout->width, layout->height);
      reply_size(*layout);
      return;
    }

    if (auto ep = parse_epub_uri(job.uri)) {
      if (!std::filesystem::is_regular_file(ep->epub_path)) {
        reply_empty();
        return;
      }
      auto layout =
          epub_page_layout_size(ep->epub_path, ep->page, ep->layout);
      if (!layout) {
        reply_empty();
        return;
      }
      const auto hex = sha256_file_hex(ep->epub_path);
      if (hex.empty()) {
        reply_empty();
        return;
      }
      auto digest = Store::parse_sha256_digest(hex);
      if (!digest) {
        reply_empty();
        return;
      }
      const auto byte_size = file_size_bytes(ep->epub_path);
      const auto mtime = file_mtime_ns(ep->epub_path);
      std::int64_t blob_id = 0;
      if (auto existing =
              store_->find_blob_by_hash(HashAlgoId::Sha256, *digest)) {
        blob_id = *existing;
        if (byte_size) store_->set_blob_size(blob_id, *byte_size);
        store_->set_blob_status(blob_id, BlobStatus::Ok);
      } else {
        blob_id = store_->insert_blob(byte_size, BlobStatus::Ok);
        store_->put_hash(blob_id, HashAlgoId::Sha256, *digest);
      }
      store_->upsert_locator(job.uri, blob_id, byte_size, mtime,
                             ep->epub_path.string(), std::to_string(ep->page));
      const auto media_id = store_->ensure_document_media(blob_id, {});
      (void)store_->ensure_page_region(media_id, ep->page);
      store_->set_media_size(media_id, layout->width, layout->height);
      reply_size(*layout);
      return;
    }

    if (auto arch = parse_archive_uri(job.uri)) {
      if (arch->member_path.empty()) {
        reply_empty();
        return;
      }
      auto bytes = member_bytes(arch->archive_path, arch->member_path);
      if (!bytes || bytes->empty()) {
        reply_empty();
        return;
      }
      auto probe = probe_image_buffer(bytes->data(), bytes->size(),
                                      format_from_member(arch->member_path));
      if (!probe) {
        reply_empty();
        return;
      }
      const auto hex = sha256_bytes_hex(bytes->data(), bytes->size());
      if (hex.empty()) {
        reply_empty();
        return;
      }
      auto digest = Store::parse_sha256_digest(hex);
      if (!digest) {
        reply_empty();
        return;
      }
      const auto byte_size =
          static_cast<std::int64_t>(bytes->size());
      std::int64_t blob_id = 0;
      if (auto existing =
              store_->find_blob_by_hash(HashAlgoId::Sha256, *digest)) {
        blob_id = *existing;
        store_->set_blob_size(blob_id, byte_size);
        store_->set_blob_status(blob_id, BlobStatus::Ok);
      } else {
        blob_id = store_->insert_blob(byte_size, BlobStatus::Ok);
        store_->put_hash(blob_id, HashAlgoId::Sha256, *digest);
      }
      store_->upsert_locator(job.uri, blob_id, byte_size, {},
                             arch->archive_path.string(), arch->member_path);
      (void)store_->ensure_image_media(blob_id, probe->size.width,
                                       probe->size.height);
      if (auto cid = ensure_store_container_blob(arch->archive_path)) {
        store_->upsert_container_member(*cid, arch->member_path, false,
                                        byte_size, blob_id);
        store_->set_container_member_blob(*cid, arch->member_path, blob_id);
      }
      reply_size(probe->size);
      return;
    }

    if (is_http_uri(job.uri)) {
      if (!http_fetch_available()) {
        reply_empty();
        return;
      }
      auto bytes = fetch_http_cached(job.uri);
      if (!bytes || bytes->empty()) {
        reply_empty();
        return;
      }
      auto probe = probe_image_buffer(bytes->data(), bytes->size(), "unknown");
      if (!probe) {
        reply_empty();
        return;
      }
      const auto hex = sha256_bytes_hex(bytes->data(), bytes->size());
      if (hex.empty()) {
        reply_empty();
        return;
      }
      auto digest = Store::parse_sha256_digest(hex);
      if (!digest) {
        reply_empty();
        return;
      }
      const auto byte_size = static_cast<std::int64_t>(bytes->size());
      std::int64_t blob_id = 0;
      if (auto existing =
              store_->find_blob_by_hash(HashAlgoId::Sha256, *digest)) {
        blob_id = *existing;
        store_->set_blob_size(blob_id, byte_size);
        store_->set_blob_status(blob_id, BlobStatus::Ok);
      } else {
        blob_id = store_->insert_blob(byte_size, BlobStatus::Ok);
        store_->put_hash(blob_id, HashAlgoId::Sha256, *digest);
      }
      store_->upsert_locator(job.uri, blob_id, byte_size, {});
      (void)store_->ensure_image_media(blob_id, probe->size.width,
                                       probe->size.height);
      reply_size(probe->size);
      return;
    }

    auto path = path_from_file_uri(job.uri);
    if (!path || !std::filesystem::is_regular_file(*path)) {
      reply_empty();
      return;
    }
    auto probe = probe_image_file(*path);
    if (!probe) {
      reply_empty();
      return;
    }
    const auto hex = sha256_file_hex(*path);
    if (hex.empty()) {
      reply_empty();
      return;
    }
    auto digest = Store::parse_sha256_digest(hex);
    if (!digest) {
      reply_empty();
      return;
    }
    const auto byte_size = file_size_bytes(*path);
    const auto mtime = file_mtime_ns(*path);
    std::int64_t blob_id = 0;
    if (auto existing =
            store_->find_blob_by_hash(HashAlgoId::Sha256, *digest)) {
      blob_id = *existing;
      if (byte_size) store_->set_blob_size(blob_id, *byte_size);
      store_->set_blob_status(blob_id, BlobStatus::Ok);
    } else {
      blob_id = store_->insert_blob(byte_size, BlobStatus::Ok);
      store_->put_hash(blob_id, HashAlgoId::Sha256, *digest);
    }
    store_->upsert_locator(job.uri, blob_id, byte_size, mtime, path->string(),
                           std::nullopt);
    (void)store_->ensure_image_media(blob_id, probe->size.width,
                                     probe->size.height);
    reply_size(probe->size);
  } catch (const std::exception& ex) {
    if (debug_enabled()) {
      dbg("handle_probe_size_store: %s", ex.what());
    }
    reply_empty();
  }
}

void Client::handle_ensure_pixels_store(Job& job) {
  auto reply = [&](std::optional<PixelLevel> px) {
    if (!job.pixels_cb) return;
    auto cb = std::move(job.pixels_cb);
    auto uri = job.uri;
    const int edge = job.max_edge;
    executor_.post([cb = std::move(cb), uri = std::move(uri), edge,
                    px = std::move(px)]() mutable {
      cb(std::move(uri), edge, std::move(px));
    });
  };

  // Prefer existing tiles / Store meta.
  if (auto px = get_pixels(job.uri, job.max_edge, job.frame_idx,
                           /*allow_tile_synth=*/true)) {
    reply(std::move(px));
    return;
  }

  // Ensure Store meta (size) exists.
  {
    Job probe;
    probe.kind = JobKind::ProbeSize;
    probe.uri = job.uri;
    handle_probe_size_store(probe);
  }

  if (auto px = get_pixels(job.uri, job.max_edge, job.frame_idx,
                           /*allow_tile_synth=*/true)) {
    reply(std::move(px));
    return;
  }

  const int edge_limit =
      job.full_native
          ? std::min(job.max_edge > 0 ? job.max_edge : kFullMaxEdge, kFullMaxEdge)
          : std::min(job.max_edge > 0 ? job.max_edge : kMaxSoftLadderEdge,
                     kMaxSoftLadderEdge);

  std::vector<LevelBlob> levels;
  std::string content_id;
  if (auto pdf = parse_pdf_uri(job.uri)) {
    auto sm = meta_from_store(job.uri);
    if (sm) content_id = sm->content_id;
    auto raster = pdf_rasterize_page(pdf->pdf_path, pdf->page, edge_limit,
                                     pdf->backend);
    if (raster && !raster->rgb.empty() && !content_id.empty()) {
      levels = build_ladder_rgb(raster->rgb.data(), raster->width, raster->height,
                                content_id, kDefaultJxlQuality, edge_limit);
    }
  } else if (auto dj = parse_djvu_uri(job.uri)) {
    auto sm = meta_from_store(job.uri);
    if (sm) content_id = sm->content_id;
    auto raster = djvu_rasterize_page(dj->djvu_path, dj->page, edge_limit);
    if (raster && !raster->rgb.empty() && !content_id.empty()) {
      levels = build_ladder_rgb(raster->rgb.data(), raster->width, raster->height,
                                content_id, kDefaultJxlQuality, edge_limit);
    }
  } else if (auto ep = parse_epub_uri(job.uri)) {
    auto sm = meta_from_store(job.uri);
    if (sm) content_id = sm->content_id;
    auto raster = epub_rasterize_page(ep->epub_path, ep->page, ep->layout,
                                      edge_limit);
    if (raster && !raster->rgb.empty() && !content_id.empty()) {
      levels = build_ladder_rgb(raster->rgb.data(), raster->width, raster->height,
                                content_id, kDefaultJxlQuality, edge_limit);
    }
  } else if (auto arch = parse_archive_uri(job.uri)) {
    if (!arch->member_path.empty()) {
      auto sm = meta_from_store(job.uri);
      if (sm) content_id = sm->content_id;
      auto bytes = member_bytes(arch->archive_path, arch->member_path);
      if (bytes && !bytes->empty() && !content_id.empty()) {
        levels = build_ladder_buffer(bytes->data(), bytes->size(), content_id,
                                     kDefaultJxlQuality, edge_limit);
      }
    }
  } else if (is_http_uri(job.uri)) {
    auto sm = meta_from_store(job.uri);
    if (sm) content_id = sm->content_id;
    auto bytes = fetch_http_cached(job.uri);
    if (bytes && !bytes->empty() && !content_id.empty()) {
      levels = build_ladder_buffer(bytes->data(), bytes->size(), content_id,
                                   kDefaultJxlQuality, edge_limit);
    }
  } else if (auto path = path_from_file_uri(job.uri)) {
    if (std::filesystem::is_regular_file(*path)) {
      const auto hex = sha256_file_hex(*path);
      if (!hex.empty()) {
        content_id = std::string(kContentIdSha256Prefix) + hex;
        levels = build_ladder(*path, content_id, kDefaultJxlQuality, edge_limit);
      }
    }
  }

  if (levels.empty()) {
    reply(std::nullopt);
    return;
  }
  // Opportunistic LQIP from the smallest ladder step (already in RAM). Far
  // cheaper than a separate EnsureLqip full-source thumbnail.
  if (!get_lqip(job.uri)) {
    const LevelBlob* small = &levels.front();
    for (const auto& lvl : levels) {
      if (lvl.max_edge > 0 &&
          (small->max_edge <= 0 || lvl.max_edge < small->max_edge)) {
        small = &lvl;
      }
    }
    if (!small->bytes.empty()) {
      if (auto key = lqip_key_ensure(*store_, job.uri)) {
        auto bytes = lqip_thumbhash_from_buffer(small->bytes.data(),
                                                small->bytes.size());
        if (!bytes.empty()) {
          const int kind =
              bytes.size() == 147 ? kLqipKindHandsum : kLqipKindThumbHash;
          try {
            store_->put_blob_lqip(key->blob_id, kind, bytes, key->page_1based);
          } catch (...) {
          }
        }
      }
    }
  }
  LevelBlob& best = levels.front();
  for (auto& lvl : levels) {
    if (lvl.max_edge > best.max_edge) best = lvl;
  }
  PixelLevel out;
  out.max_edge = best.max_edge;
  out.frame_idx = best.frame_idx;
  out.width = best.width;
  out.height = best.height;
  out.codec = best.codec;
  out.bytes = std::move(best.bytes);
  out.source = best.source;
  reply(std::move(out));
}

void Client::handle_probe_size(
    Job& job,
    const std::optional<std::vector<std::uint8_t>>& preextracted) {
  (void)preextracted;
  global_build_stats().probes_done.fetch_add(1, std::memory_order_relaxed);
  handle_probe_size_store(job);
}


std::optional<std::int64_t> Client::ensure_store_container_blob(
    const std::filesystem::path& archive_path) {
  if (!store_ || archive_path.empty()) return std::nullopt;
  try {
    const auto root_uri = archive_uri(archive_path);
    if (auto loc = store_->find_locator(root_uri)) {
      if (loc->blob_id) return *loc->blob_id;
    }
    // Also try plain file:// of the archive (same bytes, different locator).
    const auto file_uri = file_uri_from_path(archive_path.lexically_normal());
    if (auto loc = store_->find_locator(file_uri)) {
      if (loc->blob_id) {
        store_->upsert_locator(root_uri, *loc->blob_id, loc->size, loc->mtime_ns,
                               archive_path.lexically_normal().string(),
                               std::nullopt);
        return *loc->blob_id;
      }
    }
    const auto size = file_size_bytes(archive_path);
    const auto mtime = file_mtime_ns(archive_path);
    const auto blob_id = store_->insert_blob(size, BlobStatus::Ok);
    const std::string outer = archive_path.lexically_normal().string();
    store_->upsert_locator(root_uri, blob_id, size, mtime, outer, std::nullopt);
    store_->upsert_locator(file_uri, blob_id, size, mtime, outer, std::nullopt);
    return blob_id;
  } catch (const std::exception& ex) {
    if (debug_enabled()) {
      dbg("ensure_store_container_blob failed: %s", ex.what());
    }
    return std::nullopt;
  }
}

void Client::handle_ensure_pixels(
    Job& job, const std::optional<std::vector<std::uint8_t>>& preextracted) {
  (void)preextracted;
  global_build_stats().pixel_jobs.fetch_add(1, std::memory_order_relaxed);
  if (debug_enabled()) {
    dbg("EnsurePixels START uri=%s max_edge=%d frame=%d", job.uri.c_str(),
        job.max_edge, job.frame_idx);
  }
  handle_ensure_pixels_store(job);
}




void Client::store_tiles(const std::string& content_id,
                         const std::vector<TileBlob>& tiles) {
  put_tiles_to_store(content_id, tiles);
}

void Client::put_tiles_to_store(const std::string& content_id,
                                   const std::vector<TileBlob>& tiles) {
  if (!store_ || tiles.empty() || content_id.empty()) return;

  constexpr std::string_view kSha = "sha256:";
  if (!content_id.starts_with(kSha) || content_id.size() < kSha.size() + 64) {
    return;
  }

  const std::string_view rest(content_id.data() + kSha.size(),
                              content_id.size() - kSha.size());
  const std::string_view hex = rest.substr(0, 64);
  for (char c : hex) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) {
      return;
    }
  }

  std::optional<int> page_1based;
  if (rest.size() > 64) {
    constexpr std::string_view kPage = ":page:";
    if (rest.size() > 64 + kPage.size() &&
        rest.substr(64, kPage.size()) == kPage) {
      try {
        page_1based = std::stoi(std::string(rest.substr(64 + kPage.size())));
      } catch (...) {
        return;
      }
      if (*page_1based < 1) return;
    } else {
      return;  // composite ids without page mapping
    }
  }

  auto digest = Store::parse_sha256_digest(hex);
  if (!digest || digest->size() != 32) return;

  auto blob_id = store_->find_blob_by_hash(HashAlgoId::Sha256, *digest);
  if (!blob_id) {
    try {
      blob_id = store_->insert_blob({}, BlobStatus::Ok);
      store_->put_hash(*blob_id, HashAlgoId::Sha256, *digest);
    } catch (const std::exception& ex) {
      if (debug_enabled()) {
        dbg("put_tiles_to_store insert_blob: %s", ex.what());
      }
      return;
    }
  }

  try {
    std::int64_t media_id = 0;
    std::int64_t region_id = 0;
    if (page_1based) {
      media_id = store_->ensure_document_media(*blob_id, {});
      region_id = store_->ensure_page_region(media_id, *page_1based);
    } else {
      media_id = store_->ensure_image_media(*blob_id, {}, {});
      if (auto full = store_->find_full_region(media_id)) {
        region_id = full->id;
      } else {
        region_id = store_->ensure_region(media_id, RegionKind::Full, "", {});
      }
    }

    for (const auto& t : tiles) {
      if (t.bytes.empty()) continue;
      Store::TileRow meta;
      meta.media_id = media_id;
      meta.region_id = region_id;
      meta.scale = t.scale;
      meta.x = t.x;
      meta.y = t.y;
      meta.width = t.width;
      meta.height = t.height;
      if (t.codec == "jxl") {
        meta.codec_id = CodecId::Jxl;
      } else if (t.codec == "png") {
        meta.codec_id = CodecId::Png;
      } else {
        meta.codec_id = CodecId::Jpeg;
      }
      meta.quality = kDefaultTileQuality;
      store_->put_tile(meta, t.bytes);
    }
  } catch (const std::exception& ex) {
    if (debug_enabled()) {
      dbg("put_tiles_to_store failed content_id=%s: %s", content_id.c_str(),
          ex.what());
    }
  }
}

void Client::invalidate_q1_levels(const std::string& /*content_id*/) {
  // Soft/overview levels are not written on Store-only Client.
}

namespace {
struct DeferredTileStore {
  std::string content_id;
  TileBlob rgb;
};
thread_local std::vector<DeferredTileStore> g_deferred_tile_stores;
}  // namespace


void Client::handle_ensure_tiles_store(Job& job) {
  auto reply_one = [&](std::optional<TileBlob> tile) {
    if (!job.tile_cb) return;
    if (tile) debug_overlay_tile(*tile, job.uri);
    auto cb = std::move(job.tile_cb);
    auto uri = job.uri;
    const int scale = job.tile_scale;
    const int x = job.tile_x;
    const int y = job.tile_y;
    executor_.post([cb = std::move(cb), uri = std::move(uri), scale, x, y,
                    tile = std::move(tile)]() mutable {
      cb(std::move(uri), scale, x, y, std::move(tile));
    });
  };
  auto reply_pyramid_done = [&](bool ok) {
    // prepare --tiles / hosts use tile_cb as completion (one shot per URI).
    if (!job.tile_cb) return;
    auto cb = std::move(job.tile_cb);
    auto uri = job.uri;
    if (ok) {
      TileBlob marker;
      marker.scale = 0;
      marker.x = 0;
      marker.y = 0;
      marker.codec = "pyramid-ok";
      executor_.post([cb = std::move(cb), uri = std::move(uri),
                      marker = std::move(marker)]() mutable {
        cb(std::move(uri), 0, 0, 0, std::move(marker));
      });
    } else {
      executor_.post([cb = std::move(cb), uri = std::move(uri)]() mutable {
        cb(std::move(uri), 0, 0, 0, std::nullopt);
      });
    }
  };

  // Legacy/internal: multi-cell batch (prefer request_tiles fast-path above).
  if (!job.tile_batch.empty() && job.tile_batch_cb) {
    auto cb = std::move(job.tile_batch_cb);
    auto uri = job.uri;
    auto coords = std::move(job.tile_batch);
    for (std::size_t i = 0; i < coords.size(); ++i) {
      const auto& c = coords[i];
      std::optional<TileBlob> t = get_tile(uri, c.scale, c.x, c.y);
      if (!t) {
        // Encode one cell by temporarily reusing single-cell path fields.
        Job one;
        one.kind = JobKind::EnsureTiles;
        one.uri = uri;
        one.tile_scale = c.scale;
        one.tile_x = c.x;
        one.tile_y = c.y;
        one.tile_min_scale = c.scale;
        one.tile_max_scale = c.scale;
        one.tile_pyramid = false;
        one.skip_probe = job.skip_probe;
        // Synchronous encode into optional via nested handle is messy; queue
        // was already the design. Reply miss for missing durable cell so the
        // host retries via request_tile.
      }
      executor_.post([cb, i, t = std::move(t)]() mutable { cb(i, std::move(t)); });
    }
    return;
  }

  // Cache hit (Store tiles via get_tile).
  if (!job.tile_pyramid) {
    if (auto t = get_tile(job.uri, job.tile_scale, job.tile_x, job.tile_y)) {
      reply_one(std::move(t));
      return;
    }
  }

  if (!job.skip_probe) {
    Job probe;
    probe.kind = JobKind::ProbeSize;
    probe.uri = job.uri;
    handle_probe_size_store(probe);
  }

  if (!job.tile_pyramid) {
    if (auto t = get_tile(job.uri, job.tile_scale, job.tile_x, job.tile_y)) {
      reply_one(std::move(t));
      return;
    }
  }

  auto sm = meta_from_store(job.uri);
  if (!sm || sm->content_id.empty()) {
    reply_one(std::nullopt);
    return;
  }
  const std::string content_id = sm->content_id;

  // Single cell: file:// image, PDF page (live rgb888), archive member.
  if (!job.tile_pyramid) {
    std::optional<TileBlob> cell;
    if (auto pdf = parse_pdf_uri(job.uri)) {
      auto raster = pdf_render_tile_cell(pdf->pdf_path, pdf->page, job.tile_scale,
                                         job.tile_x, job.tile_y, pdf->backend);
      if (raster && !raster->rgb.empty()) {
        if (job.tile_scale >= kPdfMinDurableTileScale) {
          if (auto jpeg = encode_tile_cell_rgb(
                  raster->rgb.data(), raster->width, raster->height,
                  job.tile_scale, job.tile_x, job.tile_y, kPdfTileQuality)) {
            jpeg->source = TileSource::PdfRegion;
            store_tiles(content_id, std::vector<TileBlob>{*jpeg});
          }
        }
        TileBlob live;
        live.scale = job.tile_scale;
        live.x = job.tile_x;
        live.y = job.tile_y;
        live.width = raster->width;
        live.height = raster->height;
        live.codec = kTileCodecRgb888;
        live.source = TileSource::PdfRegion;
        live.bytes = std::move(raster->rgb);
        reply_one(std::move(live));
        return;
      }
    } else if (auto dj = parse_djvu_uri(job.uri)) {
      auto raster = djvu_render_tile_cell(dj->djvu_path, dj->page, job.tile_scale,
                                          job.tile_x, job.tile_y);
      if (raster && !raster->rgb.empty()) {
        if (job.tile_scale >= kPdfMinDurableTileScale) {
          if (auto jpeg = encode_tile_cell_rgb(
                  raster->rgb.data(), raster->width, raster->height,
                  job.tile_scale, job.tile_x, job.tile_y, kPdfTileQuality)) {
            jpeg->source = TileSource::DjvuRegion;
            store_tiles(content_id, std::vector<TileBlob>{*jpeg});
          }
        }
        TileBlob live;
        live.scale = job.tile_scale;
        live.x = job.tile_x;
        live.y = job.tile_y;
        live.width = raster->width;
        live.height = raster->height;
        live.codec = kTileCodecRgb888;
        live.source = TileSource::DjvuRegion;
        live.bytes = std::move(raster->rgb);
        reply_one(std::move(live));
        return;
      }
    } else if (auto ep = parse_epub_uri(job.uri)) {
      auto raster = epub_render_tile_cell(ep->epub_path, ep->page, ep->layout,
                                          job.tile_scale, job.tile_x, job.tile_y);
      if (raster && !raster->rgb.empty()) {
        if (job.tile_scale >= kPdfMinDurableTileScale) {
          if (auto jpeg = encode_tile_cell_rgb(
                  raster->rgb.data(), raster->width, raster->height,
                  job.tile_scale, job.tile_x, job.tile_y, kPdfTileQuality)) {
            jpeg->source = TileSource::Full;
            store_tiles(content_id, std::vector<TileBlob>{*jpeg});
          }
        }
        TileBlob live;
        live.scale = job.tile_scale;
        live.x = job.tile_x;
        live.y = job.tile_y;
        live.width = raster->width;
        live.height = raster->height;
        live.codec = kTileCodecRgb888;
        live.source = TileSource::Full;
        live.bytes = std::move(raster->rgb);
        reply_one(std::move(live));
        return;
      }
    } else if (auto arch = parse_archive_uri(job.uri)) {
      if (!arch->member_path.empty()) {
        auto bytes = member_bytes(arch->archive_path, arch->member_path);
        if (bytes && !bytes->empty()) {
          const std::string dkey =
              "a:" + extract_cache_key(arch->archive_path, arch->member_path);
          cell = build_tile_cell_buffer(bytes->data(), bytes->size(),
                                        job.tile_scale, job.tile_x, job.tile_y,
                                        kDefaultTileQuality, dkey);
        }
      }
    } else if (is_http_uri(job.uri)) {
      auto bytes = fetch_http_cached(job.uri);
      if (bytes && !bytes->empty()) {
        const std::string dkey = "h:" + std::string(job.uri);
        cell = build_tile_cell_buffer(bytes->data(), bytes->size(),
                                      job.tile_scale, job.tile_x, job.tile_y,
                                      kDefaultTileQuality, dkey);
      }
    } else if (auto path = path_from_file_uri(job.uri)) {
      if (std::filesystem::is_regular_file(*path)) {
        cell = build_tile_cell(*path, job.tile_scale, job.tile_x, job.tile_y,
                               kDefaultTileQuality);
      }
    }
    if (!cell || cell->bytes.empty()) {
      reply_one(std::nullopt);
      return;
    }
    TileBlob live = std::move(*cell);
    const bool rgb = (live.codec == kTileCodecRgb888);
    std::vector<std::uint8_t> rgb_copy;
    int dw = 0, dh = 0, ds = 0, dx = 0, dy = 0;
    if (rgb) {
      rgb_copy = live.bytes;
      dw = live.width;
      dh = live.height;
      ds = live.scale;
      dx = live.x;
      dy = live.y;
    }
    TileBlob non_rgb_store;
    if (!rgb) non_rgb_store = live;
    reply_one(std::move(live));
    if (rgb && !rgb_copy.empty()) {
      if (auto jpeg = encode_tile_cell_rgb(rgb_copy.data(), dw, dh, ds, dx, dy,
                                           kDefaultTileQuality)) {
        store_tiles(content_id, std::vector<TileBlob>{*jpeg});
      }
    } else if (!non_rgb_store.bytes.empty()) {
      store_tiles(content_id, std::vector<TileBlob>{std::move(non_rgb_store)});
    }
    return;
  }

  // Pyramid: durable JPEG cells for file:// and archive members (size known).
  if (!sm->size || sm->size->width <= 0 || sm->size->height <= 0) {
    reply_pyramid_done(false);
    return;
  }

  const int min_scale = job.tile_min_scale;
  int max_scale = job.tile_max_scale;
  if (max_scale < 0) {
    max_scale = 0;
    int w = sm->size->width, h = sm->size->height;
    while (w > kTileSize || h > kTileSize) {
      w = dim_at_tile_scale(w, 1);
      h = dim_at_tile_scale(h, 1);
      if (w < 1) w = 1;
      if (h < 1) h = 1;
      ++max_scale;
    }
  }

  std::vector<TileBlob> tiles;
  if (auto arch = parse_archive_uri(job.uri)) {
    if (arch->member_path.empty()) {
      reply_pyramid_done(false);
      return;
    }
    auto bytes = member_bytes(arch->archive_path, arch->member_path);
    if (!bytes || bytes->empty()) {
      reply_pyramid_done(false);
      return;
    }
    tiles = build_tile_pyramid_buffer(bytes->data(), bytes->size(), min_scale,
                                      max_scale, kDefaultTileQuality);
  } else if (auto path = path_from_file_uri(job.uri)) {
    if (!std::filesystem::is_regular_file(*path)) {
      reply_pyramid_done(false);
      return;
    }
    tiles = build_tile_pyramid(*path, min_scale, max_scale, kDefaultTileQuality);
  } else {
    reply_pyramid_done(false);
    return;
  }

  if (!tiles.empty()) {
    store_tiles(content_id, tiles);
  }
  reply_pyramid_done(!tiles.empty());
}

void Client::handle_ensure_tiles(
    Job& job, const std::optional<std::vector<std::uint8_t>>& preextracted) {
  (void)preextracted;
  global_build_stats().tile_jobs.fetch_add(1, std::memory_order_relaxed);
  if (debug_enabled()) {
    dbg("EnsureTiles START uri=%s scale=%d cell=%d,%d pyramid=%d",
        job.uri.c_str(), job.tile_scale, job.tile_x, job.tile_y,
        job.tile_pyramid ? 1 : 0);
  }
  handle_ensure_tiles_store(job);
}


std::vector<std::string> Client::get_tags(std::string_view uri) const {
  if (!store_) return {};
  auto sloc = store_->find_locator(uri);
  if (!sloc || !sloc->blob_id) return {};
  auto bref = store_->blob_ref_sha256(*sloc->blob_id);
  if (!bref) return {};
  return store_->tags_for_blob_ref(*bref);
}

bool Client::add_tag(std::string_view uri, std::string_view tag,
                     std::string_view source) {
  if (!store_ || tag.empty()) return false;
  auto sloc = store_->find_locator(uri);
  if (!sloc || !sloc->blob_id) return false;
  auto bref = store_->blob_ref_sha256(*sloc->blob_id);
  if (!bref) return false;
  store_->add_blob_tag(*bref, tag, source);
  return true;
}

bool Client::remove_tag(std::string_view uri, std::string_view tag) {
  if (!store_ || tag.empty()) return false;
  auto sloc = store_->find_locator(uri);
  if (!sloc || !sloc->blob_id) return false;
  auto bref = store_->blob_ref_sha256(*sloc->blob_id);
  if (!bref) return false;
  return store_->remove_blob_tag(*bref, tag);
}


namespace {

struct DocBlobKey {
  std::int64_t blob_id = 0;
  int page_1based = 1;
  std::string layout_key;
};

std::optional<DocBlobKey> doc_blob_key_lookup(const Store& store,
                                              std::string_view uri) {
  std::filesystem::path file;
  int page = 1;
  std::string layout;
  if (auto pdf = parse_pdf_uri(uri)) {
    file = pdf->pdf_path;
    page = pdf->page;
  } else if (auto dj = parse_djvu_uri(uri)) {
    file = dj->djvu_path;
    page = dj->page;
  } else if (auto ep = parse_epub_uri(uri)) {
    file = ep->epub_path;
    page = ep->page;
    layout = format_epub_layout_params(ep->layout);
  } else {
    return std::nullopt;
  }
  if (auto loc = store.find_locator(uri); loc && loc->blob_id) {
    return DocBlobKey{*loc->blob_id, page, layout};
  }
  if (!file.empty()) {
    const auto file_uri = file_uri_from_path(file);
    if (auto loc = store.find_locator(file_uri); loc && loc->blob_id) {
      return DocBlobKey{*loc->blob_id, page, layout};
    }
  }
  return std::nullopt;
}

std::optional<DocBlobKey> doc_blob_key_ensure(Store& store,
                                              std::string_view uri) {
  if (auto hit = doc_blob_key_lookup(store, uri)) return hit;
  std::filesystem::path file;
  int page = 1;
  std::string layout;
  if (auto pdf = parse_pdf_uri(uri)) {
    file = pdf->pdf_path;
    page = pdf->page;
  } else if (auto dj = parse_djvu_uri(uri)) {
    file = dj->djvu_path;
    page = dj->page;
  } else if (auto ep = parse_epub_uri(uri)) {
    file = ep->epub_path;
    page = ep->page;
    layout = format_epub_layout_params(ep->layout);
  } else {
    return std::nullopt;
  }
  if (!std::filesystem::is_regular_file(file)) return std::nullopt;
  const auto hex = sha256_file_hex(file);
  if (hex.empty()) return std::nullopt;
  auto digest = Store::parse_sha256_digest(hex);
  if (!digest) return std::nullopt;
  std::int64_t blob_id = 0;
  if (auto existing = store.find_blob_by_hash(HashAlgoId::Sha256, *digest)) {
    blob_id = *existing;
  } else {
    blob_id = store.insert_blob(file_size_bytes(file), BlobStatus::Ok);
    store.put_hash(blob_id, HashAlgoId::Sha256, *digest);
  }
  const auto file_uri = file_uri_from_path(file);
  (void)store.upsert_locator(file_uri, blob_id, file_size_bytes(file),
                             file_mtime_ns(file), file.string());
  return DocBlobKey{blob_id, page, layout};
}

}  // namespace

std::optional<PageTextLayer> Client::get_page_text_layer(
    std::string_view uri) const {
  if (!store_ || uri.empty()) return std::nullopt;
  auto key = doc_blob_key_lookup(*store_, uri);
  if (!key) return std::nullopt;
  auto bytes = store_->get_page_text_layer(key->blob_id, key->page_1based,
                                           key->layout_key);
  if (!bytes) return std::nullopt;
  return deserialize_page_text_layer(*bytes);
}

std::optional<PageTextLayer> Client::ensure_page_text_layer(
    std::string_view uri) {
  if (auto hit = get_page_text_layer(uri)) return hit;
  auto layer = extract_page_text_layer(uri);
  if (!layer || !store_) return layer;
  auto key = doc_blob_key_ensure(*store_, uri);
  if (key) {
    auto bytes = serialize_page_text_layer(*layer);
    if (!bytes.empty()) {
      store_->put_page_text_layer(key->blob_id, key->page_1based,
                                  key->layout_key, bytes);
    }
  }
  return layer;
}

std::optional<DocumentOutline> Client::get_document_outline(
    std::string_view uri) const {
  if (!store_ || uri.empty()) return std::nullopt;
  auto key = doc_blob_key_lookup(*store_, uri);
  if (!key) return std::nullopt;
  auto bytes = store_->get_document_outline(key->blob_id, key->layout_key);
  if (!bytes) return std::nullopt;
  return deserialize_document_outline(*bytes);
}

std::optional<DocumentOutline> Client::ensure_document_outline(
    std::string_view uri) {
  if (auto hit = get_document_outline(uri)) return hit;
  auto outline = extract_document_outline(uri);
  if (!outline || !store_) return outline;
  auto key = doc_blob_key_ensure(*store_, uri);
  if (key) {
    auto bytes = serialize_document_outline(*outline);
    if (!bytes.empty()) {
      store_->put_document_outline(key->blob_id, key->layout_key, bytes);
    }
  }
  return outline;
}


}  // namespace thumtoo
