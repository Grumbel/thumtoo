// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Golden-path archive timings (libarchive only — no thumtoo Client/Store).
// TOC, sequential extract-all, first/last member, scattered members.
//
// Usage:
//   thumtoo-gp-archive [--repeat N] [--json] ARCHIVE.zip|.cbz|...

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

using clock_type = std::chrono::steady_clock;

double ms_since(clock_type::time_point t0) {
  return std::chrono::duration<double, std::milli>(clock_type::now() - t0)
      .count();
}

struct Stats {
  double median = 0;
};

Stats run_median(int repeats, const std::function<void()>& fn) {
  std::vector<double> times;
  times.reserve(static_cast<std::size_t>(repeats));
  fn();
  for (int i = 0; i < repeats; ++i) {
    const auto t0 = clock_type::now();
    fn();
    times.push_back(ms_since(t0));
  }
  std::sort(times.begin(), times.end());
  return Stats{times[times.size() / 2]};
}

struct Member {
  std::string path;
  la_int64_t size = 0;
};

std::vector<Member> list_members(const std::filesystem::path& path) {
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

std::size_t extract_one(const std::filesystem::path& path,
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

std::size_t extract_all(const std::filesystem::path& path) {
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

std::size_t extract_scattered(const std::filesystem::path& path,
                              const std::vector<Member>& members, int count) {
  if (members.empty()) return 0;
  std::size_t total = 0;
  const int step = std::max(1, static_cast<int>(members.size()) / count);
  for (int i = 0; i < static_cast<int>(members.size()); i += step) {
    total += extract_one(path, members[static_cast<std::size_t>(i)].path);
  }
  return total;
}

void json_escape(std::ostream& os, const std::string& s) {
  os << '"';
  for (char c : s) {
    if (c == '"' || c == '\\') os << '\\';
    if (c == '\n') {
      os << "\\n";
      continue;
    }
    os << c;
  }
  os << '"';
}

}  // namespace

int main(int argc, char** argv) {
  int repeats = 5;
  bool json_out = false;
  std::filesystem::path path;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--repeat" && i + 1 < argc) {
      repeats = std::max(1, std::atoi(argv[++i]));
    } else if (a == "--json") {
      json_out = true;
    } else if (a == "-h" || a == "--help") {
      std::cerr << "Usage: " << argv[0]
                << " [--repeat N] [--json] ARCHIVE\n"
                << "Golden-path libarchive TOC / sequential / random member extract.\n";
      return 0;
    } else if (a[0] != '-') {
      path = a;
    }
  }
  if (path.empty() || !std::filesystem::is_regular_file(path)) {
    std::cerr << "need ARCHIVE file\n";
    return 2;
  }

  auto members = list_members(path);
  if (members.empty()) {
    std::cerr << "no members (or open failed): " << path << "\n";
    return 1;
  }

  auto toc_s = run_median(repeats, [&] { (void)list_members(path); });
  auto all_s =
      run_median(std::max(1, repeats / 2), [&] { (void)extract_all(path); });
  auto first_s =
      run_median(repeats, [&] { (void)extract_one(path, members.front().path); });
  auto last_s =
      run_median(repeats, [&] { (void)extract_one(path, members.back().path); });
  auto scat_s = run_median(std::max(1, repeats / 2), [&] {
    (void)extract_scattered(path, members, 10);
  });

  if (!json_out) {
    std::cout << "archive=" << path.filename().string()
              << " members=" << members.size() << "\n";
    std::printf("toc_ms=%.3f\n", toc_s.median);
    std::printf("extract_all_ms=%.3f\n", all_s.median);
    std::printf("extract_first_ms=%.3f\n", first_s.median);
    std::printf("extract_last_ms=%.3f\n", last_s.median);
    std::printf("extract_scattered10_ms=%.3f\n", scat_s.median);
  } else {
    std::cout << "{\n  \"schema\": 1,\n  \"tool\": \"thumtoo-gp-archive\",\n"
              << "  \"archive\": ";
    json_escape(std::cout, path.string());
    std::cout << ",\n  \"members\": " << members.size()
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
