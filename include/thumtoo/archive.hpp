// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace thumtoo {

struct ArchiveMember {
  std::string member_path;
  std::optional<std::int64_t> uncompressed_size;
};

struct ParsedArchiveUri {
  std::filesystem::path archive_path;
  /// Empty means the archive root TOC URI (…//archive without :member).
  std::string member_path;
};

/// Read archive TOC via libarchive (source I/O — not cache-only).
[[nodiscard]] std::optional<std::vector<ArchiveMember>> read_archive_toc(
    const std::filesystem::path& archive_path);

/// Extract one regular-file member into memory (enforces size caps).
[[nodiscard]] std::optional<std::vector<std::uint8_t>> extract_archive_member(
    const std::filesystem::path& archive_path, std::string_view member_path);

/// Open the archive once and extract every requested member (same size caps).
/// Keys in the result are the requested member_path strings that succeeded.
[[nodiscard]] std::unordered_map<std::string, std::vector<std::uint8_t>>
extract_archive_members(const std::filesystem::path& archive_path,
                        const std::vector<std::string>& member_paths);

/// file:///abs.zip//archive  or  file:///abs.zip//archive:member
[[nodiscard]] std::string archive_uri(const std::filesystem::path& archive_path,
                                      std::string_view member_path = {});

/// Parse Location form with //archive or //archive:member.
[[nodiscard]] std::optional<ParsedArchiveUri> parse_archive_uri(
    std::string_view uri);

/// True if member path is unsafe (.., absolute, NUL).
[[nodiscard]] bool is_unsafe_archive_member_path(std::string_view member_path);

/// Extension heuristic for archive containers (zip/cbz/rar/7z/tar…).
[[nodiscard]] bool is_likely_archive_path(const std::filesystem::path& path);

/// Extension heuristic for image members we can probe/ladder (jpeg/png/…).
[[nodiscard]] bool is_likely_image_member_path(std::string_view member_path);

}  // namespace thumtoo
