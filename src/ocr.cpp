// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/ocr.hpp"

#include "thumtoo/djvu.hpp"
#include "thumtoo/pdf.hpp"
#include "thumtoo/uri.hpp"
#include "thumtoo/archive.hpp"
#include "thumtoo/image.hpp"
#include "thumtoo/format.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <filesystem>

#if defined(THUMTOO_HAVE_TESSERACT)
#include <vips/vips.h>
#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>
#endif

namespace thumtoo {
namespace {

constexpr int kDefaultOcrMaxEdge = 3000;

struct RgbPage {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgb;  // RGB888
  TextRect page_bounds;
  int page_1based = 0;
};


thread_local std::string g_ocr_last_error;

void set_ocr_error(std::string_view msg) {
  g_ocr_last_error.assign(msg);
}

void clear_ocr_error() { g_ocr_last_error.clear(); }

#if defined(THUMTOO_HAVE_TESSERACT)
/// Convert any VipsImage to contiguous RGB uchar for Tesseract.
[[nodiscard]] std::optional<RgbPage> vips_image_to_rgb_page(VipsImage* in) {
  if (!in) return std::nullopt;
  VipsImage* rgb = nullptr;
  if (vips_colourspace(in, &rgb, VIPS_INTERPRETATION_sRGB, nullptr) != 0 || !rgb) {
    set_ocr_error("vips_colourspace to sRGB failed");
    return std::nullopt;
  }
  if (vips_image_get_format(rgb) != VIPS_FORMAT_UCHAR) {
    VipsImage* casted = nullptr;
    if (vips_cast_uchar(rgb, &casted, nullptr) != 0 || !casted) {
      g_object_unref(rgb);
      set_ocr_error("vips_cast_uchar failed");
      return std::nullopt;
    }
    g_object_unref(rgb);
    rgb = casted;
  }
  int bands = vips_image_get_bands(rgb);
  if (bands > 3) {
    VipsImage* extr = nullptr;
    if (vips_extract_band(rgb, &extr, 0, "n", 3, nullptr) != 0 || !extr) {
      g_object_unref(rgb);
      set_ocr_error("vips_extract_band (drop alpha) failed");
      return std::nullopt;
    }
    g_object_unref(rgb);
    rgb = extr;
    bands = 3;
  }
  if (bands == 1) {
    // Greyscale → RGB by repeating the channel.
    VipsImage* joined = nullptr;
    VipsImage* ins[] = {rgb, rgb, rgb};
    if (vips_bandjoin(ins, &joined, 3, nullptr) != 0 || !joined) {
      g_object_unref(rgb);
      set_ocr_error("vips_bandjoin greyscale→RGB failed");
      return std::nullopt;
    }
    g_object_unref(rgb);
    rgb = joined;
    bands = 3;
  }
  if (bands == 2) {
    // LA → take L only then expand.
    VipsImage* L = nullptr;
    if (vips_extract_band(rgb, &L, 0, "n", 1, nullptr) != 0 || !L) {
      g_object_unref(rgb);
      set_ocr_error("vips_extract_band LA→L failed");
      return std::nullopt;
    }
    g_object_unref(rgb);
    VipsImage* joined = nullptr;
    VipsImage* ins[] = {L, L, L};
    if (vips_bandjoin(ins, &joined, 3, nullptr) != 0 || !joined) {
      g_object_unref(L);
      set_ocr_error("vips_bandjoin L→RGB failed");
      return std::nullopt;
    }
    g_object_unref(L);
    rgb = joined;
    bands = 3;
  }
  if (bands != 3 || vips_image_get_format(rgb) != VIPS_FORMAT_UCHAR) {
    g_object_unref(rgb);
    set_ocr_error("image is not 3-band uchar RGB after conversion");
    return std::nullopt;
  }
  const int w = vips_image_get_width(rgb);
  const int h = vips_image_get_height(rgb);
  if (w < 1 || h < 1) {
    g_object_unref(rgb);
    set_ocr_error("empty image dimensions");
    return std::nullopt;
  }
  size_t len = 0;
  void* data = vips_image_write_to_memory(rgb, &len);
  g_object_unref(rgb);
  if (!data || len == 0) {
    if (data) g_free(data);
    set_ocr_error("vips_image_write_to_memory failed");
    return std::nullopt;
  }
  const size_t need = static_cast<size_t>(w) * static_cast<size_t>(h) * 3;
  if (len < need) {
    g_free(data);
    set_ocr_error("RGB buffer shorter than width*height*3");
    return std::nullopt;
  }
  RgbPage out;
  out.width = w;
  out.height = h;
  out.rgb.resize(need);
  std::memcpy(out.rgb.data(), data, need);
  g_free(data);
  out.page_bounds = TextRect{0, 0, static_cast<double>(w), static_cast<double>(h)};
  out.page_1based = 1;
  return out;
}

[[nodiscard]] std::optional<RgbPage> vips_file_to_rgb_page(
    const std::filesystem::path& path, int max_edge) {
  image_library_init();
  VipsImage* thumb = nullptr;
  if (vips_thumbnail(path.string().c_str(), &thumb, max_edge, "size",
                     VIPS_SIZE_DOWN, nullptr) != 0 ||
      !thumb) {
    set_ocr_error(std::string("vips_thumbnail failed for ") + path.string());
    return std::nullopt;
  }
  auto page = vips_image_to_rgb_page(thumb);
  g_object_unref(thumb);
  return page;
}

[[nodiscard]] std::optional<RgbPage> vips_buffer_to_rgb_page(const void* data,
                                                            size_t size,
                                                            int max_edge) {
  if (!data || size == 0) {
    set_ocr_error("empty image buffer");
    return std::nullopt;
  }
  image_library_init();
  VipsImage* thumb = nullptr;
  if (vips_thumbnail_buffer(const_cast<void*>(data), size, &thumb, max_edge,
                            "size", VIPS_SIZE_DOWN, nullptr) != 0 ||
      !thumb) {
    set_ocr_error("vips_thumbnail_buffer failed");
    return std::nullopt;
  }
  auto page = vips_image_to_rgb_page(thumb);
  g_object_unref(thumb);
  return page;
}

[[nodiscard]] std::optional<RgbPage> rasterize_uri_for_ocr(std::string_view uri,
                                                           int max_edge) {
  if (max_edge < 64) max_edge = kDefaultOcrMaxEdge;

  if (auto pdf = parse_pdf_uri(uri)) {
    auto layout = pdf_page_layout_size(pdf->pdf_path, pdf->page, pdf->backend);
    if (!layout || layout->width < 1 || layout->height < 1) return std::nullopt;
    // Page bounds in PDF user space (same as native text layer).
    auto bounds72 = pdf_page_size_72dpi(pdf->pdf_path, pdf->page, pdf->backend);
    TextRect bounds;
    if (bounds72) {
      bounds = TextRect{0, 0, static_cast<double>(bounds72->width),
                        static_cast<double>(bounds72->height)};
    } else {
      // Fallback: treat layout pixels as the page box (Y-down).
      bounds = TextRect{0, 0, static_cast<double>(layout->width),
                        static_cast<double>(layout->height)};
    }
    auto raster =
        pdf_rasterize_page(pdf->pdf_path, pdf->page, max_edge, pdf->backend);
    if (!raster || raster->rgb.empty() || raster->width < 1 ||
        raster->height < 1) {
      return std::nullopt;
    }
    RgbPage out;
    out.width = raster->width;
    out.height = raster->height;
    out.rgb = std::move(raster->rgb);
    out.page_bounds = bounds;
    out.page_1based = pdf->page;
    return out;
  }

#if defined(THUMTOO_HAVE_DJVU)
  if (auto dj = parse_djvu_uri(uri)) {
    if (is_likely_djvu_path(dj->djvu_path)) {
      auto layout = djvu_page_layout_size(dj->djvu_path, dj->page);
      if (!layout || layout->width < 1 || layout->height < 1) return std::nullopt;
      auto raster = djvu_rasterize_page(dj->djvu_path, dj->page, max_edge);
      if (!raster || raster->rgb.empty()) return std::nullopt;
      RgbPage out;
      out.width = raster->width;
      out.height = raster->height;
      out.rgb = std::move(raster->rgb);
      out.page_bounds =
          TextRect{0, 0, static_cast<double>(layout->width),
                   static_cast<double>(layout->height)};
      out.page_1based = dj->page;
      return out;
    }
  }
#endif

  // Archive member image: file://…//archive:member
  if (auto arch = parse_archive_uri(uri)) {
    if (!arch->member_path.empty()) {
      auto bytes = extract_archive_member(arch->archive_path, arch->member_path);
      if (!bytes || bytes->empty()) {
        set_ocr_error("archive member extract failed");
        return std::nullopt;
      }
      return vips_buffer_to_rgb_page(bytes->data(), bytes->size(), max_edge);
    }
  }

  // Plain image files (non multipage containers).
  if (auto path = path_from_file_uri(uri)) {
    if (is_pdf_path(*path) || is_djvu_path(*path) || is_epub_path(*path)) {
      set_ocr_error("path is a multipage document without a page pipe");
      return std::nullopt;
    }
    if (is_likely_archive_path(*path)) {
      set_ocr_error("path is an archive without //archive:member");
      return std::nullopt;
    }
    return vips_file_to_rgb_page(*path, max_edge);
  }

  set_ocr_error("unsupported URI for OCR rasterize");
  return std::nullopt;
}

/// Label page numbers / running headers/footers from geometry + text shape.
void annotate_region_kinds(PageTextLayer& layer) {
  const double ph = layer.page_bounds.height();
  const double pw = layer.page_bounds.width();
  if (ph < 1.0 || pw < 1.0 || layer.regions.empty()) return;
  const double top_band = layer.page_bounds.y0 + ph * 0.08;
  const double bot_band = layer.page_bounds.y1 - ph * 0.08;
  const double cx = layer.page_bounds.x0 + pw * 0.5;

  auto is_page_number_text = [](const std::string& s) {
    std::string t;
    t.reserve(s.size());
    for (char c : s) {
      if (c != ' ' && c != '\t') t.push_back(c);
    }
    if (t.empty() || t.size() > 12) return false;
    int digits = 0;
    int roman = 0;
    for (unsigned char c : t) {
      if (c >= '0' && c <= '9') ++digits;
      else if (c == 'i' || c == 'I' || c == 'v' || c == 'V' || c == 'x' || c == 'X'
               || c == 'l' || c == 'L' || c == 'c' || c == 'C') {
        ++roman;
      } else if (c == '-' || c == '.' || c == '/') {
        continue;
      } else {
        return false;
      }
    }
    return digits > 0 || roman > 0;
  };

  for (auto& r : layer.regions) {
    if (r.role != TextRegionRole::Text) continue;
    r.kind = TextRegionKind::Body;
    const double mid_y = 0.5 * (r.bbox.y0 + r.bbox.y1);
    const double mid_x = 0.5 * (r.bbox.x0 + r.bbox.x1);
    const bool in_top = mid_y <= top_band;
    const bool in_bot = mid_y >= bot_band;
    if (!in_top && !in_bot) continue;
    if (is_page_number_text(r.text)) {
      // Prefer outer/centered short tokens as page numbers.
      const double edge = std::min(std::fabs(mid_x - layer.page_bounds.x0),
                                   std::fabs(layer.page_bounds.x1 - mid_x));
      const bool outer = edge < pw * 0.2;
      const bool centered = std::fabs(mid_x - cx) < pw * 0.15;
      if (outer || centered) {
        r.kind = TextRegionKind::PageNumber;
        continue;
      }
    }
    // Non-numeric band text → header/footer candidate.
    if (in_top) r.kind = TextRegionKind::Header;
    else if (in_bot) r.kind = TextRegionKind::Footer;
  }
}


[[nodiscard]] std::string tesseract_version_string() {
  const char* v = tesseract::TessBaseAPI::Version();
  return v ? std::string(v) : std::string{};
}

std::mutex g_tess_mu;

[[nodiscard]] std::optional<PageTextLayer> run_tesseract(
    const RgbPage& page, const OcrOptions& opts) {
  std::lock_guard<std::mutex> lock(g_tess_mu);
  tesseract::TessBaseAPI api;
  const std::string lang = opts.lang.empty() ? "eng" : opts.lang;
  if (api.Init(nullptr, lang.c_str()) != 0) {
    set_ocr_error(std::string("Tesseract Init failed for lang=") + lang +
                  " (missing tessdata / TESSDATA_PREFIX?)");
    return std::nullopt;
  }
  api.SetPageSegMode(tesseract::PSM_AUTO);
  api.SetImage(page.rgb.data(), page.width, page.height, 3, page.width * 3);
  if (api.Recognize(nullptr) != 0) {
    api.End();
    set_ocr_error("Tesseract Recognize failed");
    return std::nullopt;
  }

  PageTextLayer layer;
  layer.page_1based = page.page_1based;
  layer.page_bounds = page.page_bounds;
  layer.source = TextLayerSource::Ocr;

  OcrMeta meta;
  meta.engine = opts.engine.empty() ? "tesseract" : opts.engine;
  meta.engine_version = tesseract_version_string();
  meta.model = opts.model.empty() ? "default" : opts.model;
  meta.lang = lang;
  // Approximate DPI from raster vs page box width (points → 72 dpi).
  if (page.page_bounds.width() > 1.0) {
    meta.dpi = static_cast<int>(std::lround(
        72.0 * static_cast<double>(page.width) / page.page_bounds.width()));
  }
  meta.created_unix = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  meta.params.emplace_back("psm", "auto");
  layer.ocr = meta;

  const double pw = page.page_bounds.width();
  const double ph = page.page_bounds.height();
  const double ox = page.page_bounds.x0;
  const double oy = page.page_bounds.y0;
  const double sx = pw / static_cast<double>(page.width);
  const double sy = ph / static_cast<double>(page.height);

  tesseract::ResultIterator* ri = api.GetIterator();
  if (ri != nullptr) {
    int block_id = 0;
    // Line-level regions (good for Find / select).
    do {
      const char* utf8 = ri->GetUTF8Text(tesseract::RIL_TEXTLINE);
      if (!utf8) continue;
      std::string text(utf8);
      delete[] utf8;
      while (!text.empty() &&
             (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
      }
      if (text.empty()) continue;

      int left = 0, top = 0, right = 0, bottom = 0;
      if (!ri->BoundingBox(tesseract::RIL_TEXTLINE, &left, &top, &right,
                           &bottom)) {
        continue;
      }
      if (right <= left || bottom <= top) continue;

      TextRegion reg;
      reg.role = TextRegionRole::Text;
      reg.text = std::move(text);
      reg.bbox.x0 = ox + static_cast<double>(left) * sx;
      reg.bbox.y0 = oy + static_cast<double>(top) * sy;
      reg.bbox.x1 = ox + static_cast<double>(right) * sx;
      reg.bbox.y1 = oy + static_cast<double>(bottom) * sy;
      if (ri->IsAtBeginningOf(tesseract::RIL_BLOCK)) {
        ++block_id;
      }
      reg.block_id = block_id;
      layer.regions.push_back(std::move(reg));
    } while (ri->Next(tesseract::RIL_TEXTLINE));
  }

  annotate_region_kinds(layer);
  api.End();
  return layer;
}

#endif  // THUMTOO_HAVE_TESSERACT

}  // namespace

bool ocr_available() {
#if defined(THUMTOO_HAVE_TESSERACT)
  return true;
#else
  return false;
#endif
}

std::string_view ocr_last_error() {
  return g_ocr_last_error;
}

std::optional<PageTextLayer> ocr_page_text_layer(std::string_view uri,
                                                 const OcrOptions& opts) {
#if !defined(THUMTOO_HAVE_TESSERACT)
  (void)uri;
  (void)opts;
  set_ocr_error("Tesseract not compiled into thumtoo");
  return std::nullopt;
#else
  clear_ocr_error();
  if (uri.empty()) {
    set_ocr_error("empty URI");
    return std::nullopt;
  }
  const int max_edge =
      opts.max_edge > 0 ? opts.max_edge : kDefaultOcrMaxEdge;
  auto page = rasterize_uri_for_ocr(uri, max_edge);
  if (!page) {
    if (g_ocr_last_error.empty()) set_ocr_error("rasterize failed");
    return std::nullopt;
  }
  auto layer = run_tesseract(*page, opts);
  if (!layer) {
    if (g_ocr_last_error.empty()) set_ocr_error("Tesseract failed");
    return std::nullopt;
  }
  clear_ocr_error();
  // layout_key filled by Client when storing (native base + ocr suffix).
  return layer;
#endif
}

}  // namespace thumtoo
