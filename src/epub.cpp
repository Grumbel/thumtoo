// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/epub.hpp"
#include "thumtoo/format.hpp"
#include "thumtoo/uri.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(THUMTOO_HAVE_MUPDF)
#include <mupdf/fitz.h>
#endif

namespace thumtoo {
namespace {

#if defined(THUMTOO_HAVE_MUPDF)

struct TlsEpub {
  fz_context* ctx = nullptr;
  std::string path_key;
  std::filesystem::file_time_type mtime{};
  int width_px = 0;
  int height_px = 0;
  int fs_pt = 0;
  int mt_px = 0;
  int mr_px = 0;
  int mb_px = 0;
  int ml_px = 0;
  fz_document* doc = nullptr;
  bool laid_out = false;
  int page_index = -1;
  fz_page* page = nullptr;
  fz_display_list* list = nullptr;
};

thread_local TlsEpub g_tls;

void tls_drop_page() {
  if (!g_tls.ctx) return;
  if (g_tls.list) {
    fz_drop_display_list(g_tls.ctx, g_tls.list);
    g_tls.list = nullptr;
  }
  if (g_tls.page) {
    fz_drop_page(g_tls.ctx, g_tls.page);
    g_tls.page = nullptr;
  }
  g_tls.page_index = -1;
}

void tls_drop_doc() {
  tls_drop_page();
  if (g_tls.ctx && g_tls.doc) {
    fz_drop_document(g_tls.ctx, g_tls.doc);
    g_tls.doc = nullptr;
  }
  g_tls.path_key.clear();
  g_tls.laid_out = false;
  g_tls.width_px = 0;
  g_tls.height_px = 0;
  g_tls.fs_pt = 0;
  g_tls.mt_px = 0;
  g_tls.mr_px = 0;
  g_tls.mb_px = 0;
  g_tls.ml_px = 0;
}

fz_context* tls_ctx() {
  if (!g_tls.ctx) {
    g_tls.ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (g_tls.ctx) {
      fz_try(g_tls.ctx) { fz_register_document_handlers(g_tls.ctx); }
      fz_catch(g_tls.ctx) {
        fz_drop_context(g_tls.ctx);
        g_tls.ctx = nullptr;
      }
    }
  }
  return g_tls.ctx;
}

fz_document* tls_document(const std::filesystem::path& path,
                          const EpubLayout& layout) {
  fz_context* ctx = tls_ctx();
  if (!ctx) return nullptr;

  EpubLayout L = layout;
  if (L.width_px < 1) L.width_px = kEpubDefaultPageWidthPx;
  if (L.height_px < 1) L.height_px = kEpubDefaultPageHeightPx;
  if (L.fs_pt < 1) L.fs_pt = kEpubDefaultFontSizePt;
  if (L.mt_px < 0) L.mt_px = 0;
  if (L.mr_px < 0) L.mr_px = 0;
  if (L.mb_px < 0) L.mb_px = 0;
  if (L.ml_px < 0) L.ml_px = 0;

  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(path, ec);
  const std::string key = path.lexically_normal().string();
  if (g_tls.doc && g_tls.path_key == key && !ec && g_tls.mtime == mtime &&
      g_tls.width_px == L.width_px && g_tls.height_px == L.height_px &&
      g_tls.fs_pt == L.fs_pt && g_tls.mt_px == L.mt_px && g_tls.mr_px == L.mr_px &&
      g_tls.mb_px == L.mb_px && g_tls.ml_px == L.ml_px && g_tls.laid_out) {
    return g_tls.doc;
  }

  tls_drop_doc();
  fz_document* doc = nullptr;
  fz_var(doc);
  fz_try(ctx) { doc = fz_open_document(ctx, path.string().c_str()); }
  fz_catch(ctx) { doc = nullptr; }
  if (!doc) return nullptr;

  // URI w/h/margins are pixels at kEpubLayoutDpi; MuPDF wants points.
  const float dpi = static_cast<float>(kEpubLayoutDpi);
  const float width_pt = static_cast<float>(L.width_px) * 72.f / dpi;
  const float height_pt = static_cast<float>(L.height_px) * 72.f / dpi;
  const float mt_pt = static_cast<float>(L.mt_px) * 72.f / dpi;
  const float mr_pt = static_cast<float>(L.mr_px) * 72.f / dpi;
  const float mb_pt = static_cast<float>(L.mb_px) * 72.f / dpi;
  const float ml_pt = static_cast<float>(L.ml_px) * 72.f / dpi;

  // Per-side margins via user CSS (last in cascade). Clear when all zero so
  // a prior book with margins does not leak into the next open on this TLS ctx.
  {
    char css[192];
    if (L.mt_px > 0 || L.mr_px > 0 || L.mb_px > 0 || L.ml_px > 0) {
      std::snprintf(css, sizeof(css),
                    "body { margin: %gpt %gpt %gpt %gpt !important; }",
                    static_cast<double>(mt_pt), static_cast<double>(mr_pt),
                    static_cast<double>(mb_pt), static_cast<double>(ml_pt));
      fz_set_user_css(ctx, css);
    } else {
      fz_set_user_css(ctx, "");
    }
  }

  int ok = 0;
  fz_var(ok);
  fz_try(ctx) {
    fz_layout_document(ctx, doc, width_pt, height_pt,
                       static_cast<float>(L.fs_pt));
    ok = 1;
  }
  fz_catch(ctx) { ok = 0; }
  if (!ok) {
    fz_drop_document(ctx, doc);
    return nullptr;
  }

  g_tls.path_key = key;
  g_tls.mtime = ec ? std::filesystem::file_time_type{} : mtime;
  g_tls.width_px = L.width_px;
  g_tls.height_px = L.height_px;
  g_tls.fs_pt = L.fs_pt;
  g_tls.mt_px = L.mt_px;
  g_tls.mr_px = L.mr_px;
  g_tls.mb_px = L.mb_px;
  g_tls.ml_px = L.ml_px;
  g_tls.doc = doc;
  g_tls.laid_out = true;
  return doc;
}

fz_page* tls_page(const std::filesystem::path& path, const EpubLayout& layout,
                  int page_1based) {
  if (page_1based < 1) return nullptr;
  fz_context* ctx = tls_ctx();
  fz_document* doc = tls_document(path, layout);
  if (!ctx || !doc) return nullptr;

  const int idx = page_1based - 1;
  if (g_tls.page && g_tls.page_index == idx) return g_tls.page;

  tls_drop_page();
  int n = 0;
  fz_var(n);
  fz_try(ctx) { n = fz_count_pages(ctx, doc); }
  fz_catch(ctx) { n = 0; }
  if (idx < 0 || idx >= n) return nullptr;

  fz_page* page = nullptr;
  fz_var(page);
  fz_try(ctx) { page = fz_load_page(ctx, doc, idx); }
  fz_catch(ctx) { page = nullptr; }
  if (!page) return nullptr;

  g_tls.page = page;
  g_tls.page_index = idx;
  return page;
}

fz_display_list* tls_display_list(const std::filesystem::path& path,
                                  const EpubLayout& layout, int page_1based) {
  fz_context* ctx = tls_ctx();
  fz_page* page = tls_page(path, layout, page_1based);
  if (!ctx || !page) return nullptr;
  if (g_tls.list) return g_tls.list;

  fz_display_list* list = nullptr;
  fz_var(list);
  fz_try(ctx) { list = fz_new_display_list_from_page(ctx, page); }
  fz_catch(ctx) { list = nullptr; }
  g_tls.list = list;
  return list;
}

std::optional<PdfRaster> pixmap_to_rgb(fz_context* ctx, fz_pixmap* pix) {
  if (!ctx || !pix) return std::nullopt;
  const int w = fz_pixmap_width(ctx, pix);
  const int h = fz_pixmap_height(ctx, pix);
  const int n = fz_pixmap_components(ctx, pix);
  if (w <= 0 || h <= 0 || n < 3) return std::nullopt;

  PdfRaster out;
  out.width = w;
  out.height = h;
  out.rgb.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3u);
  const unsigned char* src = fz_pixmap_samples(ctx, pix);
  const int stride = fz_pixmap_stride(ctx, pix);
  for (int y = 0; y < h; ++y) {
    const unsigned char* row = src + static_cast<std::size_t>(y) * stride;
    std::uint8_t* dst =
        out.rgb.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 3u;
    if (n == 3 || n == 4) {
      for (int x = 0; x < w; ++x) {
        dst[x * 3 + 0] = row[x * n + 0];
        dst[x * 3 + 1] = row[x * n + 1];
        dst[x * 3 + 2] = row[x * n + 2];
      }
    } else if (n == 1 || n == 2) {
      for (int x = 0; x < w; ++x) {
        const unsigned char g = row[x * n];
        dst[x * 3 + 0] = g;
        dst[x * 3 + 1] = g;
        dst[x * 3 + 2] = g;
      }
    } else {
      return std::nullopt;
    }
  }
  return out;
}

#endif  // THUMTOO_HAVE_MUPDF

}  // namespace

bool epub_available() {
#if defined(THUMTOO_HAVE_MUPDF)
  return true;
#else
  return false;
#endif
}

bool is_likely_epub_path(const std::filesystem::path& path) {
  return is_epub_path(path);
}

std::optional<ParsedEpubUri> parse_epub_uri(std::string_view uri) {
  auto loc = parse_location(uri);
  if (!loc || loc->scheme != UriScheme::File) return std::nullopt;

  EpubLayout layout = default_epub_layout();
  int page = 0;
  bool saw_epub = false;
  for (const auto& pipe : loc->pipes) {
    if (pipe.kind == LocationPipeKind::EpubLayout) {
      layout = parse_epub_layout_params(pipe.value);
      saw_epub = true;
    } else if (pipe.kind == LocationPipeKind::PdfPage ||
               pipe.kind == LocationPipeKind::PdfPagePoppler ||
               pipe.kind == LocationPipeKind::PdfPageMupdf) {
      try {
        page = std::stoi(pipe.value);
      } catch (...) {
        return std::nullopt;
      }
    } else if (pipe.kind == LocationPipeKind::ArchiveRoot ||
               pipe.kind == LocationPipeKind::ArchiveMember) {
      return std::nullopt;
    }
  }
  if (!saw_epub || page < 1) return std::nullopt;

  std::filesystem::path path(loc->base);
  if (!is_epub_path(path)) return std::nullopt;

  ParsedEpubUri out;
  out.epub_path = std::move(path);
  out.layout = layout;
  out.page = page;
  return out;
}

std::string epub_page_uri(const std::filesystem::path& path, int page_1based,
                          const EpubLayout& layout) {
  auto base = with_epub_layout(file_uri_from_path(path.lexically_normal()), layout);
  return with_pdf_page(base, page_1based);
}

std::optional<int> epub_page_count(const std::filesystem::path& path,
                                   const EpubLayout& layout) {
#if !defined(THUMTOO_HAVE_MUPDF)
  (void)path;
  (void)layout;
  return std::nullopt;
#else
  fz_context* ctx = tls_ctx();
  fz_document* doc = tls_document(path, layout);
  if (!ctx || !doc) return std::nullopt;
  int n = 0;
  fz_var(n);
  fz_try(ctx) { n = fz_count_pages(ctx, doc); }
  fz_catch(ctx) { n = 0; }
  if (n <= 0) return std::nullopt;
  return n;
#endif
}

std::optional<Size> epub_page_layout_size(const std::filesystem::path& path,
                                          int page_1based,
                                          const EpubLayout& layout) {
#if !defined(THUMTOO_HAVE_MUPDF)
  (void)path;
  (void)page_1based;
  (void)layout;
  return std::nullopt;
#else
  fz_context* ctx = tls_ctx();
  fz_page* page = tls_page(path, layout, page_1based);
  if (!ctx || !page) return std::nullopt;
  fz_rect box = fz_empty_rect;
  fz_var(box);
  int ok = 0;
  fz_var(ok);
  fz_try(ctx) {
    box = fz_bound_page(ctx, page);
    ok = 1;
  }
  fz_catch(ctx) { ok = 0; }
  if (!ok) return std::nullopt;
  const double scale = static_cast<double>(kEpubLayoutDpi) / 72.0;
  const int w =
      std::max(1, static_cast<int>(std::lround((box.x1 - box.x0) * scale)));
  const int h =
      std::max(1, static_cast<int>(std::lround((box.y1 - box.y0) * scale)));
  return Size{w, h};
#endif
}


std::optional<PdfRaster> epub_rasterize_page(const std::filesystem::path& path,
                                             int page_1based,
                                             const EpubLayout& layout,
                                             int max_edge) {
#if !defined(THUMTOO_HAVE_MUPDF)
  (void)path;
  (void)page_1based;
  (void)layout;
  (void)max_edge;
  return std::nullopt;
#else
  if (max_edge < 1 || page_1based < 1) return std::nullopt;
  fz_context* ctx = tls_ctx();
  fz_page* page = tls_page(path, layout, page_1based);
  if (!ctx || !page) return std::nullopt;

  fz_rect box = fz_empty_rect;
  fz_var(box);
  int ok = 0;
  fz_var(ok);
  fz_try(ctx) {
    box = fz_bound_page(ctx, page);
    ok = 1;
  }
  fz_catch(ctx) { ok = 0; }
  if (!ok) return std::nullopt;
  const double pw = std::max(1.0, static_cast<double>(box.x1 - box.x0));
  const double ph = std::max(1.0, static_cast<double>(box.y1 - box.y0));
  const double long_pt = std::max(pw, ph);
  const double scale = static_cast<double>(max_edge) / long_pt;
  const int out_w = std::max(1, static_cast<int>(std::lround(pw * scale)));
  const int out_h = std::max(1, static_cast<int>(std::lround(ph * scale)));
  // dpi such that page maps to out_w x out_h
  const double dpi = 72.0 * scale;
  return epub_rasterize_page_region(path, page_1based, layout, dpi, 0, 0, out_w,
                                    out_h);
#endif
}

std::optional<PdfRaster> epub_rasterize_page_region(
    const std::filesystem::path& path, int page_1based, const EpubLayout& layout,
    double dpi, int px, int py, int pw, int ph) {
#if !defined(THUMTOO_HAVE_MUPDF)
  (void)path;
  (void)page_1based;
  (void)layout;
  (void)dpi;
  (void)px;
  (void)py;
  (void)pw;
  (void)ph;
  return std::nullopt;
#else
  if (page_1based < 1 || pw <= 0 || ph <= 0 || dpi <= 0.0) return std::nullopt;
  fz_context* ctx = tls_ctx();
  fz_display_list* list = tls_display_list(path, layout, page_1based);
  if (!ctx || !list) return std::nullopt;

  const float s = static_cast<float>(dpi / 72.0);
  fz_matrix ctm = fz_scale(s, s);
  fz_irect bbox;
  bbox.x0 = px;
  bbox.y0 = py;
  bbox.x1 = px + pw;
  bbox.y1 = py + ph;
  fz_rect clip = fz_rect_from_irect(bbox);

  fz_var(ctm);
  fz_var(clip);
  fz_var(bbox);

  fz_pixmap* pix = nullptr;
  fz_device* dev = nullptr;
  fz_var(pix);
  fz_var(dev);
  int failed = 0;
  fz_var(failed);
  fz_try(ctx) {
    pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), bbox, nullptr, 0);
    fz_clear_pixmap_with_value(ctx, pix, 0xff);
    dev = fz_new_draw_device(ctx, fz_identity, pix);
    fz_run_display_list(ctx, list, dev, ctm, clip, nullptr);
    fz_close_device(ctx, dev);
  }
  fz_always(ctx) {
    if (dev) {
      fz_drop_device(ctx, dev);
      dev = nullptr;
    }
  }
  fz_catch(ctx) {
    if (pix) {
      fz_drop_pixmap(ctx, pix);
      pix = nullptr;
    }
    failed = 1;
  }
  if (failed || !pix) return std::nullopt;
  auto out = pixmap_to_rgb(ctx, pix);
  fz_drop_pixmap(ctx, pix);
  return out;
#endif
}

std::optional<PdfRaster> epub_render_tile_cell(const std::filesystem::path& path,
                                               int page_1based,
                                               const EpubLayout& layout,
                                               int scale, int x, int y) {
  if (x < 0 || y < 0) return std::nullopt;
  auto layout_px = epub_page_layout_size(path, page_1based, layout);
  if (!layout_px || layout_px->width <= 0 || layout_px->height <= 0) {
    return std::nullopt;
  }
  const Size full = pdf_page_size_at_scale(*layout_px, scale);
  const int left = x * kTileSize;
  const int top = y * kTileSize;
  if (left >= full.width || top >= full.height) return std::nullopt;
  const int tw = std::min(kTileSize, full.width - left);
  const int th = std::min(kTileSize, full.height - top);
  if (tw <= 0 || th <= 0) return std::nullopt;

  const double dpi =
      static_cast<double>(kEpubLayoutDpi) * std::ldexp(1.0, -scale);
  return epub_rasterize_page_region(path, page_1based, layout, dpi, left, top,
                                    tw, th);
}

}  // namespace thumtoo
