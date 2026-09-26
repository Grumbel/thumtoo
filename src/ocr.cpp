// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/ocr.hpp"

#include "thumtoo/djvu.hpp"
#include "thumtoo/pdf.hpp"
#include "thumtoo/uri.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

#if defined(THUMTOO_HAVE_TESSERACT)
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
  return std::nullopt;
}

#if defined(THUMTOO_HAVE_TESSERACT)

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
    return std::nullopt;
  }
  api.SetPageSegMode(tesseract::PSM_AUTO);
  api.SetImage(page.rgb.data(), page.width, page.height, 3, page.width * 3);
  if (api.Recognize(nullptr) != 0) {
    api.End();
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

std::optional<PageTextLayer> ocr_page_text_layer(std::string_view uri,
                                                 const OcrOptions& opts) {
#if !defined(THUMTOO_HAVE_TESSERACT)
  (void)uri;
  (void)opts;
  return std::nullopt;
#else
  if (uri.empty()) return std::nullopt;
  const int max_edge =
      opts.max_edge > 0 ? opts.max_edge : kDefaultOcrMaxEdge;
  auto page = rasterize_uri_for_ocr(uri, max_edge);
  if (!page) return std::nullopt;
  auto layer = run_tesseract(*page, opts);
  if (!layer) return std::nullopt;
  // layout_key filled by Client when storing (native base + ocr suffix).
  return layer;
#endif
}

}  // namespace thumtoo
