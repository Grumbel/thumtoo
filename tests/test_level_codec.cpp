// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/// Level and tile codec: JPEG for opaque images, WebP when there is alpha;
/// an encoded level downscales again without the source, and TileSynth
/// keeps the alpha of WebP tiles.

#include "thumtoo/client.hpp"
#include "thumtoo/constants.hpp"
#include "thumtoo/image.hpp"
#include "thumtoo/uri.hpp"

#include <vips/vips.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    ++g_failures;
  }
}

/// A 300×200 PNG with `bands` bands (3 = RGB, 4 = RGBA).
std::vector<std::uint8_t> make_png(int bands) {
  std::vector<std::uint8_t> px(300u * 200u * static_cast<unsigned>(bands));
  for (std::size_t i = 0; i < px.size(); ++i) {
    px[i] = static_cast<std::uint8_t>(i * 7);
  }
  VipsImage* img = vips_image_new_from_memory(px.data(), px.size(), 300, 200,
                                              bands, VIPS_FORMAT_UCHAR);
  std::vector<std::uint8_t> out;
  if (!img) return out;
  img->Type = VIPS_INTERPRETATION_sRGB;
  void* buf = nullptr;
  size_t len = 0;
  if (vips_pngsave_buffer(img, &buf, &len, nullptr) == 0 && buf) {
    auto* b = static_cast<std::uint8_t*>(buf);
    out.assign(b, b + len);
    g_free(buf);
  }
  g_object_unref(img);
  return out;
}

bool has_magic(const std::vector<std::uint8_t>& b, bool webp) {
  if (webp) {
    return b.size() > 12 && std::equal(b.begin(), b.begin() + 4, "RIFF") &&
           std::equal(b.begin() + 8, b.begin() + 12, "WEBP");
  }
  return b.size() > 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF;
}

}  // namespace

int main() {
  thumtoo::image_library_init();
  const std::string cid = "sha256:00112233445566778899aabbccddeeff";

  const auto rgb = make_png(3);
  const auto rgba = make_png(4);
  expect(!rgb.empty() && !rgba.empty(), "test PNGs");

  auto opaque = thumtoo::build_ladder_buffer(rgb.data(), rgb.size(), cid, 80);
  expect(opaque.size() == 1, "one level (rgb)");
  if (!opaque.empty()) {
    expect(opaque[0].codec == "jpeg", "rgb → jpeg");
    expect(has_magic(opaque[0].bytes, false), "jpeg bytes");
    expect(opaque[0].relative_path.ends_with(".jpg"), "jpg path");
    expect(std::max(opaque[0].width, opaque[0].height) <= 300, "edge fits source");
  }

  auto alpha = thumtoo::build_ladder_buffer(rgba.data(), rgba.size(), cid, 80);
  expect(alpha.size() == 1, "one level (rgba)");
  if (!alpha.empty()) {
    expect(alpha[0].codec == "webp", "rgba → webp");
    expect(has_magic(alpha[0].bytes, true), "webp bytes");
    expect(alpha[0].relative_path.ends_with(".webp"), "webp path");

    // Encoded level → smaller level, alpha kept.
    auto small = thumtoo::downscale_preview_level(
        alpha[0].bytes.data(), alpha[0].bytes.size(), cid, 128, 80);
    expect(small.has_value(), "downscale level");
    if (small) {
      expect(small->codec == "webp", "downscaled keeps alpha");
      expect(std::max(small->width, small->height) <= 128, "downscaled edge");
    }
  }

  // Durable tile cells: WebP with alpha, JPEG without; host rgb888 decode
  // accepts both.
  for (const bool with_alpha : {false, true}) {
    const auto& png = with_alpha ? rgba : rgb;
    auto tiles = thumtoo::build_tile_pyramid_buffer(png.data(), png.size(), 0, -1);
    expect(!tiles.empty(), "tile pyramid");
    for (const auto& t : tiles) {
      expect(t.codec == (with_alpha ? "webp" : "jpeg"), "tile codec");
      expect(has_magic(t.bytes, with_alpha), "tile bytes");
    }
    if (!tiles.empty()) {
      auto dec = thumtoo::decode_tile_blob_to_rgb888(tiles.front());
      expect(dec && dec->codec == thumtoo::kTileCodecRgb888, "tile → rgb888");
    }
  }

  // Client: pyramid into the Store, TileSynth preview keeps alpha.
  const fs::path root =
      fs::temp_directory_path() / "thumtoo-level-codec-test" /
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  fs::create_directories(root);
  {
    auto client = thumtoo::Client::open(root / "cache", {}, 0, root / "cache");
    expect(static_cast<bool>(client), "client open");
    for (const bool with_alpha : {false, true}) {
      const auto& png = with_alpha ? rgba : rgb;
      const fs::path file = root / (with_alpha ? "alpha.png" : "opaque.png");
      std::ofstream(file, std::ios::binary)
          .write(reinterpret_cast<const char*>(png.data()),
                 static_cast<std::streamsize>(png.size()));
      const std::string uri = thumtoo::file_uri_from_path(file);
      client->request_tile_pyramid(uri, 0, -1);
      client->drain();
      auto px = client->get_pixels_from_tiles(uri, 256);
      expect(px.has_value(), "tile synth");
      if (px) {
        expect(px->codec == (with_alpha ? "webp" : "jpeg"), "tile synth codec");
        expect(std::max(px->width, px->height) <= 256, "tile synth edge");
      }
    }
  }
  fs::remove_all(root);

  if (g_failures) {
    std::cerr << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "level codec: ok\n";
  return 0;
}
