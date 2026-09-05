// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thumtoo/status.hpp"

#include <cstdint>
#include <optional>
#include <string>

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

}  // namespace thumtoo
