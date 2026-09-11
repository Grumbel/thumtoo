// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thumtoo/status.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace thumtoo {

struct Size {
  int width = 0;
  int height = 0;
};

struct ContentMeta {
  std::string content_id;
  std::optional<Size> size;
  std::optional<std::int64_t> duration_ms;
  std::optional<int> still_count;
  ContentStatus status = ContentStatus::Pending;
  std::optional<std::string> error_code;
  std::optional<std::string> format;
};

/// How a soft ladder level was produced (stored on levels + PixelLevel).
/// Hosts must not treat Embedded as a completed soft ladder of the requested edge.
enum class PixelSource : int {
  Unknown = 0,      ///< Legacy row / not tagged
  JpegShrink = 1,   ///< vips_thumbnail / shrink-on-decode from full source
  Embedded = 2,     ///< EXIF or other embedded JPEG thumbnail (may be tiny / off-aspect)
  Full = 3,         ///< Full-resolution or near-native extract stored as a level
};

inline constexpr std::string_view to_string(PixelSource s) {
  switch (s) {
    case PixelSource::Unknown:
      return "unknown";
    case PixelSource::JpegShrink:
      return "jpeg_shrink";
    case PixelSource::Embedded:
      return "embedded";
    case PixelSource::Full:
      return "full";
  }
  return "unknown";
}

/// Encoded ladder level from cache (JPEG-XL bytes by default).
struct PixelLevel {
  int max_edge = 0;
  int frame_idx = 0;
  int width = 0;
  int height = 0;
  std::string codec;  // "jxl"
  std::vector<std::uint8_t> bytes;
  PixelSource source = PixelSource::Unknown;
};

/// How the tile pixels were produced (stored in index for cache policy).
enum class TileSource : int {
  Full = 0,         ///< Full-resolution decode (or non-JPEG path)
  JpegShrink = 1,   ///< libjpeg/vips DCT shrink (scale > 0)
  Embedded = 2,     ///< EXIF/embedded thumbnail
  PdfRegion = 3,    ///< Poppler region raster
  DjvuRegion = 4,   ///< ddjvu region raster
};

/// Encoded grid tile (Phase 4 / Galapix-compatible). See TILES.md.
struct TileBlob {
  int scale = 0;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  std::string codec;  // "jpeg" by default
  std::vector<std::uint8_t> bytes;
  TileSource source = TileSource::Full;
};

struct TileCoverage {
  int min_scale = 0;
  int max_scale = 0;
  Size size;
};

}  // namespace thumtoo
