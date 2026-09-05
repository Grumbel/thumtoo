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

/// Call once from main/tools before any image work (vips_init).
void image_library_init();

[[nodiscard]] std::optional<ProbeResult> probe_image_file(
    const std::filesystem::path& path);

/// Build long-edge ladder as JPEG-XL via libvips (no upscale).
[[nodiscard]] std::vector<LevelBlob> build_ladder(
    const std::filesystem::path& path, const std::string& content_id,
    int jxl_quality);

[[nodiscard]] std::string sha256_file_hex(const std::filesystem::path& path);

}  // namespace thumtoo
