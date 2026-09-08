// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace thumtoo {

/// Expand a filesystem path into one or more media location URIs for display.
///
/// - Raster image → single `file://…` URI
/// - Multipage PDF / DjVu → `file://…//page:N` (1-based, capped at max_pages)
/// - Archive (zip/cbz/rar/…) → `file://…//archive:member` for image members
/// - Missing / unreadable / non-media → empty vector (caller may keep original)
///
/// Does not touch the database; pure source inspection. Viewers should use this
/// instead of reimplementing format-specific page/TOC expansion.
[[nodiscard]] std::vector<std::string> expand_media_uris(
    const std::filesystem::path& path, int max_pages = 512);

/// True if the path is something expand_media_uris / prepare_paths would treat
/// as openable media (image, PDF, DjVu, or archive container).
[[nodiscard]] bool is_openable_media_path(const std::filesystem::path& path);

}  // namespace thumtoo
