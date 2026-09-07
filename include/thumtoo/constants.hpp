// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace thumtoo {

/// schema_meta / schema_version = 1 (DESIGN §6b). Bump only with incompatible layout.
inline constexpr int kSchemaVersion = 1;

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

/// PDF page layout / live tile rasterization (1 pt = 1 px at 72 dpi).
/// Higher than 72 so interactive tiles are sharper than screen-media-box.
inline constexpr int kPdfLayoutDpi = 144;

/// Grid tiles (Phase 4 / Galapix-compatible). See TILES.md.
inline constexpr int kTileSize = 256;
inline constexpr int kDefaultTileQuality = 80;
inline constexpr char kDefaultTileCodec[] = "jpeg";

/// Durable HTTP body cache TTL (0 = never expire by age).
inline constexpr std::int64_t kHttpCacheTtlSeconds = 7LL * 24 * 3600;
/// Refuse tile encode when width*height exceeds this (memory guard).
inline constexpr std::int64_t kTileMaxSourcePixels = 100000000LL;  // 100 MP

}  // namespace thumtoo
