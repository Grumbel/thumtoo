// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/uri.hpp"

#include <sstream>

namespace thumtoo {

std::string file_uri_from_path(const std::filesystem::path& absolute_path) {
  const auto p = absolute_path.lexically_normal().generic_string();
  // Minimal encoding: spaces only (full percent-encoding can come later).
  std::string out = "file://";
  for (char c : p) {
    if (c == ' ')
      out += "%20";
    else
      out += c;
  }
  return out;
}

std::optional<std::filesystem::path> path_from_file_uri(std::string_view uri) {
  constexpr std::string_view kPrefix = "file://";
  if (!uri.starts_with(kPrefix)) return std::nullopt;
  std::string_view rest = uri.substr(kPrefix.size());
  // Strip //archive: ... if present (path is before the pipe).
  const auto pipe = rest.find("//archive");
  if (pipe != std::string_view::npos) rest = rest.substr(0, pipe);

  std::string path;
  path.reserve(rest.size());
  for (std::size_t i = 0; i < rest.size(); ++i) {
    if (rest[i] == '%' && i + 2 < rest.size()) {
      auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      const int hi = hex(rest[i + 1]);
      const int lo = hex(rest[i + 2]);
      if (hi >= 0 && lo >= 0) {
        path.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    path.push_back(rest[i]);
  }
  if (path.empty()) return std::nullopt;
  return std::filesystem::path(path);
}

bool is_archive_uri(std::string_view uri) {
  return uri.find("//archive") != std::string_view::npos;
}

}  // namespace thumtoo
