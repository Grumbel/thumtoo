// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/archive.hpp"
#include "thumtoo/format.hpp"
#include "thumtoo/constants.hpp"
#include "thumtoo/uri.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <cstring>
#include <cctype>
#include <unordered_map>
#include "thumtoo/build_stats.hpp"

namespace thumtoo {
namespace {

bool member_paths_equal(std::string_view a, std::string_view b) {
  // Archives may use \ or leading ./ — normalize lightly.
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

}  // namespace

bool is_unsafe_archive_member_path(std::string_view member_path) {
  if (member_path.empty()) return true;
  if (member_path.find('\0') != std::string_view::npos) return true;
  if (member_path.front() == '/' || member_path.front() == '\\') return true;
  // Reject .. path segments.
  std::string_view s = member_path;
  while (!s.empty()) {
    const auto slash = s.find('/');
    const auto seg = slash == std::string_view::npos ? s : s.substr(0, slash);
    if (seg == "..") return true;
    if (slash == std::string_view::npos) break;
    s.remove_prefix(slash + 1);
  }
  return false;
}

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

std::optional<ParsedArchiveUri> parse_archive_uri(std::string_view uri) {
  constexpr std::string_view kPipe = "//archive";
  const auto pipe = uri.find(kPipe);
  if (pipe == std::string_view::npos) return std::nullopt;

  const auto outer = uri.substr(0, pipe);
  auto path = path_from_file_uri(outer);
  if (!path) {
    // path_from_file_uri expects full file:// URI; outer still includes file://
    path = path_from_file_uri(std::string(outer));
  }
  if (!path) return std::nullopt;

  ParsedArchiveUri out;
  out.archive_path = *path;
  std::string_view rest = uri.substr(pipe + kPipe.size());
  if (rest.starts_with(':')) {
    rest.remove_prefix(1);
    out.member_path = std::string(rest);
    if (is_unsafe_archive_member_path(out.member_path)) return std::nullopt;
  }
  return out;
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
    const std::string member = path;
    if (is_unsafe_archive_member_path(member)) {
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

namespace {

std::optional<std::vector<std::uint8_t>> read_entry_bytes(
    struct archive* a, struct archive_entry* entry) {
  if (archive_entry_filetype(entry) == AE_IFDIR) return std::nullopt;

  const la_int64_t declared = archive_entry_size(entry);
  if (declared > 0 &&
      static_cast<std::uint64_t>(declared) >
          kArchiveMaxMemberUncompressedBytes) {
    return std::nullopt;
  }

  std::vector<std::uint8_t> buf;
  if (declared > 0) buf.reserve(static_cast<std::size_t>(declared));

  char block[65536];
  for (;;) {
    const la_ssize_t n = archive_read_data(a, block, sizeof(block));
    if (n < 0) return std::nullopt;
    if (n == 0) break;
    if (buf.size() + static_cast<std::size_t>(n) >
        kArchiveMaxMemberUncompressedBytes) {
      return std::nullopt;
    }
    buf.insert(buf.end(), block, block + n);
  }
  return buf;
}

}  // namespace

std::unordered_map<std::string, std::vector<std::uint8_t>>
extract_archive_members(const std::filesystem::path& archive_path,
                        const std::vector<std::string>& member_paths) {
  ScopedNsAccumulator timer(global_build_stats().archive_extract_ns);
  std::unordered_map<std::string, std::vector<std::uint8_t>> out;
  if (member_paths.empty()) return out;

  // Drop unsafe requests; keep original strings as result keys.
  std::vector<std::string> wanted;
  wanted.reserve(member_paths.size());
  for (const auto& m : member_paths) {
    if (!is_unsafe_archive_member_path(m)) wanted.push_back(m);
  }
  if (wanted.empty()) return out;

  struct archive* a = archive_read_new();
  if (!a) return out;
  archive_read_support_filter_all(a);
  archive_read_support_format_all(a);

  if (archive_read_open_filename(a, archive_path.string().c_str(), 10240) !=
      ARCHIVE_OK) {
    archive_read_free(a);
    return out;
  }

  struct archive_entry* entry = nullptr;
  while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
    if (out.size() >= wanted.size()) {
      archive_read_data_skip(a);
      continue;
    }
    const char* path = archive_entry_pathname(entry);
    if (!path) {
      archive_read_data_skip(a);
      continue;
    }

    const std::string* key = nullptr;
    for (const auto& m : wanted) {
      if (out.count(m)) continue;
      if (member_paths_equal(path, m)) {
        key = &m;
        break;
      }
    }
    if (!key) {
      archive_read_data_skip(a);
      continue;
    }

    auto buf = read_entry_bytes(a, entry);
    if (buf) {
      global_build_stats().archive_bytes.fetch_add(
          buf->size(), std::memory_order_relaxed);
      out.emplace(*key, std::move(*buf));
    }
  }

  archive_read_free(a);
  return out;
}

std::optional<std::vector<std::uint8_t>> extract_archive_member(
    const std::filesystem::path& archive_path, std::string_view member_path) {
  auto map = extract_archive_members(archive_path,
                                     {std::string(member_path)});
  auto it = map.find(std::string(member_path));
  if (it == map.end()) return std::nullopt;
  return std::move(it->second);
}


bool is_likely_archive_path(const std::filesystem::path& path) {
  return is_archive_path(path);
}

bool is_likely_image_member_path(std::string_view member_path) {
  if (member_path.empty() || is_unsafe_archive_member_path(member_path))
    return false;
  return is_image_extension(member_extension_lower(member_path));
}

std::optional<std::size_t> archive_member_toc_index(
    const std::vector<std::string>& ordered_members,
    std::string_view member_path) {
  for (std::size_t i = 0; i < ordered_members.size(); ++i) {
    if (member_paths_equal(ordered_members[i], member_path)) return i;
  }
  return std::nullopt;
}

std::vector<std::string> plan_archive_batch_window(
    const std::vector<std::string>& ordered_members,
    const std::vector<std::string>& interest,
    std::size_t next_index,
    int max_window) {
  std::vector<std::string> out;
  if (ordered_members.empty() || max_window <= 0) return out;

  const std::size_t n = ordered_members.size();
  const std::size_t cap = static_cast<std::size_t>(max_window);

  // Map interest → TOC indices (unique, sorted).
  std::vector<std::size_t> idxs;
  idxs.reserve(interest.size());
  for (const auto& m : interest) {
    if (auto i = archive_member_toc_index(ordered_members, m)) {
      idxs.push_back(*i);
    }
  }
  std::sort(idxs.begin(), idxs.end());
  idxs.erase(std::unique(idxs.begin(), idxs.end()), idxs.end());

  if (idxs.empty()) {
    // No interest: continue from cursor for up to cap members.
    std::size_t start = next_index < n ? next_index : 0;
    for (std::size_t i = start; i < n && out.size() < cap; ++i) {
      out.push_back(ordered_members[i]);
    }
    return out;
  }

  // Prefer a contiguous TOC span covering interest near the cursor.
  // If interest spans more than max_window, take a window of size cap
  // starting at the interest index closest to next_index (or min interest).
  const std::size_t lo = idxs.front();
  const std::size_t hi = idxs.back();  // inclusive
  const std::size_t span = hi - lo + 1;

  std::size_t win_lo = lo;
  if (span > cap) {
    // Anchor near cursor when possible.
    std::size_t anchor = lo;
    if (next_index >= lo && next_index <= hi) {
      anchor = next_index;
    } else {
      // Closest interest index to next_index.
      std::size_t best = idxs.front();
      std::size_t best_dist = (best > next_index) ? (best - next_index)
                                                  : (next_index - best);
      for (std::size_t ix : idxs) {
        const std::size_t d =
            (ix > next_index) ? (ix - next_index) : (next_index - ix);
        if (d < best_dist) {
          best_dist = d;
          best = ix;
        }
      }
      anchor = best;
    }
    // Window of size cap containing anchor, clamped to [lo, hi].
    if (anchor + 1 >= cap) {
      win_lo = anchor + 1 - cap;
    } else {
      win_lo = 0;
    }
    if (win_lo < lo) win_lo = lo;
    if (win_lo + cap - 1 > hi) {
      if (hi + 1 >= cap) win_lo = hi + 1 - cap;
      else win_lo = 0;
      if (win_lo < lo) win_lo = lo;
    }
  }

  const std::size_t win_hi = std::min(hi, win_lo + cap - 1);
  // Prefer continuing forward from next_index when it falls inside the window:
  // emit from max(win_lo, next_index) first, then the remainder before it so
  // libarchive still sees a single forward pass if we restart from win_lo.
  // For extract_archive_members (full restart each open), TOC order from
  // win_lo is correct — one sequential scan covers the window.
  for (std::size_t i = win_lo; i <= win_hi && out.size() < cap; ++i) {
    out.push_back(ordered_members[i]);
  }
  return out;
}

}  // namespace thumtoo
