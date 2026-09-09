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

/// True if uri contains a PDF page pipe (//page:N, //poppler-page:N, //mupdf-page:N).
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
  ArchiveRoot,     // …//archive
  ArchiveMember,   // …//archive:member/path
  EpubLayout,      // …//epub:w=1200,h=1800,fs=12
  PdfPage,         // …//page:N (1-based, default PDF backend)
  PdfPagePoppler,  // …//poppler-page:N
  PdfPageMupdf,    // …//mupdf-page:N
};

struct LocationPipe {
  LocationPipeKind kind = LocationPipeKind::ArchiveRoot;
  /// Member path, epub layout params, or decimal page number for PdfPage*;
  /// empty for ArchiveRoot.
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

/// Append //page:N (1-based) — default PDF backend route.
[[nodiscard]] std::string with_pdf_page(std::string_view base_uri, int page_1based);

/// Append //poppler-page:N or //mupdf-page:N for explicit backend comparison.
[[nodiscard]] std::string with_pdf_page_poppler(std::string_view base_uri,
                                                int page_1based);
[[nodiscard]] std::string with_pdf_page_mupdf(std::string_view base_uri,
                                              int page_1based);

/// EPUB layout profile for //epub: URIs.
/// w/h are pixels at kEpubLayoutDpi; fs is MuPDF default font size in points.
/// Optional per-side margins (mt/mr/mb/ml) are pixels at the same DPI and are
/// applied via MuPDF user CSS (converted to points). Defaults match kEpubDefault*.
struct EpubLayout {
  int width_px = 0;
  int height_px = 0;
  int fs_pt = 0;
  int mt_px = 0;  // top margin
  int mr_px = 0;  // right
  int mb_px = 0;  // bottom
  int ml_px = 0;  // left
};

[[nodiscard]] EpubLayout default_epub_layout();

/// Encode layout as the //epub: payload in canonical key order:
/// w=,h=,fs=[,mt=,mr=,mb=,ml= when any margin is non-zero].
[[nodiscard]] std::string format_epub_layout_params(const EpubLayout& layout);

/// Parse w=/h=/fs=/mt=/mr=/mb=/ml= from an //epub: value (missing keys keep defaults).
/// Unknown keys ignored.
[[nodiscard]] EpubLayout parse_epub_layout_params(std::string_view params);

/// Append //epub:… layout payload onto a base URI.
[[nodiscard]] std::string with_epub_layout(std::string_view base_uri,
                                           const EpubLayout& layout);

/// True if uri contains an //epub: layout pipe.
[[nodiscard]] bool is_epub_layout_uri(std::string_view uri);

}  // namespace thumtoo
