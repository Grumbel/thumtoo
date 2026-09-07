// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "thumtoo/constants.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thumtoo {

/// Build a file:/// URI from an absolute filesystem path (DESIGN Location form).
[[nodiscard]] std::string file_uri_from_path(const std::filesystem::path& absolute_path);

/// If uri is file:///..., return the outer filesystem path (strips //archive and
/// //page pipes). Otherwise nullopt.
[[nodiscard]] std::optional<std::filesystem::path> path_from_file_uri(
    std::string_view uri);

/// True if uri uses the //archive: pipe form somewhere after file:///.
[[nodiscard]] bool is_archive_uri(std::string_view uri);

/// True if uri contains //page:N (PDF page location).
[[nodiscard]] bool is_pdf_page_uri(std::string_view uri);

/// True if scheme is http: or https: (fetch not implemented yet).
[[nodiscard]] bool is_http_uri(std::string_view uri);

/// True if uri is a content-addressed id (sha256:… / sha1:…).
[[nodiscard]] bool is_content_id_uri(std::string_view uri);

/// Also see kContentIdSha256Prefix in constants.hpp.
inline constexpr std::string_view kContentIdSha1Prefix = "sha1:";

/// Build sha256:<hex> (hex may already include the prefix).
[[nodiscard]] std::string content_id_uri_from_sha256_hex(std::string_view hex);

/// Extract hex after sha256: / sha1: or nullopt.
[[nodiscard]] std::optional<std::string> content_id_hex(std::string_view uri);

// --- Composable Location model (DESIGN § Locator URI) ---

enum class UriScheme {
  File,
  Http,
  Https,
  ContentSha256,
  ContentSha1,
  Unknown,
};

enum class LocationPipeKind {
  ArchiveRoot,    // …//archive
  ArchiveMember,  // …//archive:member/path
  PdfPage,        // …//page:N (1-based)
};

struct LocationPipe {
  LocationPipeKind kind = LocationPipeKind::ArchiveRoot;
  /// Member path, or decimal page number for PdfPage; empty for ArchiveRoot.
  std::string value;
};

/// Parsed location: scheme + base + ordered pipes (outer → inner).
struct Location {
  UriScheme scheme = UriScheme::Unknown;
  /// Absolute path (file), host/path (http), or full content id string.
  std::string base;
  std::vector<LocationPipe> pipes;
};

/// Parse DESIGN Location form. Does not resolve or fetch.
[[nodiscard]] std::optional<Location> parse_location(std::string_view uri);

/// Format Location back to a URI string.
[[nodiscard]] std::string format_location(const Location& loc);

/// Append //archive:member (or //archive if member empty) onto a base URI.
[[nodiscard]] std::string with_archive_member(std::string_view base_uri,
                                             std::string_view member_path = {});

/// Append //page:N (1-based) onto a base URI (typically a PDF file or archive member).
[[nodiscard]] std::string with_pdf_page(std::string_view base_uri, int page_1based);

}  // namespace thumtoo
