// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace thumtoo {

/// Axis-aligned rectangle in page space (PDF: media-box points, 72 dpi;
/// origin bottom-left in raw PDF user space; consumers may flip Y to match
/// top-left raster orientation — see pdf_page_text_layer docs).
struct TextRect {
  double x0 = 0;
  double y0 = 0;
  double x1 = 0;
  double y1 = 0;

  [[nodiscard]] double width() const { return x1 - x0; }
  [[nodiscard]] double height() const { return y1 - y0; }
  [[nodiscard]] bool empty() const { return width() <= 0 || height() <= 0; }
};

enum class TextRegionRole : std::uint8_t {
  Text = 0,
  Link = 1,
};

enum class TextLinkTargetKind : std::uint8_t {
  None = 0,
  InternalPage = 1,  ///< 1-based page index
  Uri = 2,
};

struct TextLinkTarget {
  TextLinkTargetKind kind = TextLinkTargetKind::None;
  int page_1based = 0;   ///< InternalPage
  double x = 0;          ///< optional dest point (page space)
  double y = 0;
  std::string uri;       ///< Uri
};

/// One selectable / searchable / clickable region on a page.
struct TextRegion {
  TextRect bbox;
  TextRegionRole role = TextRegionRole::Text;
  std::string text;  ///< role=Text: content; role=Link: optional label
  TextLinkTarget target;
};

struct PageTextLayer {
  int page_1based = 0;
  /// Empty for PDF/DjVu. EPUB layout profile hash when geometry is layout-bound.
  std::string layout_key;
  /// Page media box in the same space as region bboxes (points for PDF).
  TextRect page_bounds;
  std::vector<TextRegion> regions;
};

struct OutlineItem {
  int level = 1;  ///< 1 = top-level
  std::string title;
  /// 1-based page when the dest resolves to a page; 0 if unknown / URI-only.
  int page_1based = 0;
  std::string uri;  ///< set when dest is an external/URI action
};

struct DocumentOutline {
  std::vector<OutlineItem> items;
};

}  // namespace thumtoo

/// Serialize page text layer to a compact durable payload (always-cache).
[[nodiscard]] std::vector<std::uint8_t> serialize_page_text_layer(
    const PageTextLayer& layer);

[[nodiscard]] std::optional<PageTextLayer> deserialize_page_text_layer(
    const std::vector<std::uint8_t>& bytes);

[[nodiscard]] std::vector<std::uint8_t> serialize_document_outline(
    const DocumentOutline& outline);

[[nodiscard]] std::optional<DocumentOutline> deserialize_document_outline(
    const std::vector<std::uint8_t>& bytes);

/**
 * Extract text layer for a location URI (//page: / //epub: / DjVu //page:).
 * Does not touch the cache — pure source extract.
 */
[[nodiscard]] std::optional<PageTextLayer> extract_page_text_layer(
    std::string_view uri);

[[nodiscard]] std::optional<DocumentOutline> extract_document_outline(
    std::string_view uri);
