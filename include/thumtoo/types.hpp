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

/// Encoded ladder level from cache (JPEG-XL bytes by default).
struct PixelLevel {
  int max_edge = 0;
  int frame_idx = 0;
  int width = 0;
  int height = 0;
  std::string codec;  // "jxl"
  std::vector<std::uint8_t> bytes;
};

}  // namespace thumtoo
