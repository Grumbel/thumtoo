// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace thumtoo {

/// How prepare_paths / open classify a filesystem path (extension heuristic).
enum class PathKind {
  Unsupported = 0,
  Image,
  Archive,
  Pdf,
  Djvu,
};

/// Lowercased extension including the dot (".jpg"), or empty.
[[nodiscard]] std::string path_extension_lower(const std::filesystem::path& path);

/// Lowercased extension of a member basename (".png"), or empty.
[[nodiscard]] std::string member_extension_lower(std::string_view member_path);

[[nodiscard]] bool is_image_extension(std::string_view ext_with_dot);
[[nodiscard]] bool is_archive_filename(std::string_view lower_filename);
[[nodiscard]] bool is_pdf_extension(std::string_view ext_with_dot);
[[nodiscard]] bool is_djvu_extension(std::string_view ext_with_dot);

[[nodiscard]] bool is_image_path(const std::filesystem::path& path);
[[nodiscard]] bool is_archive_path(const std::filesystem::path& path);
[[nodiscard]] bool is_pdf_path(const std::filesystem::path& path);
[[nodiscard]] bool is_djvu_path(const std::filesystem::path& path);

[[nodiscard]] PathKind classify_path(const std::filesystem::path& path);

/// Extensions thumtoo will treat as raster images (including the dot).
[[nodiscard]] std::vector<std::string_view> image_extensions();

/// Archive container suffixes (including the dot; may be multi-dot like ".tar.gz").
[[nodiscard]] std::vector<std::string_view> archive_suffixes();

/// Freedesktop-style MIME types for images (+ PDF + common archives) for
/// .desktop files and file dialogs. Apps should use this instead of a private list.
[[nodiscard]] std::vector<std::string_view> media_mime_types();

/// Single line suitable for MimeType= in a .desktop file (semicolon-separated, trailing ;).
[[nodiscard]] std::string desktop_mime_types_line();

}  // namespace thumtoo
