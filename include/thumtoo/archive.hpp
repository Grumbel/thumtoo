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

/**
 * FastBatch archive cursor state (PIXEL_PIPELINE_REDESIGN §5).
 * One sequential extract stream per archive path for Lane A.
 * Hosts/workers update next_index after each successful batch.
 */
struct ArchiveCursor {
  std::filesystem::path archive_path;
  /// Image members in libarchive sequential (TOC) order.
  std::vector<std::string> ordered_members;
  /// Index into ordered_members of the next member preferred for forward extract.
  std::size_t next_index = 0;
};

/**
 * Plan a sequential extract window for FastBatch.
 *
 * @param ordered_members  TOC-ordered image member paths (cursor.ordered_members).
 * @param interest         Requested member paths (any order); unknown paths ignored.
 * @param next_index       Cursor position; used to prefer continuing forward.
 * @param max_window       Cap on returned size (default kBatchWindowMembers).
 * @return Members in TOC order, length ≤ max_window. Prefer a contiguous span
 *         that covers interest near the cursor; if interest is empty, returns
 *         up to max_window members starting at next_index.
 *
 * Pure function — no I/O. Does not mutate the cursor; caller advances
 * next_index after extract using the last planned member's TOC index + 1.
 */
[[nodiscard]] std::vector<std::string> plan_archive_batch_window(
    const std::vector<std::string>& ordered_members,
    const std::vector<std::string>& interest,
    std::size_t next_index,
    int max_window = 32);

/// TOC index of member_path in ordered_members, or nullopt if absent.
[[nodiscard]] std::optional<std::size_t> archive_member_toc_index(
    const std::vector<std::string>& ordered_members,
    std::string_view member_path);

}  // namespace thumtoo
