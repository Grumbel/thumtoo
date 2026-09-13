// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/// Optional libunarr backend for RAR/CBR (including solid RAR that libarchive
/// cannot extract). Sequential entry walk only — solid dictionaries do not
/// support free random member payload seeks.

#include "thumtoo/archive.hpp"
#include "thumtoo/constants.hpp"
#include "thumtoo/build_stats.hpp"

#include <unarr.h>

#include <cctype>
#include <memory>
#include <string>

namespace thumtoo {
namespace {

bool member_paths_equal(std::string_view a, std::string_view b) {
  auto strip = [](std::string_view s) {
    while (s.starts_with("./")) s.remove_prefix(2);
    return s;
  };
  a = strip(a);
  b = strip(b);
  if (a == b) return true;
  std::string aa(a), bb(b);
  for (char& c : aa)
    if (c == '\\') c = '/';
  for (char& c : bb)
    if (c == '\\') c = '/';
  return aa == bb;
}

bool path_looks_rar(const std::filesystem::path& path) {
  std::string ext = path.extension().string();
  for (char& c : ext)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (ext == ".rar" || ext == ".cbr") return true;
  std::string lower = path.filename().string();
  for (char& c : lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return lower.find(".rar") != std::string::npos;
}

struct UnarrHolder {
  ar_stream* stream = nullptr;
  ar_archive* ar = nullptr;
  ~UnarrHolder() {
    if (ar) ar_close_archive(ar);
    if (stream) ar_close(stream);
  }
};

std::unique_ptr<UnarrHolder> open_rar(const std::filesystem::path& path) {
  auto u = std::make_unique<UnarrHolder>();
  u->stream = ar_open_file(path.string().c_str());
  if (!u->stream) return nullptr;
  u->ar = ar_open_rar_archive(u->stream);
  if (!u->ar) return nullptr;
  return u;
}

std::optional<std::vector<std::uint8_t>> uncompress_current(ar_archive* ar) {
  const size_t declared = ar_entry_get_size(ar);
  if (declared > kArchiveMaxMemberUncompressedBytes) return std::nullopt;
  std::vector<std::uint8_t> buf(declared);
  if (declared == 0) return buf;
  if (!ar_entry_uncompress(ar, buf.data(), declared)) return std::nullopt;
  return buf;
}

}  // namespace

bool unarr_backend_available() { return true; }

bool archive_prefers_unarr(const std::filesystem::path& archive_path) {
  return path_looks_rar(archive_path);
}

std::optional<std::vector<ArchiveMember>> read_archive_toc_unarr(
    const std::filesystem::path& archive_path) {
  auto u = open_rar(archive_path);
  if (!u) return std::nullopt;

  std::vector<ArchiveMember> out;
  while (ar_parse_entry(u->ar)) {
    const char* name = ar_entry_get_name(u->ar);
    if (!name) continue;
    const std::string member = name;
    if (is_unsafe_archive_member_path(member)) continue;
    if (!member.empty() && (member.back() == '/' || member.back() == '\\'))
      continue;
    ArchiveMember m;
    m.member_path = member;
    const size_t sz = ar_entry_get_size(u->ar);
    if (sz > 0) m.uncompressed_size = static_cast<std::int64_t>(sz);
    out.push_back(std::move(m));
  }
  return out;
}

std::unordered_map<std::string, std::vector<std::uint8_t>>
extract_archive_members_unarr(const std::filesystem::path& archive_path,
                              const std::vector<std::string>& member_paths) {
  ScopedNsAccumulator timer(global_build_stats().archive_extract_ns);
  std::unordered_map<std::string, std::vector<std::uint8_t>> out;
  if (member_paths.empty()) return out;

  std::vector<std::string> wanted;
  for (const auto& m : member_paths) {
    if (!is_unsafe_archive_member_path(m)) wanted.push_back(m);
  }
  if (wanted.empty()) return out;

  auto u = open_rar(archive_path);
  if (!u) return out;

  // Solid: walk in archive order; discard-uncompress non-wanted members so the
  // solid dictionary stays consistent (no out-of-order parse_entry_at).
  while (ar_parse_entry(u->ar) && out.size() < wanted.size()) {
    const char* path = ar_entry_get_name(u->ar);
    if (!path) continue;

    const std::string* key = nullptr;
    for (const auto& m : wanted) {
      if (out.count(m)) continue;
      if (member_paths_equal(path, m)) {
        key = &m;
        break;
      }
    }
    if (!key) {
      const size_t sz = ar_entry_get_size(u->ar);
      if (sz == 0) continue;
      if (sz > kArchiveMaxMemberUncompressedBytes) break;
      std::vector<std::uint8_t> discard(sz);
      (void)ar_entry_uncompress(u->ar, discard.data(), sz);
      continue;
    }

    auto buf = uncompress_current(u->ar);
    if (buf) {
      global_build_stats().archive_bytes.fetch_add(
          buf->size(), std::memory_order_relaxed);
      out.emplace(*key, std::move(*buf));
    }
  }
  return out;
}

}  // namespace thumtoo
