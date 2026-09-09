// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thumtoo/constants.hpp"
#include "thumtoo/types.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thumtoo {

/// Which PDF engine handles a page URI.
enum class PdfBackend {
  Default,  ///< Resolves to MuPDF when built with it, else Poppler
  Poppler,
  MuPDF,
};

/// Effective backend after applying Default + build flags.
[[nodiscard]] PdfBackend pdf_resolve_backend(PdfBackend requested);

[[nodiscard]] bool pdf_backend_available(PdfBackend backend);

[[nodiscard]] const char* pdf_backend_name(PdfBackend backend);

struct ParsedPdfUri {
  std::filesystem::path pdf_path;
  /// 1-based page index.
  int page = 0;
  PdfBackend backend = PdfBackend::Default;
};

/// file:///abs/doc.pdf//page:12  (default backend).
[[nodiscard]] std::string pdf_page_uri(const std::filesystem::path& pdf_path,
                                       int page_1based,
                                       PdfBackend backend = PdfBackend::Default);

[[nodiscard]] std::optional<ParsedPdfUri> parse_pdf_uri(std::string_view uri);

[[nodiscard]] bool is_likely_pdf_path(const std::filesystem::path& path);

/// Page count, or nullopt if the file cannot be opened / is not a PDF.
[[nodiscard]] std::optional<int> pdf_page_count(const std::filesystem::path& path);

struct PdfRaster {
  int width = 0;
  int height = 0;
  /// Contiguous RGB888 rows (no padding).
  std::vector<std::uint8_t> rgb;
};

/// Page content mix — used to gate live (negative-scale) tiles.
struct PdfPageContentStats {
  int image_count = 0;
  /// Union of image mapping boxes / media-box area in [0,1]. -1 if unknown.
  double image_coverage = -1.0;
  int text_chars = 0;
  /// True when live deep-zoom region tiles are a bad idea (scanned / photo pages).
  bool image_heavy = false;
};

[[nodiscard]] PdfPageContentStats pdf_page_content_stats(
    const std::filesystem::path& path, int page_1based);

/// Live tiles finer than layout (scale < 0) only when the page is not image-heavy.
[[nodiscard]] bool pdf_page_allows_live_tiles(const std::filesystem::path& path,
                                              int page_1based);

/**
 * Rasterize one page so the long edge is about max_edge pixels (at least the
 * natural 72 dpi size when max_edge is large). Page is 1-based.
 */
[[nodiscard]] std::optional<PdfRaster> pdf_rasterize_page(
    const std::filesystem::path& path, int page_1based, int max_edge);

/// Intrinsic size at 72 dpi (media box), page 1-based.
[[nodiscard]] std::optional<Size> pdf_page_size_72dpi(
    const std::filesystem::path& path, int page_1based);

/// Layout size for Galapix-style tiles: media box scaled to kPdfLayoutDpi.
[[nodiscard]] std::optional<Size> pdf_page_layout_size(
    const std::filesystem::path& path, int page_1based);

/**
 * Effective page pixel size at tile scale s relative to layout (kPdfLayoutDpi).
 * s=0 → layout size; s>0 → coarser (÷2 each step); s<0 → denser (×2 each step).
 */
[[nodiscard]] Size pdf_page_size_at_scale(Size layout, int scale);

/**
 * DPI for tile scale s: kPdfLayoutDpi * 2^{-s}.
 * Unclamped — region render only ever materializes ≤kTileSize² pixels.
 */
[[nodiscard]] double pdf_dpi_for_scale(int scale);

/**
 * Rasterize a pixel rectangle of the page at the given DPI.
 * (px,py,pw,ph) are in the full-page pixel grid at that DPI (Poppler crop).
 * Page is 1-based. Does not allocate a full-page buffer beyond the crop.
 */
[[nodiscard]] std::optional<PdfRaster> pdf_rasterize_page_region(
    const std::filesystem::path& path, int page_1based, double dpi, int px,
    int py, int pw, int ph);

/**
 * Rasterize one tile cell to RGB888 (no encode). Live path for Galapix.
 */
[[nodiscard]] std::optional<PdfRaster> pdf_render_tile_cell(
    const std::filesystem::path& path, int page_1based, int scale, int x,
    int y);

/**
 * Rasterize cell then JPEG-encode (durable cache only). Prefer
 * pdf_render_tile_cell + reply rgb888 for interactive live tiles.
 */
[[nodiscard]] std::optional<TileBlob> pdf_build_tile_cell(
    const std::filesystem::path& path, int page_1based, int scale, int x,
    int y, int jpeg_quality = kDefaultTileQuality);

}  // namespace thumtoo
