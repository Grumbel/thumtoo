// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/expand.hpp"

#include "thumtoo/archive.hpp"
#include "thumtoo/djvu.hpp"
#include "thumtoo/format.hpp"
#include "thumtoo/epub.hpp"
#include "thumtoo/pdf.hpp"
#include "thumtoo/uri.hpp"

#include <algorithm>
#include <filesystem>

namespace thumtoo {

bool is_openable_media_path(const std::filesystem::path& path) {
  switch (classify_path(path)) {
    case PathKind::Image:
    case PathKind::Pdf:
    case PathKind::Djvu:
    case PathKind::Epub:
    case PathKind::Archive:
      return true;
    case PathKind::Unsupported:
      return false;
  }
  return false;
}

std::vector<std::string> expand_media_uris(const std::filesystem::path& path,
                                           int max_pages) {
  std::vector<std::string> out;
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec) {
    return out;
  }

  const auto abs = path.lexically_normal();
  const int page_cap = max_pages > 0 ? max_pages : 512;

  switch (classify_path(abs)) {
    case PathKind::Pdf: {
      auto count = pdf_page_count(abs);
      if (!count || *count < 1) {
        // Still surface the file as a single locator so the UI can report
        // failure; callers that need "skip on fail" check empty after probe.
        out.push_back(file_uri_from_path(abs));
        return out;
      }
      const int n = std::min(*count, page_cap);
      out.reserve(static_cast<std::size_t>(n));
      for (int page = 1; page <= n; ++page) {
        out.push_back(pdf_page_uri(abs, page));
      }
      return out;
    }
    case PathKind::Djvu: {
      auto count = djvu_page_count(abs);
      if (!count || *count < 1) {
        out.push_back(file_uri_from_path(abs));
        return out;
      }
      const int n = std::min(*count, page_cap);
      out.reserve(static_cast<std::size_t>(n));
      for (int page = 1; page <= n; ++page) {
        out.push_back(djvu_page_uri(abs, page));
      }
      return out;
    }
    case PathKind::Epub: {
      const auto layout = default_epub_layout();
      auto count = epub_page_count(abs, layout);
      if (!count || *count < 1) {
        out.push_back(file_uri_from_path(abs));
        return out;
      }
      const int n = std::min(*count, page_cap);
      out.reserve(static_cast<std::size_t>(n));
      for (int page = 1; page <= n; ++page) {
        out.push_back(epub_page_uri(abs, page, layout));
      }
      return out;
    }
    case PathKind::Archive: {
      auto toc = read_archive_toc(abs);
      if (!toc) {
        return out;
      }
      for (auto const& mem : *toc) {
        if (!is_likely_image_member_path(mem.member_path)) {
          continue;
        }
        out.push_back(archive_uri(abs, mem.member_path));
      }
      return out;
    }
    case PathKind::Image:
      out.push_back(file_uri_from_path(abs));
      return out;
    case PathKind::Unsupported:
      return out;
  }
  return out;
}

}  // namespace thumtoo

std::vector<std::string> expand_pdf_image_uris(const std::filesystem::path& path,
                                               int max_images) {
  std::vector<std::string> out;
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec) return out;
  const auto abs = path.lexically_normal();
  auto count = pdf_embedded_image_count(abs);
  if (!count || *count < 1) return out;
  const int cap = max_images > 0 ? max_images : 4096;
  const int n = std::min(*count, cap);
  out.reserve(static_cast<std::size_t>(n));
  for (int i = 1; i <= n; ++i) {
    out.push_back(pdf_image_uri(abs, i));
  }
  return out;
}

std::vector<std::string> expand_pdf_images_collection_uri(std::string_view uri,
                                                          int max_images) {
  if (!is_pdf_images_collection_uri(uri)) return {};
  // Strip //pdfimages (and any trailing junk) to get file base.
  auto path = path_from_file_uri(uri);
  if (!path) {
    // Plain path + //pdfimages
    auto pos = uri.find("//pdfimages");
    if (pos == std::string_view::npos) return {};
    path = std::filesystem::path(std::string(uri.substr(0, pos)));
  }
  return expand_pdf_image_uris(*path, max_images);
}
