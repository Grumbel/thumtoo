// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thumtoo/pdf.hpp"

#include <filesystem>
#include <optional>

namespace thumtoo {

/// MuPDF backend (compiled only usefully when THUMTOO_HAVE_MUPDF).
/// Public pdf_* APIs dispatch here when PdfBackend resolves to MuPDF.

[[nodiscard]] std::optional<int> mupdf_page_count(const std::filesystem::path& path);

[[nodiscard]] std::optional<Size> mupdf_page_size_72dpi(const std::filesystem::path& path,
                                                        int page_1based);

[[nodiscard]] PdfPageContentStats mupdf_page_content_stats(
    const std::filesystem::path& path, int page_1based);

[[nodiscard]] std::optional<PdfRaster> mupdf_rasterize_page(
    const std::filesystem::path& path, int page_1based, int max_edge);

[[nodiscard]] std::optional<PdfRaster> mupdf_rasterize_page_region(
    const std::filesystem::path& path, int page_1based, double dpi, int px,
    int py, int pw, int ph);

[[nodiscard]] std::optional<PdfRaster> mupdf_render_tile_cell(
    const std::filesystem::path& path, int page_1based, int scale, int x,
    int y);

}  // namespace thumtoo
