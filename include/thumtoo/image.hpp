// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thumtoo/types.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace thumtoo {

struct DecodedImage {
  int width = 0;
  int height = 0;
  int channels = 0;  // 1..4 as loaded
  std::vector<std::uint8_t> rgba;  // always RGBA8, size width*height*4
};

struct ProbeResult {
  Size size;
  std::string format;  // e.g. "jpeg", "png", "gif", "bmp", "unknown"
};

struct LevelBlob {
  int max_edge = 0;
  int frame_idx = 0;
  int width = 0;
  int height = 0;
  std::string codec;   // "jpeg" until WebP linked
  int quality = 0;
  std::string relative_path;  // under cache blobs/
  std::vector<std::uint8_t> bytes;
};

[[nodiscard]] std::optional<ProbeResult> probe_image_file(
    const std::filesystem::path& path);

[[nodiscard]] std::optional<DecodedImage> load_image_file(
    const std::filesystem::path& path);

/// Scale so the longer edge is <= max_edge (no upscale).
[[nodiscard]] DecodedImage resize_to_max_edge(const DecodedImage& src,
                                              int max_edge);

/// Encode RGB/RGBA as JPEG into memory.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> encode_jpeg(
    const DecodedImage& img, int quality);

/// Build ladder blobs for all kLadderEdges (and smaller if image is small).
[[nodiscard]] std::vector<LevelBlob> build_ladder(
    const DecodedImage& src, const std::string& content_id, int quality);

/// SHA-256 hex of file contents; empty on I/O error.
[[nodiscard]] std::string sha256_file_hex(const std::filesystem::path& path);

}  // namespace thumtoo
