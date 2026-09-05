// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/archive.hpp"
#include "thumtoo/uri.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <cstring>

namespace thumtoo {

std::string archive_uri(const std::filesystem::path& archive_path,
                        std::string_view member_path) {
  auto uri = file_uri_from_path(archive_path.lexically_normal());
  uri += "//archive";
  if (!member_path.empty()) {
    uri += ':';
    uri += member_path;
  }
  return uri;
}

std::optional<std::vector<ArchiveMember>> read_archive_toc(
    const std::filesystem::path& archive_path) {
  struct archive* a = archive_read_new();
  if (!a) return std::nullopt;
  archive_read_support_filter_all(a);
  archive_read_support_format_all(a);

  if (archive_read_open_filename(a, archive_path.string().c_str(), 10240) !=
      ARCHIVE_OK) {
    archive_read_free(a);
    return std::nullopt;
  }

  std::vector<ArchiveMember> out;
  struct archive_entry* entry = nullptr;
  while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
    const char* path = archive_entry_pathname(entry);
    if (!path) {
      archive_read_data_skip(a);
      continue;
    }
    // Path sanitization (DESIGN archive security): reject .. and absolute.
    const std::string member = path;
    if (member.find("..") != std::string::npos ||
        (!member.empty() && member[0] == '/')) {
      archive_read_data_skip(a);
      continue;
    }
    if (archive_entry_filetype(entry) == AE_IFDIR) {
      archive_read_data_skip(a);
      continue;
    }
    ArchiveMember m;
    m.member_path = member;
    const auto sz = archive_entry_size(entry);
    if (sz >= 0) m.uncompressed_size = sz;
    out.push_back(std::move(m));
    archive_read_data_skip(a);
  }
  archive_read_free(a);
  return out;
}

}  // namespace thumtoo
