// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/pdf.hpp"
#include "thumtoo/constants.hpp"
#include "thumtoo/format.hpp"
#include "thumtoo/uri.hpp"

#include <algorithm>
#include <memory>
#include <cmath>
#include <string>

#if defined(THUMTOO_HAVE_POPPLER)
#include <poppler-document.h>
#include <poppler-page.h>
#include <poppler-page-renderer.h>
#endif

namespace thumtoo {
namespace {

constexpr std::string_view kPagePipe = "//page:";

}  // namespace

bool is_likely_pdf_path(const std::filesystem::path& path) {
  return is_pdf_path(path);
}

std::string pdf_page_uri(const std::filesystem::path& pdf_path, int page_1based) {
  auto uri = file_uri_from_path(pdf_path.lexically_normal());
  uri += "//page:";
  uri += std::to_string(std::max(1, page_1based));
  return uri;
}

std::optional<ParsedPdfUri> parse_pdf_uri(std::string_view uri) {
  const auto pipe = uri.find(kPagePipe);
  if (pipe == std::string_view::npos) return std::nullopt;

  const auto outer = uri.substr(0, pipe);
  auto path = path_from_file_uri(outer);
  if (!path) return std::nullopt;

  std::string_view rest = uri.substr(pipe + kPagePipe.size());
  if (rest.empty()) return std::nullopt;
  int page = 0;
  for (char c : rest) {
    if (c < '0' || c > '9') return std::nullopt;
    page = page * 10 + (c - '0');
    if (page > 1'000'000) return std::nullopt;
  }
  if (page < 1) return std::nullopt;

  ParsedPdfUri out;
  out.pdf_path = *path;
  out.page = page;
  return out;
}

std::optional<int> pdf_page_count(const std::filesystem::path& path) {
#if !defined(THUMTOO_HAVE_POPPLER)
  (void)path;
  return std::nullopt;
#else
  std::unique_ptr<poppler::document> doc(
      poppler::document::load_from_file(path.string()));
  if (!doc || doc->is_locked()) return std::nullopt;
  const int n = doc->pages();
  if (n <= 0) return std::nullopt;
  return n;
#endif
}

std::optional<Size> pdf_page_size_72dpi(const std::filesystem::path& path,
                                        int page_1based) {
#if !defined(THUMTOO_HAVE_POPPLER)
  (void)path;
  (void)page_1based;
  return std::nullopt;
#else
  if (page_1based < 1) return std::nullopt;
  std::unique_ptr<poppler::document> doc(
      poppler::document::load_from_file(path.string()));
  if (!doc || doc->is_locked()) return std::nullopt;
  if (page_1based > doc->pages()) return std::nullopt;
  std::unique_ptr<poppler::page> page(doc->create_page(page_1based - 1));
  if (!page) return std::nullopt;
  const poppler::rectf box = page->page_rect(poppler::media_box);
  const int w = std::max(1, static_cast<int>(std::lround(box.width())));
  const int h = std::max(1, static_cast<int>(std::lround(box.height())));
  return Size{w, h};
#endif
}

std::optional<Size> pdf_page_layout_size(const std::filesystem::path& path,
                                         int page_1based) {
  auto s72 = pdf_page_size_72dpi(path, page_1based);
  if (!s72) return std::nullopt;
  // Scale media-box points to layout DPI (default 144 = 2× 72).
  const double scale = static_cast<double>(kPdfLayoutDpi) / 72.0;
  const int w = std::max(1, static_cast<int>(std::lround(s72->width * scale)));
  const int h = std::max(1, static_cast<int>(std::lround(s72->height * scale)));
  return Size{w, h};
}

std::optional<PdfRaster> pdf_rasterize_page(const std::filesystem::path& path,
                                            int page_1based, int max_edge) {
#if !defined(THUMTOO_HAVE_POPPLER)
  (void)path;
  (void)page_1based;
  (void)max_edge;
  return std::nullopt;
#else
  if (page_1based < 1) return std::nullopt;
  std::unique_ptr<poppler::document> doc(
      poppler::document::load_from_file(path.string()));
  if (!doc || doc->is_locked()) return std::nullopt;
  if (page_1based > doc->pages()) return std::nullopt;
  std::unique_ptr<poppler::page> page(doc->create_page(page_1based - 1));
  if (!page) return std::nullopt;

  const poppler::rectf box = page->page_rect(poppler::media_box);
  const double bw = std::max(1.0, box.width());
  const double bh = std::max(1.0, box.height());
  const double long_pt = std::max(bw, bh);

  // 72 dpi → 1 pixel per point. Scale up so long edge ≈ max_edge when asked.
  double dpi = 72.0;
  if (max_edge > 0 && long_pt > 0.0) {
    dpi = 72.0 * (static_cast<double>(max_edge) / long_pt);
    dpi = std::clamp(dpi, 36.0, 600.0);
  }

  poppler::page_renderer renderer;
  renderer.set_render_hint(poppler::page_renderer::antialiasing, true);
  renderer.set_render_hint(poppler::page_renderer::text_antialiasing, true);
  poppler::image img = renderer.render_page(page.get(), dpi, dpi);
  if (!img.is_valid()) return std::nullopt;

  const int w = img.width();
  const int h = img.height();
  if (w <= 0 || h <= 0) return std::nullopt;

  PdfRaster out;
  out.width = w;
  out.height = h;
  out.rgb.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3u);

  // poppler::image is typically format_rgb24 or format_argb32.
  const auto fmt = img.format();
  const char* src = img.const_data();
  const int bpl = img.bytes_per_row();
  for (int y = 0; y < h; ++y) {
    const auto* row = reinterpret_cast<const unsigned char*>(src + y * bpl);
    auto* dst = out.rgb.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 3u;
    if (fmt == poppler::image::format_rgb24) {
      for (int x = 0; x < w; ++x) {
        dst[x * 3 + 0] = row[x * 3 + 0];
        dst[x * 3 + 1] = row[x * 3 + 1];
        dst[x * 3 + 2] = row[x * 3 + 2];
      }
    } else if (fmt == poppler::image::format_argb32) {
      for (int x = 0; x < w; ++x) {
        // Byte order: typically BGRA or ARGB depending on platform; poppler
        // documents ARGB32 as 0xAARRGGBB in native endian for data().
        const unsigned char* p = row + x * 4;
        // poppler ARGB32 is native-endian 0xAARRGGBB; on little-endian memory
        // that is B,G,R,A.
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        dst[x * 3 + 0] = p[1];
        dst[x * 3 + 1] = p[2];
        dst[x * 3 + 2] = p[3];
#else
        dst[x * 3 + 0] = p[2];
        dst[x * 3 + 1] = p[1];
        dst[x * 3 + 2] = p[0];
#endif
      }
    } else if (fmt == poppler::image::format_mono) {
      for (int x = 0; x < w; ++x) {
        const unsigned char bit = (row[x / 8] >> (7 - (x % 8))) & 1;
        const unsigned char v = bit ? 0 : 255;
        dst[x * 3 + 0] = v;
        dst[x * 3 + 1] = v;
        dst[x * 3 + 2] = v;
      }
    } else {
      // Fallback: treat as RGB24-ish first three bytes per pixel if possible.
      const int spp = std::max(1, bpl / std::max(1, w));
      for (int x = 0; x < w; ++x) {
        const unsigned char* p = row + x * spp;
        dst[x * 3 + 0] = p[0];
        dst[x * 3 + 1] = spp > 1 ? p[1] : p[0];
        dst[x * 3 + 2] = spp > 2 ? p[2] : p[0];
      }
    }
  }
  return out;
#endif
}

}  // namespace thumtoo
