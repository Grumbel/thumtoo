// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/format.hpp"

#include <cctype>
#include <string>

namespace thumtoo {
namespace {

void ascii_tolower_inplace(std::string& s) {
  for (char& c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// Raster formats libvips (and our probe/ladder/tile paths) commonly handle.
constexpr std::string_view kImageExts[] = {
    ".jpg", ".jpeg", ".jpe", ".png", ".gif", ".bmp", ".webp", ".jxl",
    ".tif", ".tiff", ".heic", ".heif", ".avif",
};

// Multi-suffix archives compared against the full lowercased filename.
constexpr std::string_view kArchiveSuffixes[] = {
    ".zip",     ".cbz",     ".cbr",     ".rar",     ".7z",
    ".tar",     ".tgz",     ".tbz2",    ".txz",
    ".tar.gz",  ".tar.bz2", ".tar.xz",
};

constexpr std::string_view kMediaMimes[] = {
    // Images
    "image/jpeg",
    "image/png",
    "image/gif",
    "image/bmp",
    "image/webp",
    "image/jxl",
    "image/tiff",
    "image/heic",
    "image/heif",
    "image/avif",
    // Documents (page raster via Poppler / DjVuLibre when enabled)
    "application/pdf",
    "image/vnd.djvu",
    "image/vnd.djvu+multipage",
    "application/epub+zip",
    // Archives (expanded to image members)
    "application/zip",
    "application/vnd.rar",
    "application/x-7z-compressed",
    "application/x-tar",
    "application/gzip",
};

}  // namespace

std::string path_extension_lower(const std::filesystem::path& path) {
  auto ext = path.extension().string();
  ascii_tolower_inplace(ext);
  return ext;
}

std::string member_extension_lower(std::string_view member_path) {
  if (member_path.empty()) return {};
  const auto slash = member_path.find_last_of("/\\");
  const auto name = slash == std::string_view::npos
                        ? member_path
                        : member_path.substr(slash + 1);
  const auto dot = name.find_last_of('.');
  if (dot == std::string_view::npos) return {};
  std::string ext(name.substr(dot));
  ascii_tolower_inplace(ext);
  return ext;
}

bool is_image_extension(std::string_view ext_with_dot) {
  for (auto e : kImageExts) {
    if (ext_with_dot == e) return true;
  }
  return false;
}

bool is_archive_filename(std::string_view lower_filename) {
  for (auto suf : kArchiveSuffixes) {
    if (lower_filename.size() >= suf.size() &&
        lower_filename.compare(lower_filename.size() - suf.size(), suf.size(),
                               suf) == 0)
      return true;
  }
  return false;
}

bool is_pdf_extension(std::string_view ext_with_dot) {
  return ext_with_dot == ".pdf";
}

bool is_epub_extension(std::string_view ext_with_dot) {
  return ext_with_dot == ".epub";
}

bool is_djvu_extension(std::string_view ext_with_dot) {
  return ext_with_dot == ".djvu" || ext_with_dot == ".djv";
}

bool is_image_path(const std::filesystem::path& path) {
  return is_image_extension(path_extension_lower(path));
}

bool is_archive_path(const std::filesystem::path& path) {
  std::string name = path.filename().string();
  ascii_tolower_inplace(name);
  return is_archive_filename(name);
}

bool is_pdf_path(const std::filesystem::path& path) {
  return is_pdf_extension(path_extension_lower(path));
}

bool is_djvu_path(const std::filesystem::path& path) {
  return is_djvu_extension(path_extension_lower(path));
}

bool is_epub_path(const std::filesystem::path& path) {
  return is_epub_extension(path_extension_lower(path));
}

PathKind classify_path(const std::filesystem::path& path) {
  // Order: archive/document before generic image.
  // Note: .epub is a zip; classify as Epub before Archive so we layout pages
  // instead of expanding as a zip of HTML/CSS members.
  if (is_epub_path(path)) return PathKind::Epub;
  if (is_archive_path(path)) return PathKind::Archive;
  if (is_pdf_path(path)) return PathKind::Pdf;
  if (is_djvu_path(path)) return PathKind::Djvu;
  if (is_image_path(path)) return PathKind::Image;
  return PathKind::Unsupported;
}

std::vector<std::string_view> image_extensions() {
  return {std::begin(kImageExts), std::end(kImageExts)};
}

std::vector<std::string_view> archive_suffixes() {
  return {std::begin(kArchiveSuffixes), std::end(kArchiveSuffixes)};
}

std::vector<std::string_view> media_mime_types() {
  return {std::begin(kMediaMimes), std::end(kMediaMimes)};
}

std::string desktop_mime_types_line() {
  std::string out;
  for (auto m : kMediaMimes) {
    out.append(m);
    out.push_back(';');
  }
  return out;
}

}  // namespace thumtoo
