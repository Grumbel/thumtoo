// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/client.hpp"
#include "thumtoo/image.hpp"
#include "thumtoo/uri.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::filesystem::path default_cache_root() {
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
    return std::filesystem::path(xdg) / "thumtoo";
  }
  if (const char* home = std::getenv("HOME"); home && *home) {
    return std::filesystem::path(home) / ".cache" / "thumtoo";
  }
  return std::filesystem::path(".cache") / "thumtoo";
}

void usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0 << " [--cache DIR] PATH [PATH...]\n"
      << "  Register paths in the thumtoo cache and schedule size probes.\n"
      << "  Dimension/ladder encode is not implemented yet (marks incomplete).\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path cache = default_cache_root();
  std::vector<std::filesystem::path> paths;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      usage(argv[0]);
      return 0;
    }
    if (a == "--cache" && i + 1 < argc) {
      cache = argv[++i];
      continue;
    }
    paths.emplace_back(a);
  }

  if (paths.empty()) {
    usage(argv[0]);
    return 2;
  }

  try {
    thumtoo::image_library_init();
    auto client = thumtoo::Client::open(cache);
    client->prepare_paths(paths);
    client->drain();
    std::cout << "registered " << paths.size() << " path(s) under " << cache
              << "\n"
              << "content=" << client->db().count_content()
              << " locators=" << client->db().count_locators() << "\n";
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
