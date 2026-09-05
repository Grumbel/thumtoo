// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thumtoo {

struct ArchiveMember {
  std::string member_path;
  std::optional<std::int64_t> uncompressed_size;
};

/// Read archive TOC via libarchive (source I/O — not cache-only).
[[nodiscard]] std::optional<std::vector<ArchiveMember>> read_archive_toc(
    const std::filesystem::path& archive_path);

/// file:///abs.zip//archive  or  file:///abs.zip//archive:member
[[nodiscard]] std::string archive_uri(const std::filesystem::path& archive_path,
                                      std::string_view member_path = {});

}  // namespace thumtoo
