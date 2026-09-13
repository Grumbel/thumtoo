// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/debug_overlay.hpp"

#include "thumtoo/constants.hpp"
#include "thumtoo/image.hpp"

#include <vips/vips.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace thumtoo {
namespace {

bool env_flag_on(const char* name) {
  const char* v = std::getenv(name);
  if (!v || !v[0] || v[0] == '0') {
    return false;
  }
  return true;
}

// 5×7 glyphs for 0-9 A-Z a-z space . : × / _ - (minimal debug font).
// Each glyph is 5 columns × 7 rows, bit 0 = top-left progressing left-to-right.
constexpr int kGw = 5;
constexpr int kGh = 7;
constexpr int kGGap = 1;

std::uint8_t glyph_row(char c, int row) {
  // Packed 5-bit rows for printable ASCII subset; missing → box.
  static const std::uint8_t digits[10][7] = {
      {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}, // 0
      {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}, // 1
      {0x0e, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1f}, // 2
      {0x0e, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0e}, // 3
      {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}, // 4
      {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e}, // 5
      {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}, // 6
      {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
      {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}, // 8
      {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c}, // 9
  };
  if (row < 0 || row >= kGh) {
    return 0;
  }
  if (c >= '0' && c <= '9') {
    return digits[c - '0'][row];
  }
  if (c >= 'a' && c <= 'z') {
    c = static_cast<char>(c - 'a' + 'A');
  }
  // Coarse letters / symbols — enough for debug tags.
  switch (c) {
    case ' ':
      return 0;
    case '.':
      return row == 6 ? 0x04 : 0;
    case ':':
      return (row == 2 || row == 4) ? 0x04 : 0;
    case '-':
      return row == 3 ? 0x1f : 0;
    case '_':
      return row == 6 ? 0x1f : 0;
    case '/':
      return (0x01 << (6 - row)) & 0x1f;
    case 'X':
    case 'x':
      return (row == 0 || row == 6)   ? 0x11
             : (row == 1 || row == 5) ? 0x0a
             : (row == 2 || row == 4) ? 0x04
                                     : 0x0a;
    case 'S':
      return digits[5][row];
    case 'R':
      return row == 0   ? 0x1e
             : row == 3 ? 0x1e
             : row < 3  ? 0x11
             : row == 4 ? 0x12
             : row == 5 ? 0x11
                        : 0x11;
    case 'C':
      return (row == 0 || row == 6) ? 0x0e : (row == 1 || row == 5) ? 0x11 : 0x10;
    case 'E':
      return (row == 0 || row == 6) ? 0x1f : (row == 3) ? 0x1e : 0x10;
    case 'J':
      return row < 5 ? 0x02 : (row == 5 ? 0x11 : 0x0e);
    case 'P':
      return row == 0   ? 0x1e
             : row == 3 ? 0x1e
             : row < 3  ? 0x11
                        : 0x10;
    case 'T':
      return row == 0 ? 0x1f : 0x04;
    case 'I':
      return (row == 0 || row == 6) ? 0x0e : 0x04;
    case 'F':
      return row == 0 ? 0x1f : row == 3 ? 0x1e : 0x10;
    case 'U':
      return row == 6 ? 0x0e : 0x11;
    case 'L':
      return row == 6 ? 0x1f : 0x10;
    case 'M':
      return row == 1 ? 0x1b : row == 2 ? 0x15 : 0x11;
    case 'N':
      return row == 1   ? 0x19
             : row == 2 ? 0x15
             : row == 3 ? 0x13
                        : 0x11;
    case 'G':
      return row == 0   ? 0x0e
             : row == 3 ? 0x17
             : row == 6 ? 0x0e
             : row == 1 || row == 2 || row == 4 || row == 5 ? 0x10
                                                           : 0x11;
    case 'A':
      return row == 0 ? 0x0e : row == 3 ? 0x1f : 0x11;
    case 'B':
      return (row == 0 || row == 3 || row == 6) ? 0x1e : 0x11;
    case 'D':
      return (row == 0 || row == 6) ? 0x1e : 0x11;
    case 'H':
      return row == 3 ? 0x1f : 0x11;
    case 'V':
      return row >= 5 ? (row == 5 ? 0x0a : 0x04) : 0x11;
    case 'W':
      return row == 4 ? 0x15 : row == 5 ? 0x1b : 0x11;
    case 'Y':
      return row < 3 ? 0x11 : row == 3 ? 0x0a : 0x04;
    case 'Z':
      return row == 0 || row == 6 ? 0x1f : (0x01 << row);
    case 'K':
      return row == 3 ? 0x18 : row < 3 ? (0x10 | (0x04 >> row)) : (0x10 | (0x01 << (row - 3)));
    case 'O':
      return digits[0][row];
    case 'Q':
      return row == 5 ? 0x13 : row == 6 ? 0x0d : digits[0][row];
    default:
      return (row == 0 || row == 6) ? 0x1f : 0x11;
  }
}

void put_px(std::uint8_t* rgb, int w, int h, int x, int y, std::uint8_t r,
            std::uint8_t g, std::uint8_t b) {
  if (x < 0 || y < 0 || x >= w || y >= h) {
    return;
  }
  std::uint8_t* p = rgb + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                           static_cast<std::size_t>(x)) *
                              3u;
  p[0] = r;
  p[1] = g;
  p[2] = b;
}

void draw_char(std::uint8_t* rgb, int w, int h, int x0, int y0, char c,
               int scale) {
  for (int row = 0; row < kGh; ++row) {
    const std::uint8_t bits = glyph_row(c, row);
    for (int col = 0; col < kGw; ++col) {
      if (!(bits & (0x10 >> col))) {
        continue;
      }
      for (int dy = 0; dy < scale; ++dy) {
        for (int dx = 0; dx < scale; ++dx) {
          const int x = x0 + col * scale + dx;
          const int y = y0 + row * scale + dy;
          // Black outline around glyph, white fill.
          put_px(rgb, w, h, x - 1, y, 0, 0, 0);
          put_px(rgb, w, h, x + 1, y, 0, 0, 0);
          put_px(rgb, w, h, x, y - 1, 0, 0, 0);
          put_px(rgb, w, h, x, y + 1, 0, 0, 0);
          put_px(rgb, w, h, x, y, 255, 255, 220);
        }
      }
    }
  }
}

void draw_text_line(std::uint8_t* rgb, int w, int h, int x0, int y0,
                    const std::string& s, int scale) {
  int x = x0;
  for (char c : s) {
    draw_char(rgb, w, h, x, y0, c, scale);
    x += (kGw + kGGap) * scale;
    if (x >= w - 4) {
      break;
    }
  }
}

void draw_debug_border(std::uint8_t* rgb, int w, int h, int thickness) {
  if (!rgb || w < 1 || h < 1 || thickness < 1) {
    return;
  }
  const int t = std::min(thickness, std::min(w, h) / 2);
  // Magenta border — visible on light and dark photos.
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (x < t || y < t || x >= w - t || y >= h - t) {
        put_px(rgb, w, h, x, y, 255, 0, 255);
      }
    }
  }
}

std::string basename_tail(std::string_view uri) {
  std::string s(uri);
  // Keep last path segment; strip query.
  const auto q = s.find('?');
  if (q != std::string::npos) {
    s.resize(q);
  }
  const auto slash = s.find_last_of("/\\");
  if (slash != std::string::npos) {
    s = s.substr(slash + 1);
  }
  if (s.size() > 28) {
    s = s.substr(0, 28);
  }
  return s;
}

bool rgb_from_encoded(const std::vector<std::uint8_t>& bytes, int& w, int& h,
                      std::vector<std::uint8_t>& out_rgb) {
  image_library_init();
  if (bytes.empty()) {
    return false;
  }
  VipsImage* img = vips_image_new_from_buffer(
      bytes.data(), static_cast<size_t>(bytes.size()), "", nullptr);
  // Soft ladder is often JXL — some libvips builds need an explicit loader.
  if (!img && bytes.size() >= 12) {
    (void)vips_jxlload_buffer(const_cast<void*>(static_cast<const void*>(bytes.data())),
                              bytes.size(), &img, nullptr);
  }
  if (!img) {
    std::fprintf(stderr,
                 "thumtoo: DEBUG_OVERLAY decode failed (bytes=%zu)\n",
                 bytes.size());
    return false;
  }
  VipsImage* rgb = nullptr;
  if (vips_colourspace(img, &rgb, VIPS_INTERPRETATION_sRGB, nullptr) != 0 || !rgb) {
    g_object_unref(img);
    return false;
  }
  g_object_unref(img);
  if (vips_image_get_bands(rgb) != 3) {
    VipsImage* flat = nullptr;
    if (vips_extract_band(rgb, &flat, 0, "n", 3, nullptr) != 0 || !flat) {
      g_object_unref(rgb);
      return false;
    }
    g_object_unref(rgb);
    rgb = flat;
  }
  w = vips_image_get_width(rgb);
  h = vips_image_get_height(rgb);
  if (w <= 0 || h <= 0) {
    g_object_unref(rgb);
    return false;
  }
  size_t len = 0;
  void* mem = vips_image_write_to_memory(rgb, &len);
  g_object_unref(rgb);
  if (!mem || len == 0) {
    if (mem) g_free(mem);
    return false;
  }
  out_rgb.assign(static_cast<std::uint8_t*>(mem),
                 static_cast<std::uint8_t*>(mem) + len);
  g_free(mem);
  const std::size_t need =
      static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3u;
  return out_rgb.size() >= need;
}

bool encode_jpeg(const std::uint8_t* rgb, int w, int h,
                 std::vector<std::uint8_t>& out, int quality = 85) {
  image_library_init();
  VipsImage* img = vips_image_new_from_memory(
      rgb, static_cast<size_t>(w) * static_cast<size_t>(h) * 3u, w, h, 3,
      VIPS_FORMAT_UCHAR);
  if (!img) {
    return false;
  }
  void* buf = nullptr;
  size_t len = 0;
  if (vips_jpegsave_buffer(img, &buf, &len, "Q", quality, nullptr) != 0 ||
      !buf) {
    g_object_unref(img);
    return false;
  }
  out.assign(static_cast<std::uint8_t*>(buf),
             static_cast<std::uint8_t*>(buf) + len);
  g_free(buf);
  g_object_unref(img);
  return true;
}

}  // namespace

bool debug_overlay_enabled() {
  return env_flag_on("THUMTOO_DEBUG_OVERLAY") ||
         env_flag_on("BILTOO_DEBUG_OVERLAY");
}

void debug_overlay_rgb888(std::uint8_t* rgb, int width, int height,
                          const std::vector<std::string>& lines) {
  if (!rgb || width < 8 || height < 8) {
    return;
  }
  const int border = std::max(2, std::min(width, height) / 64);
  draw_debug_border(rgb, width, height, border);

  const int scale = std::max(1, std::min(3, std::min(width, height) / 120));
  const int line_h = (kGh + 2) * scale;
  int y = border + 2;
  for (const std::string& line : lines) {
    if (y + line_h >= height - border) {
      break;
    }
    draw_text_line(rgb, width, height, border + 2, y, line, scale);
    y += line_h + scale;
  }
}

void debug_overlay_pixel_level(PixelLevel& px, std::string_view uri_tail,
                               int request_edge) {
  if (!debug_overlay_enabled() || px.bytes.empty()) {
    return;
  }
  static bool once = false;
  if (!once) {
    once = true;
    std::fprintf(stderr,
                 "thumtoo: DEBUG_OVERLAY active (stamping soft levels + tiles)\n");
  }
  int w = 0;
  int h = 0;
  std::vector<std::uint8_t> rgb;
  if (!rgb_from_encoded(px.bytes, w, h, rgb)) {
    return;
  }
  std::vector<std::string> lines;
  lines.push_back(basename_tail(uri_tail));
  lines.push_back("SOFT " + std::to_string(w) + "x" + std::to_string(h));
  lines.push_back("req=" + std::to_string(request_edge) +
                  " max=" + std::to_string(px.max_edge));
  lines.push_back(std::string(to_string(px.source)));
  if (!px.codec.empty()) {
    lines.push_back(px.codec);
  }
  debug_overlay_rgb888(rgb.data(), w, h, lines);

  std::vector<std::uint8_t> out;
  if (!encode_jpeg(rgb.data(), w, h, out, 90)) {
    return;
  }
  px.bytes = std::move(out);
  px.codec = "jpeg";
  px.width = w;
  px.height = h;
  std::fprintf(stderr,
               "thumtoo: DEBUG_OVERLAY soft %dx%d req=%d %s\n", w, h,
               request_edge, std::string(basename_tail(uri_tail)).c_str());
}

void debug_overlay_tile(TileBlob& tile, std::string_view uri_tail) {
  if (!debug_overlay_enabled() || tile.bytes.empty()) {
    return;
  }
  int w = 0;
  int h = 0;
  std::vector<std::uint8_t> rgb;
  if (!rgb_from_encoded(tile.bytes, w, h, rgb)) {
    return;
  }
  std::vector<std::string> lines;
  lines.push_back(basename_tail(uri_tail));
  lines.push_back("TILE " + std::to_string(w) + "x" + std::to_string(h));
  lines.push_back("s=" + std::to_string(tile.scale) + " x=" +
                  std::to_string(tile.x) + " y=" + std::to_string(tile.y));
  lines.push_back(std::string("tsrc=") +
                  std::to_string(static_cast<int>(tile.source)));
  debug_overlay_rgb888(rgb.data(), w, h, lines);

  std::vector<std::uint8_t> out;
  if (!encode_jpeg(rgb.data(), w, h, out, 90)) {
    return;
  }
  tile.bytes = std::move(out);
  tile.codec = "jpeg";
  tile.width = w;
  tile.height = h;
}

}  // namespace thumtoo
