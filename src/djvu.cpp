// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/djvu.hpp"

#include "thumtoo/format.hpp"
#include "thumtoo/image.hpp"
#include "thumtoo/uri.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <mutex>

#if defined(THUMTOO_HAVE_DJVU)
#  include <libdjvu/ddjvuapi.h>
#  include <libdjvu/miniexp.h>
#endif

namespace thumtoo {
namespace {

constexpr std::string_view kPagePipe = "//page:";

#if defined(THUMTOO_HAVE_DJVU)

// One shared document for the process. TLS caches used to open the same
// multipage book on every worker (×N full documents in RAM + decode thrash).
// ddjvu contexts are not safe for concurrent use; serialize all API calls.
struct SharedDjvuDoc {
  std::mutex mu;
  std::string path_key;
  std::filesystem::file_time_type mtime{};
  ddjvu_context_t* ctx = nullptr;
  ddjvu_document_t* doc = nullptr;
  // Layout cache for the open document only.
  int layout_page = 0;
  Size layout_native{0, 0};
};

SharedDjvuDoc g_djvu;

void release_shared_djvu_unlocked() {
  if (g_djvu.doc) {
    ddjvu_document_release(g_djvu.doc);
    g_djvu.doc = nullptr;
  }
  if (g_djvu.ctx) {
    ddjvu_context_release(g_djvu.ctx);
    g_djvu.ctx = nullptr;
  }
  g_djvu.path_key.clear();
  g_djvu.mtime = {};
  g_djvu.layout_page = 0;
  g_djvu.layout_native = {};
}

void pump_messages(ddjvu_context_t* ctx) {
  if (!ctx) return;
  const ddjvu_message_t* msg;
  while ((msg = ddjvu_message_peek(ctx)) != nullptr) {
    ddjvu_message_pop(ctx);
  }
}

void wait_doc_decoded(ddjvu_context_t* ctx, ddjvu_document_t* doc) {
  if (!ctx || !doc) return;
  while (!ddjvu_document_decoding_done(doc)) {
    ddjvu_message_wait(ctx);
    pump_messages(ctx);
  }
}

void wait_page_decoded(ddjvu_context_t* ctx, ddjvu_page_t* page) {
  if (!ctx || !page) return;
  while (!ddjvu_page_decoding_done(page)) {
    ddjvu_message_wait(ctx);
    pump_messages(ctx);
  }
}

// Caller must hold g_djvu.mu.
ddjvu_document_t* cached_djvu_document_unlocked(const std::filesystem::path& path) {
  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(path, ec);
  const std::string key = path.lexically_normal().string();
  if (g_djvu.doc && g_djvu.path_key == key && !ec && g_djvu.mtime == mtime) {
    return g_djvu.doc;
  }

  release_shared_djvu_unlocked();

  ddjvu_context_t* ctx = ddjvu_context_create("thumtoo");
  if (!ctx) return nullptr;

  ddjvu_document_t* doc =
      ddjvu_document_create_by_filename_utf8(ctx, path.string().c_str(), TRUE);
  if (!doc) {
    ddjvu_context_release(ctx);
    return nullptr;
  }

  wait_doc_decoded(ctx, doc);
  if (ddjvu_document_decoding_error(doc)) {
    ddjvu_document_release(doc);
    ddjvu_context_release(ctx);
    return nullptr;
  }

  g_djvu.path_key = key;
  g_djvu.mtime = ec ? std::filesystem::file_time_type{} : mtime;
  g_djvu.ctx = ctx;
  g_djvu.doc = doc;
  g_djvu.layout_page = 0;
  g_djvu.layout_native = {};
  return doc;
}

// Caller must hold g_djvu.mu.
std::optional<Size> page_native_size_unlocked(ddjvu_context_t* ctx,
                                              ddjvu_document_t* doc,
                                              int page_1based) {
  if (!ctx || !doc || page_1based < 1) return std::nullopt;
  const int n = ddjvu_document_get_pagenum(doc);
  if (page_1based > n) return std::nullopt;

  ddjvu_pageinfo_t info{};
  while (ddjvu_document_get_pageinfo(doc, page_1based - 1, &info) <
         DDJVU_JOB_OK) {
    ddjvu_message_wait(ctx);
    pump_messages(ctx);
  }
  if (info.width <= 0 || info.height <= 0) return std::nullopt;
  return Size{info.width, info.height};
}

#endif  // THUMTOO_HAVE_DJVU

}  // namespace

bool is_likely_djvu_path(const std::filesystem::path& path) {
  return is_djvu_path(path);
}

std::string djvu_page_uri(const std::filesystem::path& djvu_path,
                          int page_1based) {
  auto uri = file_uri_from_path(djvu_path.lexically_normal());
  uri += "//page:";
  uri += std::to_string(std::max(1, page_1based));
  return uri;
}

std::optional<ParsedDjvuUri> parse_djvu_uri(std::string_view uri) {
  const auto pipe = uri.find(kPagePipe);
  if (pipe == std::string_view::npos) return std::nullopt;

  const auto outer = uri.substr(0, pipe);
  auto path = path_from_file_uri(outer);
  if (!path) return std::nullopt;
  if (!is_djvu_path(*path)) return std::nullopt;

  std::string_view rest = uri.substr(pipe + kPagePipe.size());
  if (rest.empty()) return std::nullopt;
  int page = 0;
  for (char c : rest) {
    if (c < '0' || c > '9') return std::nullopt;
    page = page * 10 + (c - '0');
    if (page > 1'000'000) return std::nullopt;
  }
  if (page < 1) return std::nullopt;

  ParsedDjvuUri out;
  out.djvu_path = *path;
  out.page = page;
  return out;
}

std::optional<int> djvu_page_count(const std::filesystem::path& path) {
#if !defined(THUMTOO_HAVE_DJVU)
  (void)path;
  return std::nullopt;
#else
  std::lock_guard lock(g_djvu.mu);
  ddjvu_document_t* doc = cached_djvu_document_unlocked(path);
  if (!doc || !g_djvu.ctx) return std::nullopt;
  pump_messages(g_djvu.ctx);
  const int n = ddjvu_document_get_pagenum(doc);
  if (n <= 0) return std::nullopt;
  return n;
#endif
}

std::optional<Size> djvu_page_size_native(const std::filesystem::path& path,
                                          int page_1based) {
#if !defined(THUMTOO_HAVE_DJVU)
  (void)path;
  (void)page_1based;
  return std::nullopt;
#else
  if (page_1based < 1) return std::nullopt;
  std::lock_guard lock(g_djvu.mu);
  const std::string key = path.lexically_normal().string();
  if (g_djvu.path_key == key && g_djvu.layout_page == page_1based &&
      g_djvu.layout_native.width > 0) {
    return g_djvu.layout_native;
  }
  ddjvu_document_t* doc = cached_djvu_document_unlocked(path);
  if (!doc || !g_djvu.ctx) return std::nullopt;
  auto sz = page_native_size_unlocked(g_djvu.ctx, doc, page_1based);
  if (!sz) return std::nullopt;
  g_djvu.layout_page = page_1based;
  g_djvu.layout_native = *sz;
  return sz;
#endif
}

std::optional<Size> djvu_page_layout_size(const std::filesystem::path& path,
                                          int page_1based) {
  // Native decoder pixels are the layout grid (scale 0). DjVu pages are
  // already pixel-sized (often ~200–300 dpi scans), unlike PDF media points.
  return djvu_page_size_native(path, page_1based);
}

Size djvu_page_size_at_scale(Size layout, int scale) {
  if (layout.width <= 0 || layout.height <= 0) return Size{0, 0};
  if (scale == 0) return layout;
  const double factor = std::ldexp(1.0, -scale);
  const int w =
      std::max(1, static_cast<int>(std::lround(layout.width * factor)));
  const int h =
      std::max(1, static_cast<int>(std::lround(layout.height * factor)));
  return Size{w, h};
}

double djvu_dpi_for_scale(int scale) {
  // Relative to native page pixels as "layout". Factor is 2^{-scale}.
  return std::ldexp(1.0, -scale);
}

std::optional<DjvuRaster> djvu_rasterize_page(const std::filesystem::path& path,
                                              int page_1based, int max_edge) {
#if !defined(THUMTOO_HAVE_DJVU)
  (void)path;
  (void)page_1based;
  (void)max_edge;
  return std::nullopt;
#else
  if (page_1based < 1) return std::nullopt;
  std::lock_guard lock(g_djvu.mu);
  ddjvu_document_t* doc = cached_djvu_document_unlocked(path);
  if (!doc || !g_djvu.ctx) return std::nullopt;

  ddjvu_page_t* page = ddjvu_page_create_by_pageno(doc, page_1based - 1);
  if (!page) return std::nullopt;
  wait_page_decoded(g_djvu.ctx, page);
  if (ddjvu_page_decoding_error(page)) {
    ddjvu_page_release(page);
    return std::nullopt;
  }

  const int nw = ddjvu_page_get_width(page);
  const int nh = ddjvu_page_get_height(page);
  if (nw <= 0 || nh <= 0) {
    ddjvu_page_release(page);
    return std::nullopt;
  }

  int out_w = nw;
  int out_h = nh;
  if (max_edge > 0) {
    const int long_edge = std::max(nw, nh);
    if (long_edge > max_edge) {
      const double f = static_cast<double>(max_edge) / long_edge;
      out_w = std::max(1, static_cast<int>(std::lround(nw * f)));
      out_h = std::max(1, static_cast<int>(std::lround(nh * f)));
    }
  }

  ddjvu_rect_t pagerect{0, 0, static_cast<unsigned>(out_w),
                        static_cast<unsigned>(out_h)};
  ddjvu_rect_t renderrect = pagerect;

  ddjvu_format_t* fmt = ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, nullptr);
  if (!fmt) {
    ddjvu_page_release(page);
    return std::nullopt;
  }
  // Image-style coordinates: y increases downward (matches tile grid / Galapix).
  // Default ddjvu y-direction is PostScript-style (upwards); without this, tile
  // crops flip vertically and appear to walk up the page as zoom increases.
  ddjvu_format_set_row_order(fmt, 1);
  ddjvu_format_set_y_direction(fmt, 1);

  DjvuRaster out;
  out.width = out_w;
  out.height = out_h;
  out.rgb.resize(static_cast<std::size_t>(out_w) *
                 static_cast<std::size_t>(out_h) * 3u);

  const int rowsize = out_w * 3;
  const int ok = ddjvu_page_render(page, DDJVU_RENDER_COLOR, &pagerect,
                                   &renderrect, fmt, rowsize,
                                   reinterpret_cast<char*>(out.rgb.data()));
  ddjvu_format_release(fmt);
  ddjvu_page_release(page);
  if (g_djvu.ctx) ddjvu_cache_clear(g_djvu.ctx);
  // Blank / non-image DjVu pages often return 0 from render with a valid size.
  // Emit white so Galapix does not keep a permanent purple missing-tile cell.
  if (!ok) {
    std::fill(out.rgb.begin(), out.rgb.end(), static_cast<std::uint8_t>(255));
  }
  return out;
#endif
}

std::optional<DjvuRaster> djvu_rasterize_page_region(
    const std::filesystem::path& path, int page_1based, double scale_factor,
    int px, int py, int pw, int ph) {
#if !defined(THUMTOO_HAVE_DJVU)
  (void)path;
  (void)page_1based;
  (void)scale_factor;
  (void)px;
  (void)py;
  (void)pw;
  (void)ph;
  return std::nullopt;
#else
  if (page_1based < 1 || pw <= 0 || ph <= 0) return std::nullopt;
  std::lock_guard lock(g_djvu.mu);
  ddjvu_document_t* doc = cached_djvu_document_unlocked(path);
  if (!doc || !g_djvu.ctx) return std::nullopt;

  ddjvu_page_t* page = ddjvu_page_create_by_pageno(doc, page_1based - 1);
  if (!page) return std::nullopt;
  wait_page_decoded(g_djvu.ctx, page);
  if (ddjvu_page_decoding_error(page)) {
    ddjvu_page_release(page);
    return std::nullopt;
  }

  const int nw = ddjvu_page_get_width(page);
  const int nh = ddjvu_page_get_height(page);
  if (nw <= 0 || nh <= 0) {
    ddjvu_page_release(page);
    return std::nullopt;
  }

  const double factor = scale_factor > 0.0 ? scale_factor : 1.0;
  const int full_w = std::max(1, static_cast<int>(std::lround(nw * factor)));
  const int full_h = std::max(1, static_cast<int>(std::lround(nh * factor)));

  // pagerect = full page in output pixels; renderrect = crop in that space.
  ddjvu_rect_t pagerect{0, 0, static_cast<unsigned>(full_w),
                        static_cast<unsigned>(full_h)};
  ddjvu_rect_t renderrect{px, py, static_cast<unsigned>(pw),
                          static_cast<unsigned>(ph)};

  ddjvu_format_t* fmt = ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, nullptr);
  if (!fmt) {
    ddjvu_page_release(page);
    return std::nullopt;
  }
  // Image-style coordinates: y increases downward (matches tile grid / Galapix).
  // Default ddjvu y-direction is PostScript-style (upwards); without this, tile
  // crops flip vertically and appear to walk up the page as zoom increases.
  ddjvu_format_set_row_order(fmt, 1);
  ddjvu_format_set_y_direction(fmt, 1);

  DjvuRaster out;
  out.width = pw;
  out.height = ph;
  out.rgb.resize(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph) *
                 3u);

  const int rowsize = pw * 3;
  const int ok = ddjvu_page_render(page, DDJVU_RENDER_COLOR, &pagerect,
                                   &renderrect, fmt, rowsize,
                                   reinterpret_cast<char*>(out.rgb.data()));
  ddjvu_format_release(fmt);
  ddjvu_page_release(page);
  if (g_djvu.ctx) ddjvu_cache_clear(g_djvu.ctx);
  // Blank / non-image DjVu pages often return 0 from render with a valid size.
  // Emit white so Galapix does not keep a permanent purple missing-tile cell.
  if (!ok) {
    std::fill(out.rgb.begin(), out.rgb.end(), static_cast<std::uint8_t>(255));
  }
  return out;
#endif
}

std::optional<DjvuRaster> djvu_render_tile_cell(const std::filesystem::path& path,
                                                int page_1based, int scale,
                                                int x, int y) {
  auto layout = djvu_page_layout_size(path, page_1based);
  if (!layout) return std::nullopt;
  const Size full = djvu_page_size_at_scale(*layout, scale);
  if (full.width <= 0 || full.height <= 0) return std::nullopt;

  const int x0 = x * kTileSize;
  const int y0 = y * kTileSize;
  if (x0 >= full.width || y0 >= full.height) return std::nullopt;
  const int pw = std::min(kTileSize, full.width - x0);
  const int ph = std::min(kTileSize, full.height - y0);
  if (pw <= 0 || ph <= 0) return std::nullopt;

  const double factor = std::ldexp(1.0, -scale);
  return djvu_rasterize_page_region(path, page_1based, factor, x0, y0, pw, ph);
}

std::optional<TileBlob> djvu_build_tile_cell(const std::filesystem::path& path,
                                             int page_1based, int scale, int x,
                                             int y, int jpeg_quality) {
  auto raster = djvu_render_tile_cell(path, page_1based, scale, x, y);
  if (!raster) return std::nullopt;
  return encode_tile_cell_rgb(raster->rgb.data(), raster->width, raster->height,
                              scale, x, y, jpeg_quality);
}

#if defined(THUMTOO_HAVE_DJVU)

// DjVu text zones are nested miniexps:
//   (word|line|para|... xmin ymin xmax ymax "text" | child…)
// Coordinates are page pixels, origin bottom-left.
void walk_text_sexpr(miniexp_t expr, std::vector<TextRegion>& regions,
                     int page_h) {
  if (!miniexp_consp(expr)) return;
  miniexp_t head = miniexp_car(expr);
  if (!miniexp_symbolp(head)) return;

  const char* sym = miniexp_to_name(head);
  if (!sym) return;

  // Expect at least type + 4 numbers.
  miniexp_t rest = miniexp_cdr(expr);
  if (!miniexp_consp(rest)) return;
  miniexp_t x0e = miniexp_car(rest); rest = miniexp_cdr(rest);
  if (!miniexp_consp(rest)) return;
  miniexp_t y0e = miniexp_car(rest); rest = miniexp_cdr(rest);
  if (!miniexp_consp(rest)) return;
  miniexp_t x1e = miniexp_car(rest); rest = miniexp_cdr(rest);
  if (!miniexp_consp(rest)) return;
  miniexp_t y1e = miniexp_car(rest); rest = miniexp_cdr(rest);

  if (!miniexp_numberp(x0e) || !miniexp_numberp(y0e) ||
      !miniexp_numberp(x1e) || !miniexp_numberp(y1e)) {
    return;
  }
  const double x0 = static_cast<double>(miniexp_to_int(x0e));
  const double y0 = static_cast<double>(miniexp_to_int(y0e));
  const double x1 = static_cast<double>(miniexp_to_int(x1e));
  const double y1 = static_cast<double>(miniexp_to_int(y1e));

  const bool is_word = std::strcmp(sym, "word") == 0;
  const bool is_line = std::strcmp(sym, "line") == 0;
  // Prefer word-level; fall back to line if children are only a string.
  if (is_word || is_line) {
    // Collect trailing string leaves as the text for this zone.
    std::string text;
    for (miniexp_t p = rest; miniexp_consp(p); p = miniexp_cdr(p)) {
      miniexp_t el = miniexp_car(p);
      if (miniexp_stringp(el)) {
        const char* s = miniexp_to_str(el);
        if (s && s[0]) {
          if (!text.empty()) text.push_back(' ');
          text += s;
        }
      } else if (miniexp_consp(el)) {
        walk_text_sexpr(el, regions, page_h);
      }
    }
    if (is_word && !text.empty()) {
      TextRegion reg;
      reg.role = TextRegionRole::Text;
      reg.text = std::move(text);
      // Keep native bottom-left origin; document for consumers.
      reg.bbox = TextRect{x0, y0, x1, y1};
      if (!reg.bbox.empty()) regions.push_back(std::move(reg));
      return;
    }
    if (is_line && !text.empty()) {
      // Line with only a string (no word children).
      TextRegion reg;
      reg.role = TextRegionRole::Text;
      reg.text = std::move(text);
      reg.bbox = TextRect{x0, y0, x1, y1};
      if (!reg.bbox.empty()) regions.push_back(std::move(reg));
      return;
    }
  }

  // Recurse into children for page/column/region/para containers.
  for (miniexp_t p = rest; miniexp_consp(p); p = miniexp_cdr(p)) {
    miniexp_t el = miniexp_car(p);
    if (miniexp_consp(el)) walk_text_sexpr(el, regions, page_h);
  }
  (void)page_h;
}

void walk_anno_sexpr(miniexp_t expr, std::vector<TextRegion>& regions) {
  if (!miniexp_consp(expr)) return;
  miniexp_t head = miniexp_car(expr);
  if (miniexp_symbolp(head) &&
      std::strcmp(miniexp_to_name(head), "maparea") == 0) {
    // (maparea url_or_(url ... ) (rect x y w h) …) or (oval) (poly)
    std::string uri;
    TextRect bbox;
    bool have_bbox = false;
    for (miniexp_t p = miniexp_cdr(expr); miniexp_consp(p); p = miniexp_cdr(p)) {
      miniexp_t el = miniexp_car(p);
      if (miniexp_stringp(el)) {
        const char* s = miniexp_to_str(el);
        if (s) uri = s;
      } else if (miniexp_consp(el)) {
        miniexp_t eh = miniexp_car(el);
        if (!miniexp_symbolp(eh)) continue;
        const char* es = miniexp_to_name(eh);
        if (!es) continue;
        if (std::strcmp(es, "url") == 0) {
          miniexp_t u = miniexp_car(miniexp_cdr(el));
          if (miniexp_stringp(u)) {
            const char* s = miniexp_to_str(u);
            if (s) uri = s;
          }
        } else if (std::strcmp(es, "rect") == 0) {
          // (rect x y w h) — bottom-left origin
          miniexp_t a = miniexp_cdr(el);
          if (!miniexp_consp(a)) continue;
          int x = miniexp_numberp(miniexp_car(a)) ? miniexp_to_int(miniexp_car(a)) : 0;
          a = miniexp_cdr(a);
          if (!miniexp_consp(a)) continue;
          int y = miniexp_numberp(miniexp_car(a)) ? miniexp_to_int(miniexp_car(a)) : 0;
          a = miniexp_cdr(a);
          if (!miniexp_consp(a)) continue;
          int w = miniexp_numberp(miniexp_car(a)) ? miniexp_to_int(miniexp_car(a)) : 0;
          a = miniexp_cdr(a);
          if (!miniexp_consp(a)) continue;
          int h = miniexp_numberp(miniexp_car(a)) ? miniexp_to_int(miniexp_car(a)) : 0;
          if (w > 0 && h > 0) {
            bbox = TextRect{static_cast<double>(x), static_cast<double>(y),
                            static_cast<double>(x + w), static_cast<double>(y + h)};
            have_bbox = true;
          }
        }
      }
    }
    if (have_bbox && !uri.empty()) {
      TextRegion reg;
      reg.role = TextRegionRole::Link;
      reg.bbox = bbox;
      reg.target.kind = TextLinkTargetKind::Uri;
      reg.target.uri = std::move(uri);
      regions.push_back(std::move(reg));
    }
    return;
  }
  // Recurse lists of annotations.
  for (miniexp_t p = expr; miniexp_consp(p); p = miniexp_cdr(p)) {
    walk_anno_sexpr(miniexp_car(p), regions);
  }
}

void walk_outline_sexpr(miniexp_t expr, int level, DocumentOutline& out) {
  // Bookmarks: (bookmarks (title url …) (title url (children…)) …)
  // or nested lists of (title dest [children])
  if (!miniexp_consp(expr)) return;
  miniexp_t head = miniexp_car(expr);
  if (miniexp_symbolp(head) &&
      std::strcmp(miniexp_to_name(head), "bookmarks") == 0) {
    for (miniexp_t p = miniexp_cdr(expr); miniexp_consp(p); p = miniexp_cdr(p)) {
      walk_outline_sexpr(miniexp_car(p), level, out);
    }
    return;
  }
  // Entry: (title dest child…) where title and dest are strings.
  if (miniexp_stringp(head)) {
    OutlineItem item;
    item.level = level;
    const char* title = miniexp_to_str(head);
    if (title) item.title = title;
    miniexp_t rest = miniexp_cdr(expr);
    if (miniexp_consp(rest) && miniexp_stringp(miniexp_car(rest))) {
      const char* dest = miniexp_to_str(miniexp_car(rest));
      if (dest && dest[0]) {
        // "#N" page dest or URL
        if (dest[0] == '#') {
          int page = 0;
          if (std::sscanf(dest + 1, "%d", &page) == 1 && page >= 1) {
            item.page_1based = page;
          } else {
            item.uri = dest;
          }
        } else {
          item.uri = dest;
        }
      }
      rest = miniexp_cdr(rest);
    }
    out.items.push_back(std::move(item));
    for (; miniexp_consp(rest); rest = miniexp_cdr(rest)) {
      walk_outline_sexpr(miniexp_car(rest), level + 1, out);
    }
    return;
  }
  for (miniexp_t p = expr; miniexp_consp(p); p = miniexp_cdr(p)) {
    walk_outline_sexpr(miniexp_car(p), level, out);
  }
}

#endif  // THUMTOO_HAVE_DJVU

std::optional<PageTextLayer> djvu_page_text_layer(const std::filesystem::path& path,
                                                  int page_1based) {
#if !defined(THUMTOO_HAVE_DJVU)
  (void)path;
  (void)page_1based;
  return std::nullopt;
#else
  if (page_1based < 1) return std::nullopt;
  std::lock_guard lock(g_djvu.mu);
  ddjvu_document_t* doc = cached_djvu_document_unlocked(path);
  if (!doc || !g_djvu.ctx) return std::nullopt;

  auto sz = page_native_size_unlocked(g_djvu.ctx, doc, page_1based);
  if (!sz) return std::nullopt;

  PageTextLayer layer;
  layer.page_1based = page_1based;
  layer.page_bounds = TextRect{0, 0, static_cast<double>(sz->width),
                               static_cast<double>(sz->height)};

  // Wait for page text (may need to fetch page data).
  miniexp_t text = miniexp_dummy;
  while ((text = ddjvu_document_get_pagetext(doc, page_1based - 1, "word")) ==
         miniexp_dummy) {
    ddjvu_message_wait(g_djvu.ctx);
    pump_messages(g_djvu.ctx);
  }
  if (text && text != miniexp_nil && !miniexp_symbolp(text)) {
    walk_text_sexpr(text, layer.regions, sz->height);
    ddjvu_miniexp_release(doc, text);
  }

  miniexp_t anno = miniexp_dummy;
  while ((anno = ddjvu_document_get_pageanno(doc, page_1based - 1)) ==
         miniexp_dummy) {
    ddjvu_message_wait(g_djvu.ctx);
    pump_messages(g_djvu.ctx);
  }
  if (anno && anno != miniexp_nil && !miniexp_symbolp(anno)) {
    walk_anno_sexpr(anno, layer.regions);
    ddjvu_miniexp_release(doc, anno);
  }

  return layer;
#endif
}

std::optional<DocumentOutline> djvu_document_outline(
    const std::filesystem::path& path) {
#if !defined(THUMTOO_HAVE_DJVU)
  (void)path;
  return std::nullopt;
#else
  std::lock_guard lock(g_djvu.mu);
  ddjvu_document_t* doc = cached_djvu_document_unlocked(path);
  if (!doc || !g_djvu.ctx) return std::nullopt;

  // Outline is document-level; try get_outline if available via anno/bookmarks.
  // ddjvu_document_get_outline exists in modern djvulibre (returns miniexp).
  miniexp_t root = miniexp_dummy;
  // ddjvu_document_get_outline is part of modern ddjvuapi (bookmarks / NAVM).
  while ((root = ddjvu_document_get_outline(doc)) == miniexp_dummy) {
    ddjvu_message_wait(g_djvu.ctx);
    pump_messages(g_djvu.ctx);
  }
  DocumentOutline out;
  if (root && root != miniexp_nil && !miniexp_symbolp(root)) {
    walk_outline_sexpr(root, 1, out);
    ddjvu_miniexp_release(doc, root);
  }
  return out;
#endif
}


}  // namespace thumtoo
