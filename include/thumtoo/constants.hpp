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

inline constexpr int kDefaultWebpQuality = 80;
inline constexpr char kDefaultCodec[] = "webp";

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
inline constexpr char kSchemaMetaWebpQualityKey[] = "webp_quality";

}  // namespace thumtoo
