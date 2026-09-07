// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thumtoo/types.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thumtoo {

struct ParsedPdfUri {
  std::filesystem::path pdf_path;
  /// 1-based page index (matches //page:N).
  int page = 0;
};

/// file:///abs/doc.pdf//page:12  (page is 1-based).
[[nodiscard]] std::string pdf_page_uri(const std::filesystem::path& pdf_path,
                                       int page_1based);

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

}  // namespace thumtoo
