// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/client.hpp"
#include "thumtoo/constants.hpp"
#include "thumtoo/uri.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {
int g_failures = 0;
void expect(bool c, const char* m) {
  if (!c) {
    std::cerr << "FAIL: " << m << "\n";
    ++g_failures;
  }
}
}  // namespace

int main() {
  using namespace thumtoo;
  const auto root =
      fs::temp_directory_path() / "thumtoo-client-test" /
      std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count());
  fs::create_directories(root);

  const auto img = root / "pixel.jpg";
  {
    std::ofstream out(img, std::ios::binary);
    out << "not-a-real-jpeg";
  }

  const auto uri = file_uri_from_path(img);
  expect(uri.starts_with("file://"), "file uri prefix");
  expect(path_from_file_uri(uri).has_value(), "roundtrip path");

  {
    auto client = Client::open(root / "cache");
    expect(!client->get_size(uri).has_value(), "no size before prepare");

    bool called = false;
    client->request_size(uri, [&](std::string, std::optional<Size>) {
      called = true;
    });
    client->drain();
    expect(called, "callback invoked");

    auto meta = client->get_meta(uri);
    expect(meta.has_value(), "meta present");
    expect(meta && meta->status == ContentStatus::Incomplete, "incomplete");
    expect(meta && meta->error_code &&
               *meta->error_code == "probe_not_implemented",
           "probe stub code");
  }

  std::error_code ec;
  fs::remove_all(root, ec);
  if (g_failures) {
    std::cerr << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "ok\n";
  return 0;
}
