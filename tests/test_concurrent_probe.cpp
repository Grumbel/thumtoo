// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/// Concurrent size probes of distinct files on several workers: every
/// location keeps its own content (Store inserts must not hand one worker
/// the row id another worker just inserted).

#include "thumtoo/client.hpp"
#include "thumtoo/image.hpp"
#include "thumtoo/uri.hpp"

#include <vips/vips.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    ++g_failures;
  }
}

/// A w×h RGB PNG, contents depending on `seed`.
bool write_png(const fs::path& file, int w, int h, int seed) {
  std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 3);
  for (std::size_t i = 0; i < px.size(); ++i) {
    px[i] = static_cast<std::uint8_t>(i * 13 + seed * 31);
  }
  VipsImage* img =
      vips_image_new_from_memory(px.data(), px.size(), w, h, 3, VIPS_FORMAT_UCHAR);
  if (!img) return false;
  img->Type = VIPS_INTERPRETATION_sRGB;
  const bool ok = vips_pngsave(img, file.c_str(), nullptr) == 0;
  g_object_unref(img);
  return ok;
}

}  // namespace

int main() {
  thumtoo::image_library_init();
  constexpr int kFiles = 48;
  const fs::path root =
      fs::temp_directory_path() / "thumtoo-concurrent-probe-test" /
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  fs::create_directories(root);

  std::vector<std::string> uris;
  for (int i = 0; i < kFiles; ++i) {
    const fs::path file = root / ("img" + std::to_string(i) + ".png");
    expect(write_png(file, 40 + i, 30 + i, i), "write " + file.string());
    uris.push_back(thumtoo::file_uri_from_path(file));
  }

  {
    auto client = thumtoo::Client::open(root / "cache", {}, 4, root / "cache");
    expect(static_cast<bool>(client), "client open");
    for (const auto& uri : uris) client->request_size(uri, {});
    client->drain();

    std::set<std::string> ids;
    for (int i = 0; i < kFiles; ++i) {
      auto meta = client->get_meta(uris[i]);
      expect(meta.has_value(), "meta " + uris[i]);
      if (!meta) continue;
      ids.insert(meta->content_id);
      expect(meta->size && meta->size->width == 40 + i &&
                 meta->size->height == 30 + i,
             "own size " + uris[i]);
    }
    expect(ids.size() == static_cast<std::size_t>(kFiles), "distinct content ids");
  }
  fs::remove_all(root);

  if (g_failures) {
    std::cerr << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "concurrent probe: ok\n";
  return 0;
}
