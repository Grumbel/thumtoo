// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thumtoo/text.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace thumtoo {

struct OcrOptions {
  /// Tesseract language codes (e.g. "eng", "eng+deu"). Empty → "eng".
  std::string lang;
  /// Target raster long-edge pixels (clamped). 0 → default (3000).
  int max_edge = 0;
  /// Engine id for provenance / store key. Empty → "tesseract".
  std::string engine;
  /// Model / tessdata name. Empty → "default".
  std::string model;
};

/// True when built with THUMTOO_HAVE_TESSERACT and runtime init succeeds once.
[[nodiscard]] bool ocr_available();

/**
 * Rasterize @p uri (PDF/DjVu page or image) and run OCR → PageTextLayer with
 * source=Ocr. Does not touch Store. Returns nullopt if OCR is unavailable or
 * the locator cannot be rasterized.
 */
[[nodiscard]] std::optional<PageTextLayer> ocr_page_text_layer(
    std::string_view uri, const OcrOptions& opts = {});

}  // namespace thumtoo
