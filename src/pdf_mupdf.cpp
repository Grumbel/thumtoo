// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/pdf_mupdf.hpp"
#include "thumtoo/constants.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <tuple>

#if defined(THUMTOO_HAVE_MUPDF)
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
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
  fz_var(doc);
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
  fz_var(page);
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
  // n==1/2 grey(+alpha), n==3/4 rgb(+alpha); reject empty or exotic.
  if (w <= 0 || h <= 0 || n < 1 || n > 4) return std::nullopt;

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
    } else {  // n == 1 || n == 2
      for (int x = 0; x < w; ++x) {
        const unsigned char g = row[x * n];
        dst[x * 3 + 0] = g;
        dst[x * 3 + 1] = g;
        dst[x * 3 + 2] = g;
      }
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
  fz_var(n);
  fz_try(ctx) { n = fz_count_pages(ctx, doc); }
  fz_catch(ctx) { n = 0; }
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

  fz_rect box = fz_empty_rect;
  fz_var(box);
  int ok = 0;
  fz_var(ok);
  fz_try(ctx) {
    box = fz_bound_page(ctx, page);
    ok = 1;
  }
  fz_catch(ctx) { ok = 0; }
  if (!ok) return st;
  const double page_w = std::max(1.0, static_cast<double>(box.x1 - box.x0));
  const double page_h = std::max(1.0, static_cast<double>(box.y1 - box.y0));
  const double page_area = page_w * page_h;

  // One structured-text pass: characters + image blocks (with bboxes).
  fz_stext_options opts{};
  opts.flags = 0;
  fz_stext_page* stext = nullptr;
  fz_var(stext);
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
  fz_matrix ctm = fz_scale(scale, scale);

  fz_pixmap* pix = nullptr;
  fz_var(pix);
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
  // Pixmap origin at (px,py) in *device* space (pixels after ctm). Identity
  // draw-device transform maps device coords onto this pixmap.
  fz_irect bbox;
  bbox.x0 = px;
  bbox.y0 = py;
  bbox.x1 = px + pw;
  bbox.y1 = py + ph;
  // fz_run_display_list scissor is in *device* space (same as pixmap), not
  // page points. Passing page-space (px/s) made bottom tiles miss the
  // pixmap entirely → solid white cells at scales where s != 1.
  fz_rect clip = fz_rect_from_irect(bbox);

  // fz_var: used inside fz_try; GCC -Wclobbered otherwise (even when set
  // before the try — address escape forces memory backing).
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
    // MuPDF: fz_new_draw_device(ctx, transform, dest) — transform maps
    // device calls into pixmap space; list run supplies page→pixel ctm.
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


/// True for a usable embedded raster Image XObject (not a stencil ImageMask).
[[nodiscard]] bool mupdf_obj_is_raster_image(fz_context* ctx, pdf_obj* obj) {
  if (!ctx || !obj) return false;
  obj = pdf_resolve_indirect(ctx, obj);
  if (!pdf_name_eq(ctx, pdf_dict_get(ctx, obj, PDF_NAME(Subtype)), PDF_NAME(Image))) {
    return false;
  }
  // Stencil / soft-mask bitmaps are Subtype Image with ImageMask true — skip.
  if (pdf_dict_get_bool(ctx, obj, PDF_NAME(ImageMask))) {
    return false;
  }
  const int w = pdf_to_int(ctx, pdf_dict_get(ctx, obj, PDF_NAME(Width)));
  const int h = pdf_to_int(ctx, pdf_dict_get(ctx, obj, PDF_NAME(Height)));
  // Reject empty / broken dicts (mutool extract still lists them; we need pixels).
  return w > 0 && h > 0;
}

/// mutool-style: walk the whole xref for Image objects (stable object-number order).
/// Page-resource walks miss images only referenced from deeper forms / content and
/// can disagree with load. Returns 1-based index → object number, or 0.
[[nodiscard]] int mupdf_find_embedded_image_objnum(fz_context* ctx, pdf_document* pdf,
                                                   fz_document* /*doc*/, int image_1based) {
  if (!ctx || !pdf || image_1based < 1) return 0;
  int found_num = 0;
  int seen = 0;
  int xref_len = 0;
  int num = 0;
  fz_var(found_num);
  fz_var(seen);
  fz_var(xref_len);
  fz_var(num);
  fz_try(ctx) { xref_len = pdf_xref_len(ctx, pdf); }
  fz_catch(ctx) { return 0; }

  for (num = 1; num < xref_len && found_num == 0; ++num) {
    pdf_obj* obj = nullptr;
    fz_var(obj);
    fz_try(ctx) {
      obj = pdf_load_object(ctx, pdf, num);
      if (mupdf_obj_is_raster_image(ctx, obj)) {
        ++seen;
        if (seen == image_1based) {
          found_num = num;
        }
      }
    }
    fz_always(ctx) {
      if (obj) pdf_drop_obj(ctx, obj);
    }
    fz_catch(ctx) { /* skip broken object */ }
  }
  return found_num;
}

std::optional<int> mupdf_embedded_image_count(const std::filesystem::path& path) {
#if !defined(THUMTOO_HAVE_MUPDF)
  (void)path;
  return std::nullopt;
#else
  fz_context* ctx = tls_ctx();
  fz_document* doc = tls_document(path);
  if (!ctx || !doc) return std::nullopt;
  pdf_document* pdf = pdf_document_from_fz_document(ctx, doc);
  if (!pdf) return std::nullopt;

  int total = 0;
  int xref_len = 0;
  int num = 0;
  fz_var(total);
  fz_var(xref_len);
  fz_var(num);
  fz_try(ctx) { xref_len = pdf_xref_len(ctx, pdf); }
  fz_catch(ctx) { return std::nullopt; }

  for (num = 1; num < xref_len; ++num) {
    pdf_obj* obj = nullptr;
    fz_var(obj);
    fz_try(ctx) {
      obj = pdf_load_object(ctx, pdf, num);
      if (mupdf_obj_is_raster_image(ctx, obj)) {
        ++total;
      }
    }
    fz_always(ctx) {
      if (obj) pdf_drop_obj(ctx, obj);
    }
    fz_catch(ctx) { /* skip */ }
  }
  return total;
#endif
}

std::optional<PdfRaster> mupdf_rasterize_embedded_image(
    const std::filesystem::path& path, int image_1based, int max_edge) {
#if !defined(THUMTOO_HAVE_MUPDF)
  (void)path;
  (void)image_1based;
  (void)max_edge;
  return std::nullopt;
#else
  if (image_1based < 1) return std::nullopt;
  fz_context* ctx = tls_ctx();
  fz_document* doc = tls_document(path);
  if (!ctx || !doc) return std::nullopt;
  pdf_document* pdf = pdf_document_from_fz_document(ctx, doc);
  if (!pdf) return std::nullopt;

  const int objnum = mupdf_find_embedded_image_objnum(ctx, pdf, doc, image_1based);
  if (objnum <= 0) return std::nullopt;

  pdf_obj* target = nullptr;
  fz_image* image = nullptr;
  fz_pixmap* pix = nullptr;
  fz_var(target);
  fz_var(image);
  fz_var(pix);
  std::optional<PdfRaster> out;
  fz_try(ctx) {
    // mutool extract uses an indirect ref; load_object alone is not enough for
    // all stream Image XObjects across MuPDF versions.
    target = pdf_new_indirect(ctx, pdf, objnum, 0);
    if (!target) {
      fz_throw(ctx, FZ_ERROR_GENERIC, "pdfimage: missing object %d", objnum);
    }
    image = pdf_load_image(ctx, pdf, target);
    // Native resolution pixmap (identity matrix / full image).
    pix = fz_get_pixmap_from_image(ctx, image, nullptr, nullptr, nullptr, nullptr);
    if (pix) {
      // Convert CMYK/etc.; leave DeviceGray and DeviceRGB as-is for pixmap_to_rgb.
      fz_colorspace* cs = fz_pixmap_colorspace(ctx, pix);
      if (cs && fz_colorspace_n(ctx, cs) != 3 && fz_colorspace_n(ctx, cs) != 1) {
        fz_pixmap* rgb = fz_convert_pixmap(ctx, pix, fz_device_rgb(ctx), nullptr,
                                           nullptr, fz_default_color_params, 0);
        fz_drop_pixmap(ctx, pix);
        pix = rgb;
      }
      if (max_edge > 0 && pix) {
        const int w = fz_pixmap_width(ctx, pix);
        const int h = fz_pixmap_height(ctx, pix);
        const int long_edge = std::max(w, h);
        if (long_edge > max_edge) {
          const float scale =
              static_cast<float>(max_edge) / static_cast<float>(long_edge);
          const int nw = std::max(1, static_cast<int>(std::lround(w * scale)));
          const int nh = std::max(1, static_cast<int>(std::lround(h * scale)));
          fz_pixmap* scaled = fz_new_pixmap(ctx, fz_pixmap_colorspace(ctx, pix),
                                            nw, nh, nullptr, 0);
          if (scaled) {
            fz_clear_pixmap_with_value(ctx, scaled, 0);
            const int n = fz_pixmap_components(ctx, pix);
            const unsigned char* src = fz_pixmap_samples(ctx, pix);
            unsigned char* dst = fz_pixmap_samples(ctx, scaled);
            for (int y = 0; y < nh; ++y) {
              const int sy = std::min(h - 1, static_cast<int>(y / scale));
              for (int x = 0; x < nw; ++x) {
                const int sx = std::min(w - 1, static_cast<int>(x / scale));
                const unsigned char* s =
                    src + (static_cast<size_t>(sy) * w + sx) * n;
                unsigned char* d =
                    dst + (static_cast<size_t>(y) * nw + x) * n;
                for (int c = 0; c < n; ++c) d[c] = s[c];
              }
            }
            fz_drop_pixmap(ctx, pix);
            pix = scaled;
          }
        }
      }
      out = pixmap_to_rgb(ctx, pix);
    }
  }
  fz_always(ctx) {
    if (pix) fz_drop_pixmap(ctx, pix);
    if (image) fz_drop_image(ctx, image);
    if (target) pdf_drop_obj(ctx, target);
  }
  fz_catch(ctx) { out = std::nullopt; }
  return out;
#endif
}

std::optional<Size> mupdf_embedded_image_size(const std::filesystem::path& path,
                                              int image_1based) {
#if !defined(THUMTOO_HAVE_MUPDF)
  (void)path;
  (void)image_1based;
  return std::nullopt;
#else
  if (image_1based < 1) return std::nullopt;
  fz_context* ctx = tls_ctx();
  fz_document* doc = tls_document(path);
  if (!ctx || !doc) return std::nullopt;
  pdf_document* pdf = pdf_document_from_fz_document(ctx, doc);
  if (!pdf) return std::nullopt;

  const int objnum = mupdf_find_embedded_image_objnum(ctx, pdf, doc, image_1based);
  if (objnum <= 0) return std::nullopt;

  pdf_obj* target = nullptr;
  std::optional<Size> out;
  fz_var(target);
  fz_try(ctx) {
    target = pdf_new_indirect(ctx, pdf, objnum, 0);
    if (!target) {
      fz_throw(ctx, FZ_ERROR_GENERIC, "pdfimage: missing object %d", objnum);
    }
    pdf_obj* resolved = pdf_resolve_indirect(ctx, target);
    // Prefer dictionary /Width /Height (no stream decode). Fallback to fz_image.
    const int w = pdf_to_int(ctx, pdf_dict_get(ctx, resolved, PDF_NAME(Width)));
    const int h = pdf_to_int(ctx, pdf_dict_get(ctx, resolved, PDF_NAME(Height)));
    if (w > 0 && h > 0) {
      out = Size{w, h};
    } else {
      fz_image* image = pdf_load_image(ctx, pdf, target);
      if (image) {
        out = Size{image->w, image->h};
        fz_drop_image(ctx, image);
      }
    }
  }
  fz_always(ctx) {
    if (target) pdf_drop_obj(ctx, target);
  }
  fz_catch(ctx) { out = std::nullopt; }
  return out;
#endif
}


std::optional<PageTextLayer> mupdf_page_text_layer(const std::filesystem::path& path,
                                                   int page_1based) {
#if !defined(THUMTOO_HAVE_MUPDF)
  (void)path;
  (void)page_1based;
  return std::nullopt;
#else
  if (page_1based < 1) return std::nullopt;
  fz_context* ctx = tls_ctx();
  fz_page* page = tls_page(path, page_1based);
  if (!ctx || !page) return std::nullopt;

  PageTextLayer layer;
  layer.page_1based = page_1based;

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
  layer.page_bounds = TextRect{box.x0, box.y0, box.x1, box.y1};

  // Text: one structured-text pass → line-level regions (good for search/select).
  fz_stext_options opts{};
  opts.flags = 0;
  fz_stext_page* stext = nullptr;
  fz_var(stext);
  fz_try(ctx) { stext = fz_new_stext_page_from_page(ctx, page, &opts); }
  fz_catch(ctx) { stext = nullptr; }
  if (stext) {
    for (fz_stext_block* block = stext->first_block; block; block = block->next) {
      if (block->type != FZ_STEXT_BLOCK_TEXT) continue;
      for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
        std::string line_text;
        line_text.reserve(64);
        for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
          if (ch->c == 0) continue;
          // Encode Unicode codepoint as UTF-8.
          const int c = ch->c;
          if (c < 0x80) {
            line_text.push_back(static_cast<char>(c));
          } else if (c < 0x800) {
            line_text.push_back(static_cast<char>(0xC0 | (c >> 6)));
            line_text.push_back(static_cast<char>(0x80 | (c & 0x3F)));
          } else if (c < 0x10000) {
            line_text.push_back(static_cast<char>(0xE0 | (c >> 12)));
            line_text.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            line_text.push_back(static_cast<char>(0x80 | (c & 0x3F)));
          } else {
            line_text.push_back(static_cast<char>(0xF0 | (c >> 18)));
            line_text.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            line_text.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            line_text.push_back(static_cast<char>(0x80 | (c & 0x3F)));
          }
        }
        // Trim trailing whitespace-only lines.
        while (!line_text.empty() &&
               (line_text.back() == ' ' || line_text.back() == '\t' ||
                line_text.back() == '\r' || line_text.back() == '\n')) {
          line_text.pop_back();
        }
        if (line_text.empty()) continue;

        TextRegion reg;
        reg.role = TextRegionRole::Text;
        reg.text = std::move(line_text);
        reg.bbox = TextRect{line->bbox.x0, line->bbox.y0, line->bbox.x1, line->bbox.y1};
        if (!reg.bbox.empty()) layer.regions.push_back(std::move(reg));
      }
    }
    fz_drop_stext_page(ctx, stext);
  }

  // Links: separate pass. Snapshot first; resolve with POD-only helper.
  fz_link* links = nullptr;
  fz_var(links);
  fz_try(ctx) { links = fz_load_links(ctx, page); }
  fz_catch(ctx) { links = nullptr; }
  struct LinkSnap {
    TextRect bbox;
    std::string uri;
  };
  std::vector<LinkSnap> snaps;
  for (fz_link* link = links; link; link = link->next) {
    LinkSnap s;
    s.bbox = TextRect{link->rect.x0, link->rect.y0, link->rect.x1, link->rect.y1};
    if (s.bbox.empty()) continue;
    if (link->uri) s.uri = link->uri;
    snaps.push_back(std::move(s));
  }
  if (links) {
    fz_drop_link(ctx, links);
    links = nullptr;
  }
  fz_document* doc = tls_document(path);
  for (std::size_t i = 0; i < snaps.size(); ++i) {
    // Copy fields before any fz_try so no reference lives across longjmp.
    const TextRect bbox = snaps[i].bbox;
    const std::string uri = snaps[i].uri;
    TextRegion reg;
    reg.role = TextRegionRole::Link;
    reg.bbox = bbox;
    if (!uri.empty() && uri[0] == '#') {
      int dest_page = -1;
      float lx = 0, ly = 0;
      if (resolve_hash_link_page(ctx, doc, uri.c_str(), &dest_page, &lx, &ly)) {
        reg.target.kind = TextLinkTargetKind::InternalPage;
        reg.target.page_1based = dest_page + 1;
        reg.target.x = static_cast<double>(lx);
        reg.target.y = static_cast<double>(ly);
      } else {
        int page_num = 0;
        if (std::sscanf(uri.c_str(), "#page=%d", &page_num) == 1 && page_num >= 1) {
          reg.target.kind = TextLinkTargetKind::InternalPage;
          reg.target.page_1based = page_num;
        } else {
          reg.target.kind = TextLinkTargetKind::Uri;
          reg.target.uri = uri;
        }
      }
    } else if (!uri.empty()) {
      reg.target.kind = TextLinkTargetKind::Uri;
      reg.target.uri = uri;
    }
    layer.regions.push_back(std::move(reg));
  }

  return layer;
#endif
}

namespace {

#if defined(THUMTOO_HAVE_MUPDF)

[[nodiscard]] bool resolve_hash_link_page(fz_context* ctx, fz_document* doc,
                                          const char* uri, int* page_0based,
                                          float* x_out, float* y_out) {
  if (!ctx || !doc || !uri || uri[0] != '#') return false;
  int page = -1;
  float lx = 0, ly = 0;
  int ok = 0;
  fz_var(page);
  fz_var(lx);
  fz_var(ly);
  fz_var(ok);
  fz_try(ctx) {
    fz_location loc = fz_resolve_link(ctx, doc, uri, &lx, &ly);
    page = loc.page;
    ok = 1;
  }
  fz_catch(ctx) { ok = 0; }
  if (!ok || page < 0) return false;
  if (page_0based) *page_0based = page;
  if (x_out) *x_out = lx;
  if (y_out) *y_out = ly;
  return true;
}

void flatten_outline(fz_outline* node, int level,
                     std::vector<std::tuple<int, std::string, std::string>>& out) {
  // No fz_try here — only walk the tree.
  for (; node; node = node->next) {
    std::string title = node->title ? node->title : "";
    std::string uri = node->uri ? node->uri : "";
    out.emplace_back(level, std::move(title), std::move(uri));
    if (node->down) flatten_outline(node->down, level + 1, out);
  }
}

void append_outline(fz_context* ctx, fz_document* doc, fz_outline* root, int /*level*/,
                    DocumentOutline& out) {
  std::vector<std::tuple<int, std::string, std::string>> snaps;
  flatten_outline(root, 1, snaps);
  for (std::size_t i = 0; i < snaps.size(); ++i) {
    const int level = std::get<0>(snaps[i]);
    const std::string title = std::get<1>(snaps[i]);
    const std::string uri = std::get<2>(snaps[i]);
    OutlineItem item;
    item.level = level;
    item.title = title;
    if (!uri.empty() && uri[0] == '#') {
      int dest_page = -1;
      float lx = 0, ly = 0;
      if (resolve_hash_link_page(ctx, doc, uri.c_str(), &dest_page, &lx, &ly)) {
        item.page_1based = dest_page + 1;
      } else {
        item.uri = uri;
      }
      (void)lx;
      (void)ly;
    } else if (!uri.empty()) {
      item.uri = uri;
    }
    out.items.push_back(std::move(item));
  }
}
#endif

}  // namespace

std::optional<DocumentOutline> mupdf_document_outline(
    const std::filesystem::path& path) {
#if !defined(THUMTOO_HAVE_MUPDF)
  (void)path;
  return std::nullopt;
#else
  fz_context* ctx = tls_ctx();
  fz_document* doc = tls_document(path);
  if (!ctx || !doc) return std::nullopt;

  fz_outline* root = nullptr;
  fz_var(root);
  fz_try(ctx) { root = fz_load_outline(ctx, doc); }
  fz_catch(ctx) { root = nullptr; }
  if (!root) {
    // Empty outline is valid (document has none).
    return DocumentOutline{};
  }
  DocumentOutline out;
  append_outline(ctx, doc, root, 1, out);
  fz_drop_outline(ctx, root);
  return out;
#endif
}

}  // namespace thumtoo
