// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/pdf_mupdf.hpp"
#include "thumtoo/constants.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#if defined(THUMTOO_HAVE_MUPDF)
#include <mupdf/fitz.h>
#endif

namespace thumtoo {
namespace {

#if defined(THUMTOO_HAVE_MUPDF)

struct TlsMupdf {
  fz_context* ctx = nullptr;
  std::string path_key;
  std::filesystem::file_time_type mtime{};
  fz_document* doc = nullptr;
  int page_index = -1;  // 0-based loaded page
  fz_page* page = nullptr;
  fz_display_list* list = nullptr;
};

thread_local TlsMupdf g_tls;

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

fz_document* tls_document(const std::filesystem::path& path) {
  fz_context* ctx = tls_ctx();
  if (!ctx) return nullptr;

  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(path, ec);
  const std::string key = path.lexically_normal().string();
  if (g_tls.doc && g_tls.path_key == key && !ec && g_tls.mtime == mtime) {
    return g_tls.doc;
  }

  tls_drop_doc();
  fz_document* doc = nullptr;
  fz_try(ctx) { doc = fz_open_document(ctx, path.string().c_str()); }
  fz_catch(ctx) { doc = nullptr; }
  if (!doc) return nullptr;

  g_tls.path_key = key;
  g_tls.mtime = ec ? std::filesystem::file_time_type{} : mtime;
  g_tls.doc = doc;
  return doc;
}

fz_page* tls_page(const std::filesystem::path& path, int page_1based) {
  if (page_1based < 1) return nullptr;
  fz_context* ctx = tls_ctx();
  fz_document* doc = tls_document(path);
  if (!ctx || !doc) return nullptr;

  const int idx = page_1based - 1;
  if (g_tls.page && g_tls.page_index == idx) return g_tls.page;

  tls_drop_page();
  const int n = fz_count_pages(ctx, doc);
  if (idx < 0 || idx >= n) return nullptr;

  fz_page* page = nullptr;
  fz_try(ctx) { page = fz_load_page(ctx, doc, idx); }
  fz_catch(ctx) { page = nullptr; }
  if (!page) return nullptr;

  g_tls.page = page;
  g_tls.page_index = idx;
  return page;
}

/// Display list for the current page (built once, reused for region tiles).
fz_display_list* tls_display_list(const std::filesystem::path& path,
                                  int page_1based) {
  fz_context* ctx = tls_ctx();
  fz_page* page = tls_page(path, page_1based);
  if (!ctx || !page) return nullptr;
  if (g_tls.list) return g_tls.list;

  fz_display_list* list = nullptr;
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

std::optional<int> mupdf_page_count(const std::filesystem::path& path) {
#if !defined(THUMTOO_HAVE_MUPDF)
  (void)path;
  return std::nullopt;
#else
  fz_context* ctx = tls_ctx();
  fz_document* doc = tls_document(path);
  if (!ctx || !doc) return std::nullopt;
  int n = 0;
  fz_try(ctx) { n = fz_count_pages(ctx, doc); }
  fz_catch(ctx) { return std::nullopt; }
  if (n <= 0) return std::nullopt;
  return n;
#endif
}

std::optional<Size> mupdf_page_size_72dpi(const std::filesystem::path& path,
                                          int page_1based) {
#if !defined(THUMTOO_HAVE_MUPDF)
  (void)path;
  (void)page_1based;
  return std::nullopt;
#else
  fz_context* ctx = tls_ctx();
  fz_page* page = tls_page(path, page_1based);
  if (!ctx || !page) return std::nullopt;
  fz_rect box;
  fz_try(ctx) { box = fz_bound_page(ctx, page); }
  fz_catch(ctx) { return std::nullopt; }
  const int w = std::max(1, static_cast<int>(std::lround(box.x1 - box.x0)));
  const int h = std::max(1, static_cast<int>(std::lround(box.y1 - box.y0)));
  return Size{w, h};
#endif
}

PdfPageContentStats mupdf_page_content_stats(const std::filesystem::path& path,
                                             int page_1based) {
  PdfPageContentStats st;
#if !defined(THUMTOO_HAVE_MUPDF)
  (void)path;
  (void)page_1based;
  return st;
#else
  fz_context* ctx = tls_ctx();
  fz_page* page = tls_page(path, page_1based);
  if (!ctx || !page) return st;

  fz_rect box;
  fz_try(ctx) { box = fz_bound_page(ctx, page); }
  fz_catch(ctx) { return st; }
  const double page_w = std::max(1.0, box.x1 - box.x0);
  const double page_h = std::max(1.0, box.y1 - box.y0);
  const double page_area = page_w * page_h;

  // One structured-text pass: characters + image blocks (with bboxes).
  fz_stext_options opts{};
  opts.flags = 0;
  fz_stext_page* stext = nullptr;
  fz_try(ctx) {
    stext = fz_new_stext_page_from_page(ctx, page, &opts);
  }
  fz_catch(ctx) { stext = nullptr; }
  if (stext) {
    for (fz_stext_block* block = stext->first_block; block; block = block->next) {
      if (block->type == FZ_STEXT_BLOCK_TEXT) {
        for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
          for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
            if (ch->c > 32) ++st.text_chars;
          }
        }
      } else if (block->type == FZ_STEXT_BLOCK_IMAGE) {
        ++st.image_count;
        const double w = std::abs(static_cast<double>(block->bbox.x1 - block->bbox.x0));
        const double h = std::abs(static_cast<double>(block->bbox.y1 - block->bbox.y0));
        if (w > 0 && h > 0) {
          st.image_coverage = (st.image_coverage < 0 ? 0 : st.image_coverage) +
                              (w * h) / page_area;
        }
      }
    }
    fz_drop_stext_page(ctx, stext);
  }

  if (st.image_coverage < 0) {
    st.image_coverage = 0;
  }
  st.image_coverage = std::min(1.0, st.image_coverage);

  st.image_heavy = st.image_coverage >= kPdfImageHeavyCoverage;
  if (st.image_count == 1 && st.image_coverage >= 0.35) {
    st.image_heavy = true;
  }
  if (st.image_count == 0) {
    const double text_density = static_cast<double>(st.text_chars) / page_area;
    if (text_density < kPdfSparseTextPerPoint2) {
      st.image_heavy = true;
    }
  }
  return st;
#endif
}

std::optional<PdfRaster> mupdf_rasterize_page(const std::filesystem::path& path,
                                              int page_1based, int max_edge) {
#if !defined(THUMTOO_HAVE_MUPDF)
  (void)path;
  (void)page_1based;
  (void)max_edge;
  return std::nullopt;
#else
  if (max_edge < 1) return std::nullopt;
  fz_context* ctx = tls_ctx();
  fz_page* page = tls_page(path, page_1based);
  if (!ctx || !page) return std::nullopt;

  fz_rect box;
  fz_try(ctx) { box = fz_bound_page(ctx, page); }
  fz_catch(ctx) { return std::nullopt; }
  const double pw = std::max(1.0, box.x1 - box.x0);
  const double ph = std::max(1.0, box.y1 - box.y0);
  const double long_pt = std::max(pw, ph);
  const double scale = static_cast<double>(max_edge) / long_pt;
  fz_matrix ctm = fz_scale(scale, scale);

  fz_pixmap* pix = nullptr;
  fz_try(ctx) {
    pix = fz_new_pixmap_from_page(ctx, page, ctm, fz_device_rgb(ctx), 0);
  }
  fz_catch(ctx) { pix = nullptr; }
  if (!pix) return std::nullopt;
  auto out = pixmap_to_rgb(ctx, pix);
  fz_drop_pixmap(ctx, pix);
  return out;
#endif
}

std::optional<PdfRaster> mupdf_rasterize_page_region(
    const std::filesystem::path& path, int page_1based, double dpi, int px,
    int py, int pw, int ph) {
#if !defined(THUMTOO_HAVE_MUPDF)
  (void)path;
  (void)page_1based;
  (void)dpi;
  (void)px;
  (void)py;
  (void)pw;
  (void)ph;
  return std::nullopt;
#else
  if (page_1based < 1 || pw <= 0 || ph <= 0 || dpi <= 0.0) return std::nullopt;
  fz_context* ctx = tls_ctx();
  fz_display_list* list = tls_display_list(path, page_1based);
  if (!ctx || !list) return std::nullopt;

  const float s = static_cast<float>(dpi / 72.0);
  fz_matrix ctm = fz_scale(s, s);
  // Clip in page space (points); pixmap is only the tile in pixel space.
  fz_rect clip;
  clip.x0 = static_cast<float>(px) / s;
  clip.y0 = static_cast<float>(py) / s;
  clip.x1 = static_cast<float>(px + pw) / s;
  clip.y1 = static_cast<float>(py + ph) / s;

  fz_irect bbox;
  bbox.x0 = 0;
  bbox.y0 = 0;
  bbox.x1 = pw;
  bbox.y1 = ph;

  // page_pt * scale → pixels, then shift so (px,py) maps to pixmap origin.
  fz_matrix draw =
      fz_concat(fz_translate(-static_cast<float>(px), -static_cast<float>(py)), ctm);

  fz_pixmap* pix = nullptr;
  fz_device* dev = nullptr;
  fz_try(ctx) {
    pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), bbox, nullptr, 0);
    fz_clear_pixmap_with_value(ctx, pix, 0xff);
    dev = fz_new_draw_device_with_bbox(ctx, nullptr, pix, nullptr, bbox);
    fz_run_display_list(ctx, list, dev, draw, clip, nullptr);
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
    return std::nullopt;
  }

  if (!pix) return std::nullopt;
  auto out = pixmap_to_rgb(ctx, pix);
  fz_drop_pixmap(ctx, pix);
  return out;
#endif
}

std::optional<PdfRaster> mupdf_render_tile_cell(const std::filesystem::path& path,
                                                 int page_1based, int scale,
                                                 int x, int y) {
  if (x < 0 || y < 0) return std::nullopt;
  auto s72 = mupdf_page_size_72dpi(path, page_1based);
  if (!s72 || s72->width <= 0 || s72->height <= 0) return std::nullopt;

  // Match Poppler layout size: media box scaled to kPdfLayoutDpi.
  const double layout_scale = static_cast<double>(kPdfLayoutDpi) / 72.0;
  Size layout{
      std::max(1, static_cast<int>(std::lround(s72->width * layout_scale))),
      std::max(1, static_cast<int>(std::lround(s72->height * layout_scale)))};
  const Size full = pdf_page_size_at_scale(layout, scale);
  const int left = x * kTileSize;
  const int top = y * kTileSize;
  if (left >= full.width || top >= full.height) return std::nullopt;
  const int tw = std::min(kTileSize, full.width - left);
  const int th = std::min(kTileSize, full.height - top);
  if (tw <= 0 || th <= 0) return std::nullopt;

  if (scale < kPdfMinLiveTileScaleImageHeavy) {
    auto st = mupdf_page_content_stats(path, page_1based);
    if (st.image_heavy) return std::nullopt;
  }

  const double dpi = pdf_dpi_for_scale(scale);
  return mupdf_rasterize_page_region(path, page_1based, dpi, left, top, tw, th);
}

}  // namespace thumtoo
