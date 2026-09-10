// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace thumtoo {

/// schema_meta / schema_version = 1 (DESIGN §6b). Bump only with incompatible layout.
inline constexpr int kSchemaVersion = 3;

/// Fixed long-edge ladder (pixels).
inline constexpr std::array<int, 5> kLadderEdges = {128, 256, 512, 1024, 2048};

/// Ladder codec is JPEG-XL via libvips (required dependency; see flake.nix).
inline constexpr int kDefaultJxlQuality = 80;
inline constexpr char kDefaultCodec[] = "jxl";

/// Video temporal stills (plus poster as frame_idx 0).
inline constexpr int kDefaultVideoStillCount = 16;

inline constexpr std::string_view kContentIdSha256Prefix = "sha256:";
inline constexpr std::string_view kContentIdProvisionalPrefix = "prov:";

/// Archive security defaults (DESIGN §6b).
inline constexpr std::uint64_t kArchiveMaxMemberUncompressedBytes =
    512ull * 1024ull * 1024ull;
inline constexpr int kArchiveMaxCompressionRatio = 100;
inline constexpr std::uint64_t kArchiveMaxPrepareTotalUncompressedBytes =
    2ull * 1024ull * 1024ull * 1024ull;

inline constexpr char kSchemaMetaVersionKey[] = "schema_version";
inline constexpr char kSchemaMetaLadderEdgesKey[] = "ladder_edges";
inline constexpr char kSchemaMetaJxlQualityKey[] = "jxl_quality";
// Legacy name still accepted when reading old caches.
inline constexpr char kSchemaMetaWebpQualityKey[] = "webp_quality";

/// PDF page layout / live tile rasterization: media box scaled to this DPI
/// (144 = 2× 72). Scale 0 matches get_size; negative scales are denser
/// (dpi = kPdfLayoutDpi * 2^{-scale}). Do not change without a cache migration.
inline constexpr int kPdfLayoutDpi = 144;

/// Finest scale written to the durable tile DB for PDF pages. Finer scales
/// (e.g. -3, -4) may still be generated live and returned to the client, but
/// are not stored — avoids filling the cache with 1k–9k dpi cells.
inline constexpr int kPdfMinDurableTileScale = -2;  // 144 * 4 = 576 dpi

/// Image-heavy pages: no live tiles finer than this (0 = layout dpi only).
inline constexpr int kPdfMinLiveTileScaleImageHeavy = 0;

/// Fraction of page area covered by image XObjects → treat as image-heavy.
inline constexpr double kPdfImageHeavyCoverage = 0.45;

/// Sparse text (chars per square point of media box) → scanned heuristic
/// when image-mapping is unavailable.
inline constexpr double kPdfSparseTextPerPoint2 = 0.002;

/// EPUB default virtual page size in **pixels** at kEpubLayoutDpi and base font
/// size in points for fz_layout_document. Changing these invalidates default
/// expand URIs and cached tiles for that profile.
/// (Legacy used points; URI now carries pixels so image-viewer math stays simple.)
/// ~6.25×9.4 in at 144 dpi — closer to a trade page than a large tablet sheet.
inline constexpr int kEpubDefaultPageWidthPx = 900;
inline constexpr int kEpubDefaultPageHeightPx = 1350;
/// Body text size; also forced via user CSS so document styles cannot ignore it.
inline constexpr int kEpubDefaultFontSizePt = 15;
/// Default line-height ×100 (140 → 1.4).
inline constexpr int kEpubDefaultLineHeightPercent = 140;
/// Layout pixel density for EPUB pages (same convention as PDF layout DPI).
inline constexpr int kEpubLayoutDpi = 144;

/// Grid tiles (Phase 4 / Galapix-compatible). See TILES.md.
inline constexpr int kTileSize = 256;
inline constexpr int kDefaultTileQuality = 80;
/// PDF live/durable cells: text rings badly at Q=80 when zoomed; use higher.
inline constexpr int kPdfTileQuality = 95;
inline constexpr char kDefaultTileCodec[] = "jpeg";
/// Uncompressed RGB888 payload in TileBlob::bytes (live PDF cells).
inline constexpr char kTileCodecRgb888[] = "rgb888";

/// Durable HTTP body cache TTL (0 = never expire by age).
inline constexpr std::int64_t kHttpCacheTtlSeconds = 7LL * 24 * 3600;
/// Refuse tile encode when width*height exceeds this (memory guard).
inline constexpr std::int64_t kTileMaxSourcePixels = 100000000LL;  // 100 MP

}  // namespace thumtoo
