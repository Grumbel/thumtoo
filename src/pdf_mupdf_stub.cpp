// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Linked only when THUMTOO_HAVE_MUPDF is unset (see CMakeLists.txt).

#include "thumtoo/pdf_mupdf.hpp"

namespace thumtoo {

void mupdf_force_next_open_as_text(const std::filesystem::path& path) {
  (void)path;
}

std::string mupdf_last_error() { return {}; }

void mupdf_clear_last_error() {}

std::optional<int> mupdf_page_count(const std::filesystem::path& path) {
  (void)path;
  return std::nullopt;
}

std::optional<Size> mupdf_page_size_72dpi(const std::filesystem::path& path,
                                          int page_1based) {
  (void)path;
  (void)page_1based;
  return std::nullopt;
}

std::optional<Size> mupdf_page_layout_size(const std::filesystem::path& path,
                                            int page_1based) {
  (void)path;
  (void)page_1based;
  return std::nullopt;
}

PdfPageContentStats mupdf_page_content_stats(const std::filesystem::path& path,
                                             int page_1based) {
  (void)path;
  (void)page_1based;
  return {};
}

std::optional<PdfRaster> mupdf_rasterize_page(const std::filesystem::path& path,
                                               int page_1based, int max_edge) {
  (void)path;
  (void)page_1based;
  (void)max_edge;
  return std::nullopt;
}

std::optional<PdfRaster> mupdf_rasterize_page_region(
    const std::filesystem::path& path, int page_1based, double dpi, int px,
    int py, int pw, int ph) {
  (void)path;
  (void)page_1based;
  (void)dpi;
  (void)px;
  (void)py;
  (void)pw;
  (void)ph;
  return std::nullopt;
}

std::optional<PdfRaster> mupdf_render_tile_cell(const std::filesystem::path& path,
                                                 int page_1based, int scale,
                                                 int x, int y) {
  (void)path;
  (void)page_1based;
  (void)scale;
  (void)x;
  (void)y;
  return std::nullopt;
}

std::optional<int> mupdf_embedded_image_count(const std::filesystem::path& path) {
  (void)path;
  return std::nullopt;
}

std::optional<PdfRaster> mupdf_rasterize_embedded_image(
    const std::filesystem::path& path, int image_1based, int max_edge) {
  (void)path;
  (void)image_1based;
  (void)max_edge;
  return std::nullopt;
}

std::optional<Size> mupdf_embedded_image_size(const std::filesystem::path& path,
                                               int image_1based) {
  (void)path;
  (void)image_1based;
  return std::nullopt;
}

std::optional<PageTextLayer> mupdf_page_text_layer(
    const std::filesystem::path& path, int page_1based) {
  (void)path;
  (void)page_1based;
  return std::nullopt;
}

std::optional<DocumentOutline> mupdf_document_outline(
    const std::filesystem::path& path) {
  (void)path;
  return std::nullopt;
}

std::optional<PdfRaster> mupdf_page_thumb_rgb(const std::filesystem::path& path,
                                              int page_1based) {
  (void)path;
  (void)page_1based;
  return std::nullopt;
}

}  // namespace thumtoo
