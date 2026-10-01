// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Golden-path archive timings (no thumtoo Client/Store).
// Backend: libarchive (always); libunarr when built with THUMTOO_HAVE_UNARR
// (RAR/CBR including solid RAR4).
//
// Usage:
//   thumtoo-gp-archive [--repeat N] [--backend auto|libarchive|unarr] [--json] ARCHIVE

#include <archive.h>
#include <archive_entry.h>

#include "gp_common.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if defined(THUMTOO_HAVE_UNARR) && THUMTOO_HAVE_UNARR
#include <unarr.h>
#define GP_ARCHIVE_HAVE_UNARR 1
#else
#define GP_ARCHIVE_HAVE_UNARR 0
#endif

namespace {

struct Member {
  std::string path;
  la_int64_t size = 0;
};

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

bool file_is_rar5(const std::filesystem::path& path) {
  std::FILE* f = std::fopen(path.string().c_str(), "rb");
  if (!f) return false;
  unsigned char mag[8] = {};
  const size_t n = std::fread(mag, 1, 8, f);
  std::fclose(f);
  if (n < 7) return false;
  return mag[0] == 'R' && mag[1] == 'a' && mag[2] == 'r' && mag[3] == '!'
         && mag[4] == 0x1a && mag[5] == 0x07 && mag[6] == 0x01;
}

// --- libarchive ----------------------------------------------------------------

std::vector<Member> list_members_la(const std::filesystem::path& path) {
  std::vector<Member> out;
  struct archive* a = archive_read_new();
  archive_read_support_filter_all(a);
  archive_read_support_format_all(a);
  if (archive_read_open_filename(a, path.string().c_str(), 10240) != ARCHIVE_OK) {
    archive_read_free(a);
    return out;
  }
  struct archive_entry* entry = nullptr;
  while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
    const char* p = archive_entry_pathname(entry);
    if (!p || archive_entry_filetype(entry) == AE_IFDIR) {
      archive_read_data_skip(a);
      continue;
    }
    Member m;
    m.path = p;
    m.size = archive_entry_size(entry);
    out.push_back(std::move(m));
    archive_read_data_skip(a);
  }
  archive_read_free(a);
  return out;
}

std::size_t extract_one_la(const std::filesystem::path& path,
                           const std::string& member_path) {
  struct archive* a = archive_read_new();
  archive_read_support_filter_all(a);
  archive_read_support_format_all(a);
  if (archive_read_open_filename(a, path.string().c_str(), 10240) != ARCHIVE_OK) {
    archive_read_free(a);
    return 0;
  }
  struct archive_entry* entry = nullptr;
  std::size_t bytes = 0;
  while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
    const char* p = archive_entry_pathname(entry);
    if (p && member_path == p) {
      char buf[16384];
      for (;;) {
        la_ssize_t n = archive_read_data(a, buf, sizeof(buf));
        if (n <= 0) break;
        bytes += static_cast<std::size_t>(n);
      }
      break;
    }
    archive_read_data_skip(a);
  }
  archive_read_free(a);
  return bytes;
}

std::size_t extract_all_la(const std::filesystem::path& path) {
  struct archive* a = archive_read_new();
  archive_read_support_filter_all(a);
  archive_read_support_format_all(a);
  if (archive_read_open_filename(a, path.string().c_str(), 10240) != ARCHIVE_OK) {
    archive_read_free(a);
    return 0;
  }
  struct archive_entry* entry = nullptr;
  std::size_t bytes = 0;
  char buf[16384];
  while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
    if (archive_entry_filetype(entry) == AE_IFDIR) continue;
    for (;;) {
      la_ssize_t n = archive_read_data(a, buf, sizeof(buf));
      if (n <= 0) break;
      bytes += static_cast<std::size_t>(n);
    }
  }
  archive_read_free(a);
  return bytes;
}

std::size_t extract_scattered_la(const std::filesystem::path& path,
                                 const std::vector<Member>& members, int count) {
  if (members.empty()) return 0;
  std::size_t total = 0;
  const int step = std::max(1, static_cast<int>(members.size()) / count);
  for (int i = 0; i < static_cast<int>(members.size()); i += step) {
    total += extract_one_la(path, members[static_cast<std::size_t>(i)].path);
  }
  return total;
}

// --- unarr (optional) ----------------------------------------------------------

#if GP_ARCHIVE_HAVE_UNARR

struct UnarrHolder {
  ar_stream* stream = nullptr;
  ar_archive* ar = nullptr;
  ~UnarrHolder() {
    if (ar) ar_close_archive(ar);
    if (stream) ar_close(stream);
  }
};

std::unique_ptr<UnarrHolder> open_unarr(const std::filesystem::path& path) {
  if (file_is_rar5(path)) return nullptr;
  auto h = std::make_unique<UnarrHolder>();
  h->stream = ar_open_file(path.string().c_str());
  if (!h->stream) return nullptr;
  h->ar = ar_open_rar_archive(h->stream);
  if (!h->ar) return nullptr;
  return h;
}

std::vector<Member> list_members_unarr(const std::filesystem::path& path) {
  std::vector<Member> out;
  auto h = open_unarr(path);
  if (!h) return out;
  while (ar_parse_entry(h->ar)) {
    const char* name = ar_entry_get_name(h->ar);
    if (!name) continue;
    Member m;
    m.path = name;
    m.size = static_cast<la_int64_t>(ar_entry_get_size(h->ar));
    out.push_back(std::move(m));
  }
  return out;
}

std::size_t extract_one_unarr(const std::filesystem::path& path,
                              const std::string& member_path) {
  auto h = open_unarr(path);
  if (!h) return 0;
  while (ar_parse_entry(h->ar)) {
    const char* name = ar_entry_get_name(h->ar);
    if (!name || member_path != name) continue;
    size_t declared = ar_entry_get_size(h->ar);
    if (declared == 0) return 0;
    std::vector<unsigned char> buf(declared);
    if (!ar_entry_uncompress(h->ar, buf.data(), declared)) return 0;
    return declared;
  }
  return 0;
}

std::size_t extract_all_unarr(const std::filesystem::path& path) {
  auto h = open_unarr(path);
  if (!h) return 0;
  std::size_t bytes = 0;
  while (ar_parse_entry(h->ar)) {
    size_t declared = ar_entry_get_size(h->ar);
    if (declared == 0) continue;
    std::vector<unsigned char> buf(declared);
    if (!ar_entry_uncompress(h->ar, buf.data(), declared)) continue;
    bytes += declared;
  }
  return bytes;
}

std::size_t extract_scattered_unarr(const std::filesystem::path& path,
                                    const std::vector<Member>& members,
                                    int count) {
  if (members.empty()) return 0;
  std::size_t total = 0;
  const int step = std::max(1, static_cast<int>(members.size()) / count);
  for (int i = 0; i < static_cast<int>(members.size()); i += step) {
    total += extract_one_unarr(path, members[static_cast<std::size_t>(i)].path);
  }
  return total;
}

#endif  // GP_ARCHIVE_HAVE_UNARR

enum class Backend { Libarchive, Unarr };

Backend pick_backend(const std::string& flag, const std::filesystem::path& path) {
  if (flag == "libarchive") return Backend::Libarchive;
  if (flag == "unarr") {
#if GP_ARCHIVE_HAVE_UNARR
    return Backend::Unarr;
#else
    std::cerr << "this build has no libunarr; using libarchive\n";
    return Backend::Libarchive;
#endif
  }
  // auto
#if GP_ARCHIVE_HAVE_UNARR
  if (path_looks_rar(path) && !file_is_rar5(path)) return Backend::Unarr;
#endif
  return Backend::Libarchive;
}

const char* backend_name(Backend b) {
  return b == Backend::Unarr ? "unarr" : "libarchive";
}

}  // namespace

int main(int argc, char** argv) {
  int repeats = 5;
  bool json_out = false;
  std::string backend_flag = "auto";
  std::filesystem::path path;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--repeat" && i + 1 < argc) {
      repeats = std::max(1, std::atoi(argv[++i]));
    } else if (a == "--backend" && i + 1 < argc) {
      backend_flag = argv[++i];
    } else if (a == "--json") {
      json_out = true;
    } else if (a == "-h" || a == "--help") {
      std::cerr << "Usage: " << argv[0]
                << " [--repeat N] [--backend auto|libarchive|unarr] [--json] "
                   "ARCHIVE\n"
                << "Golden-path archive TOC / sequential / random member extract.\n"
                << "Built with unarr: "
#if GP_ARCHIVE_HAVE_UNARR
                << "yes"
#else
                << "no"
#endif
                << "\n";
      return 0;
    } else if (a[0] != '-') {
      path = a;
    }
  }
  if (path.empty() || !std::filesystem::is_regular_file(path)) {
    std::cerr << "need ARCHIVE file\n";
    return 2;
  }

  const Backend backend = pick_backend(backend_flag, path);

  std::vector<Member> members;
  gp::Timing toc_s{}, all_s{}, first_s{}, last_s{}, scat_s{};

  if (backend == Backend::Unarr) {
#if GP_ARCHIVE_HAVE_UNARR
    members = list_members_unarr(path);
    if (members.empty()) {
      std::cerr << "unarr: no members (or open failed; RAR5 unsupported): " << path
                << "\n";
      return 1;
    }
    toc_s = gp::time_median(repeats, [&] { (void)list_members_unarr(path); });
    all_s =
        gp::time_median(std::max(1, repeats / 2), [&] { (void)extract_all_unarr(path); });
    first_s = gp::time_median(
        repeats, [&] { (void)extract_one_unarr(path, members.front().path); });
    last_s = gp::time_median(
        repeats, [&] { (void)extract_one_unarr(path, members.back().path); });
    scat_s = gp::time_median(std::max(1, repeats / 2), [&] {
      (void)extract_scattered_unarr(path, members, 10);
    });
#else
    std::cerr << "unarr not compiled in\n";
    return 1;
#endif
  } else {
    members = list_members_la(path);
    if (members.empty()) {
      std::cerr << "libarchive: no members (or open failed): " << path << "\n";
      return 1;
    }
    toc_s = gp::time_median(repeats, [&] { (void)list_members_la(path); });
    all_s =
        gp::time_median(std::max(1, repeats / 2), [&] { (void)extract_all_la(path); });
    first_s = gp::time_median(
        repeats, [&] { (void)extract_one_la(path, members.front().path); });
    last_s = gp::time_median(
        repeats, [&] { (void)extract_one_la(path, members.back().path); });
    scat_s = gp::time_median(std::max(1, repeats / 2), [&] {
      (void)extract_scattered_la(path, members, 10);
    });
  }

  if (!json_out) {
    std::cout << "archive=" << path.filename().string()
              << " backend=" << backend_name(backend)
              << " members=" << members.size() << "\n";
    std::printf("toc_ms=%.3f\n", toc_s.median);
    std::printf("extract_all_ms=%.3f\n", all_s.median);
    std::printf("extract_first_ms=%.3f\n", first_s.median);
    std::printf("extract_last_ms=%.3f\n", last_s.median);
    std::printf("extract_scattered10_ms=%.3f\n", scat_s.median);
  } else {
    std::cout << "{\n  \"schema\": 1,\n  \"tool\": \"thumtoo-gp-archive\",\n"
              << "  \"archive\": ";
    gp::json_string(std::cout, path.string());
    std::cout << ",\n  \"backend\": \"" << backend_name(backend) << "\",\n"
              << "  \"unarr_built\": " << (GP_ARCHIVE_HAVE_UNARR ? "true" : "false")
              << ",\n  \"members\": " << members.size()
              << ",\n  \"metrics\": {\n"
              << "    \"toc_ms\": " << toc_s.median << ",\n"
              << "    \"extract_all_ms\": " << all_s.median << ",\n"
              << "    \"extract_first_ms\": " << first_s.median << ",\n"
              << "    \"extract_last_ms\": " << last_s.median << ",\n"
              << "    \"extract_scattered10_ms\": " << scat_s.median << "\n"
              << "  }\n}\n";
  }
  return 0;
}
