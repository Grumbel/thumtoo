// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/uri.hpp"

#include <cctype>
#include <sstream>

namespace thumtoo {
namespace {

constexpr std::string_view kArchivePipe = "//archive";
constexpr std::string_view kPagePipe = "//page:";
constexpr std::string_view kPopplerPagePipe = "//poppler-page:";
constexpr std::string_view kMupdfPagePipe = "//mupdf-page:";

[[nodiscard]] std::size_t find_first_pipe(std::string_view rest, std::size_t from,
                                          bool* is_page, LocationPipeKind* page_kind) {
  const auto arch = rest.find(kArchivePipe, from);
  const auto page = rest.find(kPagePipe, from);
  const auto pop = rest.find(kPopplerPagePipe, from);
  const auto mu = rest.find(kMupdfPagePipe, from);
  std::size_t next = std::string_view::npos;
  *is_page = false;
  auto consider = [&](std::size_t pos, bool page_pipe, LocationPipeKind kind) {
    if (pos == std::string_view::npos) return;
    if (next == std::string_view::npos || pos < next) {
      next = pos;
      *is_page = page_pipe;
      if (page_pipe) *page_kind = kind;
    }
  };
  consider(arch, false, LocationPipeKind::ArchiveRoot);
  consider(page, true, LocationPipeKind::PdfPage);
  consider(pop, true, LocationPipeKind::PdfPagePoppler);
  consider(mu, true, LocationPipeKind::PdfPageMupdf);
  return next;
}

std::string percent_decode_path(std::string_view rest) {
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
  return path;
}

std::string_view strip_pipes(std::string_view rest) {
  bool is_page = false;
  LocationPipeKind pk = LocationPipeKind::PdfPage;
  const auto next = find_first_pipe(rest, 0, &is_page, &pk);
  if (next == std::string_view::npos) return rest;
  return rest.substr(0, next);
}

bool parse_pipes(std::string_view rest, std::vector<LocationPipe>& out) {
  // Caller passes the full post-scheme remainder; scan for archive and page pipes.
  std::size_t i = 0;
  while (i < rest.size()) {
    bool is_page = false;
    LocationPipeKind page_kind = LocationPipeKind::PdfPage;
    const std::size_t next = find_first_pipe(rest, i, &is_page, &page_kind);
    if (next == std::string_view::npos) break;

    if (is_page) {
      std::string_view tag = kPagePipe;
      if (page_kind == LocationPipeKind::PdfPagePoppler) tag = kPopplerPagePipe;
      else if (page_kind == LocationPipeKind::PdfPageMupdf) tag = kMupdfPagePipe;
      std::string_view after = rest.substr(next + tag.size());
      std::size_t n = 0;
      while (n < after.size() && after[n] >= '0' && after[n] <= '9') ++n;
      if (n == 0) return false;
      LocationPipe pipe;
      pipe.kind = page_kind;
      pipe.value = std::string(after.substr(0, n));
      out.push_back(std::move(pipe));
      i = next + tag.size() + n;
    } else {
      std::string_view after = rest.substr(next + kArchivePipe.size());
      LocationPipe pipe;
      if (after.empty()) {
        pipe.kind = LocationPipeKind::ArchiveRoot;
        pipe.value.clear();
        out.push_back(std::move(pipe));
        i = rest.size();
        break;
      }
      if (after.front() != ':') {
        // //archive without colon only valid if end
        return false;
      }
      after.remove_prefix(1);
      // Member runs until next pipe or end
      std::size_t mem_end = after.size();
      bool dummy = false;
      LocationPipeKind pk = LocationPipeKind::PdfPage;
      const auto na = after.find(kArchivePipe);
      const auto np = find_first_pipe(after, 0, &dummy, &pk);
      if (na != std::string_view::npos) mem_end = std::min(mem_end, na);
      if (np != std::string_view::npos) mem_end = std::min(mem_end, np);
      pipe.kind = LocationPipeKind::ArchiveMember;
      pipe.value = std::string(after.substr(0, mem_end));
      out.push_back(std::move(pipe));
      i = next + kArchivePipe.size() + 1 + mem_end;
    }
  }
  return true;
}

}  // namespace

std::string file_uri_from_path(const std::filesystem::path& absolute_path) {
  const auto p = absolute_path.lexically_normal().generic_string();
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
  rest = strip_pipes(rest);
  std::string path = percent_decode_path(rest);
  if (path.empty()) return std::nullopt;
  return std::filesystem::path(path);
}

bool is_archive_uri(std::string_view uri) {
  return uri.find(kArchivePipe) != std::string_view::npos;
}

bool is_pdf_page_uri(std::string_view uri) {
  return uri.find(kPagePipe) != std::string_view::npos;
}

bool is_http_uri(std::string_view uri) {
  return uri.starts_with("http://") || uri.starts_with("https://");
}

bool is_content_id_uri(std::string_view uri) {
  return uri.starts_with(kContentIdSha256Prefix) || uri.starts_with(kContentIdSha1Prefix);
}

std::string content_id_uri_from_sha256_hex(std::string_view hex) {
  if (hex.starts_with(kContentIdSha256Prefix)) return std::string(hex);
  std::string out(kContentIdSha256Prefix);
  out.append(hex);
  return out;
}

std::optional<std::string> content_id_hex(std::string_view uri) {
  if (uri.starts_with(kContentIdSha256Prefix))
    return std::string(uri.substr(kContentIdSha256Prefix.size()));
  if (uri.starts_with(kContentIdSha1Prefix))
    return std::string(uri.substr(kContentIdSha1Prefix.size()));
  return std::nullopt;
}

std::optional<Location> parse_location(std::string_view uri) {
  Location loc;
  if (uri.starts_with("file://")) {
    loc.scheme = UriScheme::File;
    std::string_view rest = uri.substr(7);
    auto base = strip_pipes(rest);
    loc.base = percent_decode_path(base);
    if (loc.base.empty()) return std::nullopt;
    if (!parse_pipes(rest, loc.pipes)) return std::nullopt;
    return loc;
  }
  if (uri.starts_with("https://")) {
    loc.scheme = UriScheme::Https;
    loc.base = std::string(uri.substr(8));
    // Pipes on remote URLs are reserved for later; reject nested for now.
    if (loc.base.find("//archive") != std::string::npos ||
        loc.base.find("//page:") != std::string::npos ||
        loc.base.find("//poppler-page:") != std::string::npos ||
        loc.base.find("//mupdf-page:") != std::string::npos) {
      // Allow pipes on the path portion for future networked archives
      std::string_view rest = uri.substr(8);
      auto base = strip_pipes(rest);
      loc.base = std::string(base);
      if (!parse_pipes(rest, loc.pipes)) return std::nullopt;
    }
    if (loc.base.empty()) return std::nullopt;
    return loc;
  }
  if (uri.starts_with("http://")) {
    loc.scheme = UriScheme::Http;
    std::string_view rest = uri.substr(7);
    auto base = strip_pipes(rest);
    loc.base = std::string(base);
    if (loc.base.empty()) return std::nullopt;
    if (!parse_pipes(rest, loc.pipes)) return std::nullopt;
    return loc;
  }
  if (uri.starts_with(kContentIdSha256Prefix)) {
    loc.scheme = UriScheme::ContentSha256;
    loc.base = std::string(uri);
    return loc;
  }
  if (uri.starts_with(kContentIdSha1Prefix)) {
    loc.scheme = UriScheme::ContentSha1;
    loc.base = std::string(uri);
    return loc;
  }
  return std::nullopt;
}

std::string format_location(const Location& loc) {
  std::string out;
  switch (loc.scheme) {
    case UriScheme::File:
      out = file_uri_from_path(loc.base);
      break;
    case UriScheme::Http:
      out = "http://";
      out += loc.base;
      break;
    case UriScheme::Https:
      out = "https://";
      out += loc.base;
      break;
    case UriScheme::ContentSha256:
    case UriScheme::ContentSha1:
      return loc.base;
    case UriScheme::Unknown:
      return {};
  }
  for (const auto& pipe : loc.pipes) {
    switch (pipe.kind) {
      case LocationPipeKind::ArchiveRoot:
        out += "//archive";
        break;
      case LocationPipeKind::ArchiveMember:
        out += "//archive:";
        out += pipe.value;
        break;
      case LocationPipeKind::PdfPage:
        out += "//page:";
        out += pipe.value;
        break;
      case LocationPipeKind::PdfPagePoppler:
        out += "//poppler-page:";
        out += pipe.value;
        break;
      case LocationPipeKind::PdfPageMupdf:
        out += "//mupdf-page:";
        out += pipe.value;
        break;
    }
  }
  return out;
}

std::string with_archive_member(std::string_view base_uri, std::string_view member_path) {
  std::string out(base_uri);
  if (member_path.empty()) {
    out += "//archive";
  } else {
    out += "//archive:";
    out.append(member_path);
  }
  return out;
}

std::string with_pdf_page(std::string_view base_uri, int page_1based) {
  if (page_1based < 1) page_1based = 1;
  std::string out(base_uri);
  out += "//page:";
  out += std::to_string(page_1based);
  return out;
}

std::string with_pdf_page_poppler(std::string_view base_uri, int page_1based) {
  if (page_1based < 1) page_1based = 1;
  std::string out(base_uri);
  out += "//poppler-page:";
  out += std::to_string(page_1based);
  return out;
}

std::string with_pdf_page_mupdf(std::string_view base_uri, int page_1based) {
  if (page_1based < 1) page_1based = 1;
  std::string out(base_uri);
  out += "//mupdf-page:";
  out += std::to_string(page_1based);
  return out;
}

}  // namespace thumtoo
