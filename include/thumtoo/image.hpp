// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thumtoo/constants.hpp"
#include "thumtoo/types.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thumtoo {

struct ProbeResult {
  Size size;
  std::string format;
};

struct LevelBlob {
  int max_edge = 0;
  int frame_idx = 0;
  int width = 0;
  int height = 0;
  std::string codec;  // "jxl"
  int quality = 0;
  std::string relative_path;
  std::vector<std::uint8_t> bytes;
};

void image_library_init();

[[nodiscard]] std::optional<ProbeResult> probe_image_file(
    const std::filesystem::path& path);

[[nodiscard]] std::optional<ProbeResult> probe_image_buffer(
    const std::uint8_t* data, std::size_t size, std::string_view hint_format = {});

[[nodiscard]] std::vector<LevelBlob> build_ladder(
    const std::filesystem::path& path, const std::string& content_id,
    int jxl_quality);

[[nodiscard]] std::vector<LevelBlob> build_ladder_buffer(
    const std::uint8_t* data, std::size_t size, const std::string& content_id,
    int jxl_quality);

/// Build JXL ladder from contiguous RGB888 pixels (no alpha).
[[nodiscard]] std::vector<LevelBlob> build_ladder_rgb(
    const std::uint8_t* rgb, int width, int height, const std::string& content_id,
    int jxl_quality);

[[nodiscard]] std::string sha256_file_hex(const std::filesystem::path& path);
[[nodiscard]] std::string sha256_bytes_hex(const std::uint8_t* data,
                                           std::size_t size);

/// Galapix-style 256² JPEG tile pyramid. scale 0 = full res; higher = coarser.
/// Generates [min_scale .. max_scale] inclusive. If max_scale < 0, goes until
/// the image fits in a single tile.
[[nodiscard]] std::vector<TileBlob> build_tile_pyramid(
    const std::filesystem::path& path, int min_scale = 0, int max_scale = -1,
    int jpeg_quality = kDefaultTileQuality);

[[nodiscard]] std::vector<TileBlob> build_tile_pyramid_buffer(
    const std::uint8_t* data, std::size_t size, int min_scale = 0,
    int max_scale = -1, int jpeg_quality = kDefaultTileQuality);

}  // namespace thumtoo
