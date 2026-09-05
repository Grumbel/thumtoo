// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace thumtoo {

/// Build a file:/// URI from an absolute filesystem path (DESIGN Location form).
[[nodiscard]] std::string file_uri_from_path(const std::filesystem::path& absolute_path);

/// If uri is file:///..., return the path; otherwise nullopt.
[[nodiscard]] std::optional<std::filesystem::path> path_from_file_uri(
    std::string_view uri);

/// True if uri uses the //archive: pipe form somewhere after file:///.
[[nodiscard]] bool is_archive_uri(std::string_view uri);

}  // namespace thumtoo
