// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
  /// MuPDF structured-text block index (0-based) when known; -1 otherwise.
  int block_id = -1;
};

/// Provenance of a page text layer (native extract vs OCR backend).
enum class TextLayerSource : std::uint8_t {
  Native = 0,
  Ocr = 1,
};

/// Backend metadata when source == Ocr. Engine-agnostic so LLM OCR can reuse.
struct OcrMeta {
  std::string engine;
  std::string engine_version;
  std::string model;
  std::string lang;
  int dpi = 0;
  std::int64_t created_unix = 0;
  std::vector<std::pair<std::string, std::string>> params;
};

struct PageTextLayer {
  int page_1based = 0;
  std::string layout_key;
  TextRect page_bounds;
  std::vector<TextRegion> regions;
  TextLayerSource source = TextLayerSource::Native;
  std::optional<OcrMeta> ocr;
};

struct OutlineItem {
  int level = 1;
  std::string title;
  int page_1based = 0;
  std::string uri;
};

struct DocumentOutline {
  std::vector<OutlineItem> items;
};

[[nodiscard]] std::string ocr_store_layout_key(std::string_view base_layout_key,
                                               std::string_view engine,
                                               std::string_view model);

[[nodiscard]] std::vector<std::uint8_t> serialize_page_text_layer(
    const PageTextLayer& layer);

[[nodiscard]] std::optional<PageTextLayer> deserialize_page_text_layer(
    const std::vector<std::uint8_t>& bytes);

[[nodiscard]] std::vector<std::uint8_t> serialize_document_outline(
    const DocumentOutline& outline);

[[nodiscard]] std::optional<DocumentOutline> deserialize_document_outline(
    const std::vector<std::uint8_t>& bytes);

[[nodiscard]] std::optional<PageTextLayer> extract_page_text_layer(
    std::string_view uri);

[[nodiscard]] std::optional<DocumentOutline> extract_document_outline(
    std::string_view uri);

}  // namespace thumtoo
