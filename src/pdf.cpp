// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/pdf.hpp"
#include "thumtoo/pdf_mupdf.hpp"
#include "thumtoo/constants.hpp"
#include "thumtoo/format.hpp"
#include "thumtoo/uri.hpp"
#include "thumtoo/image.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace thumtoo {
namespace {

constexpr std::string_view kPagePipe = "//page:";
/// Legacy alias from the removed Poppler backend; still parsed so old session
/// paths open. All PDF work goes through MuPDF.
constexpr std::string_view kPopplerPagePipe = "//poppler-page:";
constexpr std::string_view kMupdfPagePipe = "//mupdf-page:";

}  // namespace

bool is_likely_pdf_path(const std::filesystem::path& path) {
  return is_pdf_path(path);
}

PdfBackend pdf_resolve_backend(PdfBackend requested) {
  (void)requested;
#if defined(THUMTOO_HAVE_MUPDF)
  // Default, Poppler (legacy), and MuPDF all resolve to MuPDF.
  return PdfBackend::MuPDF;
#else
  return PdfBackend::Default;
#endif
}

bool pdf_backend_available(PdfBackend backend) {
  (void)backend;
#if defined(THUMTOO_HAVE_MUPDF)
  return true;
#else
  return false;
#endif
}

const char* pdf_backend_name(PdfBackend backend) {
  (void)backend;
#if defined(THUMTOO_HAVE_MUPDF)
  return "mupdf";
#else
  return "none";
#endif
}

std::string pdf_image_uri(const std::filesystem::path& pdf_path, int image_1based) {
  std::string uri = file_uri_from_path(pdf_path.lexically_normal());
  uri += "//pdfimage:";
  uri += std::to_string(std::max(1, image_1based));
  return uri;
}

std::optional<ParsedPdfImageUri> parse_pdf_image_uri(std::string_view uri) {
  constexpr std::string_view kPipe = "//pdfimage:";
  auto pos = uri.find(kPipe);
  if (pos == std::string_view::npos) return std::nullopt;
  const auto outer = uri.substr(0, pos);
  auto path = path_from_file_uri(outer);
  if (!path) {
    if (outer.find("://") != std::string_view::npos) return std::nullopt;
    path = std::filesystem::path(std::string(outer));
  }
  if (!is_pdf_path(*path) && !is_likely_pdf_path(*path)) return std::nullopt;
  std::string_view rest = uri.substr(pos + kPipe.size());
  if (rest.empty()) return std::nullopt;
  int image = 0;
  for (char c : rest) {
    if (c < '0' || c > '9') break;
    image = image * 10 + (c - '0');
    if (image > 1'000'000) return std::nullopt;
  }
  if (image < 1) return std::nullopt;
  ParsedPdfImageUri out;
  out.pdf_path = *path;
  out.image = image;
  return out;
}

std::optional<int> pdf_embedded_image_count(const std::filesystem::path& path) {
#if defined(THUMTOO_HAVE_MUPDF)
  return mupdf_embedded_image_count(path);
#else
  (void)path;
  return std::nullopt;
#endif
}

std::optional<PdfRaster> pdf_rasterize_embedded_image(const std::filesystem::path& path,
                                                      int image_1based, int max_edge) {
#if defined(THUMTOO_HAVE_MUPDF)
  return mupdf_rasterize_embedded_image(path, image_1based, max_edge);
#else
  (void)path;
  (void)image_1based;
  (void)max_edge;
  return std::nullopt;
#endif
}

std::optional<Size> pdf_embedded_image_size(const std::filesystem::path& path,
                                            int image_1based) {
#if defined(THUMTOO_HAVE_MUPDF)
  return mupdf_embedded_image_size(path, image_1based);
#else
  (void)path;
  (void)image_1based;
  return std::nullopt;
#endif
}

std::string pdf_page_uri(const std::filesystem::path& pdf_path, int page_1based,
                         PdfBackend backend) {
  auto uri = file_uri_from_path(pdf_path.lexically_normal());
  // Prefer neutral //page:N. Explicit MuPDF only when requested.
  // PdfBackend::Poppler is treated as Default (legacy).
  if (backend == PdfBackend::MuPDF) {
    uri += "//mupdf-page:";
  } else {
    uri += "//page:";
  }
  uri += std::to_string(std::max(1, page_1based));
  return uri;
}

std::optional<ParsedPdfUri> parse_pdf_uri(std::string_view uri) {
  PdfBackend backend = PdfBackend::Default;
  std::string_view tag = kPagePipe;
  auto pipe = uri.find(kPagePipe);
  auto pop = uri.find(kPopplerPagePipe);
  auto mu = uri.find(kMupdfPagePipe);

  std::size_t pos = std::string_view::npos;
  auto consider = [&](std::size_t p, PdfBackend b, std::string_view t) {
    if (p == std::string_view::npos) return;
    if (pos == std::string_view::npos || p < pos) {
      pos = p;
      backend = b;
      tag = t;
    }
  };
  consider(pipe, PdfBackend::Default, kPagePipe);
  // Legacy //poppler-page: still parses; pdf_resolve_backend maps to MuPDF.
  consider(pop, PdfBackend::Poppler, kPopplerPagePipe);
  consider(mu, PdfBackend::MuPDF, kMupdfPagePipe);
  if (pos == std::string_view::npos) return std::nullopt;

  const auto outer = uri.substr(0, pos);
  auto path = path_from_file_uri(outer);
  if (!path) {
    if (!outer.empty() && outer.front() == '/') {
      path = std::filesystem::path(std::string(outer));
    }
  }
  if (!path) return std::nullopt;
  if (!is_pdf_path(*path)) return std::nullopt;

  std::string_view rest = uri.substr(pos + tag.size());
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
  out.backend = backend;
  return out;
}

std::optional<int> pdf_page_count(const std::filesystem::path& path,
                                  PdfBackend backend) {
  (void)backend;
#if defined(THUMTOO_HAVE_MUPDF)
  return mupdf_page_count(path);
#else
  (void)path;
  return std::nullopt;
#endif
}

std::optional<Size> pdf_page_size_72dpi(const std::filesystem::path& path,
                                        int page_1based, PdfBackend backend) {
  (void)backend;
#if defined(THUMTOO_HAVE_MUPDF)
  return mupdf_page_size_72dpi(path, page_1based);
#else
  (void)path;
  (void)page_1based;
  return std::nullopt;
#endif
}

std::optional<Size> pdf_page_layout_size(const std::filesystem::path& path,
                                         int page_1based, PdfBackend backend) {
  auto s72 = pdf_page_size_72dpi(path, page_1based, backend);
  if (!s72) return std::nullopt;
  const double scale = static_cast<double>(kPdfLayoutDpi) / 72.0;
  const int w = std::max(1, static_cast<int>(std::lround(s72->width * scale)));
  const int h = std::max(1, static_cast<int>(std::lround(s72->height * scale)));
  return Size{w, h};
}

Size pdf_page_size_at_scale(Size layout, int scale) {
  if (layout.width <= 0 || layout.height <= 0) return Size{0, 0};
  if (scale == 0) return layout;
  const double factor = std::ldexp(1.0, -scale);
  const int w = std::max(1, static_cast<int>(std::lround(layout.width * factor)));
  const int h = std::max(1, static_cast<int>(std::lround(layout.height * factor)));
  return Size{w, h};
}

double pdf_dpi_for_scale(int scale) {
  return static_cast<double>(kPdfLayoutDpi) * std::ldexp(1.0, -scale);
}

std::optional<PdfRaster> pdf_rasterize_page(const std::filesystem::path& path,
                                            int page_1based, int max_edge,
                                            PdfBackend backend) {
  (void)backend;
#if defined(THUMTOO_HAVE_MUPDF)
  return mupdf_rasterize_page(path, page_1based, max_edge);
#else
  (void)path;
  (void)page_1based;
  (void)max_edge;
  return std::nullopt;
#endif
}

PdfPageContentStats pdf_page_content_stats(const std::filesystem::path& path,
                                           int page_1based,
                                           PdfBackend backend) {
  (void)backend;
#if defined(THUMTOO_HAVE_MUPDF)
  return mupdf_page_content_stats(path, page_1based);
#else
  (void)path;
  (void)page_1based;
  return {};
#endif
}

bool pdf_page_allows_live_tiles(const std::filesystem::path& path,
                                int page_1based, PdfBackend backend) {
  return !pdf_page_content_stats(path, page_1based, backend).image_heavy;
}

std::optional<PdfRaster> pdf_rasterize_page_region(
    const std::filesystem::path& path, int page_1based, double dpi, int px,
    int py, int pw, int ph, PdfBackend backend) {
  (void)backend;
#if defined(THUMTOO_HAVE_MUPDF)
  return mupdf_rasterize_page_region(path, page_1based, dpi, px, py, pw, ph);
#else
  (void)path;
  (void)page_1based;
  (void)dpi;
  (void)px;
  (void)py;
  (void)pw;
  (void)ph;
  return std::nullopt;
#endif
}

std::optional<PdfRaster> pdf_render_tile_cell(const std::filesystem::path& path,
                                               int page_1based, int scale, int x,
                                               int y, PdfBackend backend) {
  (void)backend;
#if defined(THUMTOO_HAVE_MUPDF)
  return mupdf_render_tile_cell(path, page_1based, scale, x, y);
#else
  (void)path;
  (void)page_1based;
  (void)scale;
  (void)x;
  (void)y;
  return std::nullopt;
#endif
}

std::optional<TileBlob> pdf_build_tile_cell(const std::filesystem::path& path,
                                            int page_1based, int scale, int x,
                                            int y, int jpeg_quality,
                                            PdfBackend backend) {
  auto raster = pdf_render_tile_cell(path, page_1based, scale, x, y, backend);
  if (!raster || raster->rgb.empty()) return std::nullopt;
  return encode_tile_cell_rgb(raster->rgb.data(), raster->width, raster->height,
                              scale, x, y, jpeg_quality);
}

std::optional<PageTextLayer> pdf_page_text_layer(const std::filesystem::path& path,
                                                 int page_1based,
                                                 PdfBackend backend) {
  (void)backend;
#if defined(THUMTOO_HAVE_MUPDF)
  return mupdf_page_text_layer(path, page_1based);
#else
  (void)path;
  (void)page_1based;
  return std::nullopt;
#endif
}

std::optional<DocumentOutline> pdf_document_outline(const std::filesystem::path& path,
                                                    PdfBackend backend) {
  (void)backend;
#if defined(THUMTOO_HAVE_MUPDF)
  return mupdf_document_outline(path);
#else
  (void)path;
  return std::nullopt;
#endif
}

}  // namespace thumtoo
