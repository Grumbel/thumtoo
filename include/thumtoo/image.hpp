// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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

[[nodiscard]] std::string sha256_file_hex(const std::filesystem::path& path);
[[nodiscard]] std::string sha256_bytes_hex(const std::uint8_t* data,
                                           std::size_t size);

}  // namespace thumtoo
