// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Golden-path tile encode/decode matrix (vips only — no thumtoo Client/Store).
// Splits an image into 256² cells at scale 0 and measures encode/decode (jpeg|webp|avif|jxl)
// at several quality settings (extendable to other codecs later).
//
// Usage:
//   thumtoo-gp-tile [--tile N] [--repeat R] [--json] [--quality 60,80,90] FILE

#include <vips/vips.h>

#include "gp_common.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void ensure_vips() {
  static bool once = false;
  if (!once) {
    if (VIPS_INIT("gp_tile")) {
      std::cerr << "VIPS_INIT failed\n";
      std::exit(1);
    }
    once = true;
  }
}

std::vector<int> parse_qualities(const std::string& s) {
  std::vector<int> out;
  std::stringstream ss(s);
  std::string part;
  while (std::getline(ss, part, ',')) {
    if (part.empty()) continue;
    int q = std::atoi(part.c_str());
    if (q < 1) q = 1;
    if (q > 100) q = 100;
    out.push_back(q);
  }
  if (out.empty()) out = {60, 80, 90};
  return out;
}

struct Cell {
  int x = 0, y = 0, w = 0, h = 0;
};

std::vector<Cell> grid_cells(int img_w, int img_h, int tile) {
  std::vector<Cell> cells;
  const int nx = (img_w + tile - 1) / tile;
  const int ny = (img_h + tile - 1) / tile;
  cells.reserve(static_cast<std::size_t>(nx * ny));
  for (int ty = 0; ty < ny; ++ty) {
    for (int tx = 0; tx < nx; ++tx) {
      Cell c;
      c.x = tx * tile;
      c.y = ty * tile;
      c.w = std::min(tile, img_w - c.x);
      c.h = std::min(tile, img_h - c.y);
      cells.push_back(c);
    }
  }
  return cells;
}

}  // namespace

int main(int argc, char** argv) {
  int tile = 256;
  int repeats = 3;
  bool json_out = false;
  std::string quality_list = "60,80,90";
  std::string codec = "jpeg";  // jpeg | webp | avif | jxl
  std::filesystem::path file;
  // Cap cells timed for large images (full-grid encode still counted for bytes).
  int max_time_cells = 16;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--tile" && i + 1 < argc) {
      tile = std::atoi(argv[++i]);
      if (tile < 16) tile = 16;
    } else if (a == "--repeat" && i + 1 < argc) {
      repeats = std::atoi(argv[++i]);
      if (repeats < 1) repeats = 1;
    } else if (a == "--codec" && i + 1 < argc) {
      codec = argv[++i];
    } else if (a == "--quality" && i + 1 < argc) {
      quality_list = argv[++i];
    } else if (a == "--max-cells" && i + 1 < argc) {
      max_time_cells = std::atoi(argv[++i]);
      if (max_time_cells < 1) max_time_cells = 1;
    } else if (a == "--json") {
      json_out = true;
    } else if (a == "-h" || a == "--help") {
      std::cerr
          << "Usage: " << argv[0]
          << " [--tile N] [--repeat R] [--codec jpeg|webp|avif|jxl] [--quality 60,80,90] "
             "[--max-cells K] [--json] FILE\n"
          << "Golden-path tile encode/decode matrix (jpeg/webp/avif/jxl) "
             "(vips only).\n";
      return 0;
    } else if (a[0] == '-') {
      std::cerr << "unknown option: " << a << "\n";
      return 2;
    } else {
      file = a;
    }
  }
  if (file.empty()) {
    std::cerr << "need FILE\n";
    return 2;
  }
  if (codec != "jpeg" && codec != "webp" && codec != "avif" && codec != "jxl") {
    std::cerr << "unknown --codec (want jpeg|webp|avif|jxl): " << codec << "\n";
    return 2;
  }

  ensure_vips();

  VipsImage* src = vips_image_new_from_file(file.string().c_str(), nullptr);
  if (!src) {
    std::cerr << "open failed: " << file << "\n";
    return 1;
  }
  // Force decode once into memory image for fair extract+encode timing.
  VipsImage* mem = nullptr;
  if (vips_copy(src, &mem, nullptr) != 0 || !mem) {
    g_object_unref(src);
    std::cerr << "copy failed\n";
    return 1;
  }
  g_object_unref(src);
  // Materialize
  {
    size_t len = 0;
    void* buf = vips_image_write_to_memory(mem, &len);
    if (buf) g_free(buf);
  }

  const int img_w = vips_image_get_width(mem);
  const int img_h = vips_image_get_height(mem);
  auto cells = grid_cells(img_w, img_h, tile);
  const int time_n =
      std::min(max_time_cells, static_cast<int>(cells.size()));
  auto qualities = parse_qualities(quality_list);

  if (!json_out) {
    std::cout << "file=" << file.filename().string() << " " << img_w << "x"
              << img_h << " tiles=" << cells.size() << " tile=" << tile
              << " timed_cells=" << time_n << "\n";
    std::cout << "quality,encode_ms_median,decode_ms_median,bytes_total,"
                 "bytes_per_cell_mean\n";
  } else {
    std::cout << "{\n  \"schema\": 1,\n  \"tool\": \"thumtoo-gp-tile\",\n"
              << "  \"file\": ";
    gp::json_string(std::cout, file.string());
    std::cout << ",\n  \"width\": " << img_w << ",\n  \"height\": " << img_h
              << ",\n  \"tile\": " << tile << ",\n  \"cells\": " << cells.size()
              << ",\n  \"timed_cells\": " << time_n
              << ",\n  \"repeats\": " << repeats << ",\n  \"rows\": [\n";
  }

  bool first_row = true;
  for (int q : qualities) {
    // Encode all cells once for byte totals; time a subset.
    std::vector<std::vector<unsigned char>> blobs(cells.size());
    auto encode_one = [&](int idx) {
      const Cell& c = cells[static_cast<std::size_t>(idx)];
      VipsImage* crop = nullptr;
      if (vips_crop(mem, &crop, c.x, c.y, c.w, c.h, nullptr) != 0 || !crop) {
        return;
      }
      void* out_buf = nullptr;
      size_t out_len = 0;
      int save_rc = -1;
      if (codec == "webp") {
        save_rc = vips_webpsave_buffer(crop, &out_buf, &out_len, "Q", q, nullptr);
      } else if (codec == "avif") {
        // AVIF via HEIF saver (AV1). Needs libvips built with libheif.
        save_rc = vips_heifsave_buffer(crop, &out_buf, &out_len, "Q", q,
                                       "compression", VIPS_FOREIGN_HEIF_COMPRESSION_AV1,
                                       nullptr);
      } else if (codec == "jxl") {
        // JPEG XL. Needs libvips built with libjxl.
        save_rc = vips_jxlsave_buffer(crop, &out_buf, &out_len, "Q", q, nullptr);
      } else {
        save_rc = vips_jpegsave_buffer(crop, &out_buf, &out_len, "Q", q, "strip", TRUE,
                                      nullptr);
      }
      if (save_rc == 0 && out_buf) {
        auto& b = blobs[static_cast<std::size_t>(idx)];
        b.assign(static_cast<unsigned char*>(out_buf),
                 static_cast<unsigned char*>(out_buf) + out_len);
        g_free(out_buf);
      }
      g_object_unref(crop);
    };

    // Full grid for bytes
    for (int i = 0; i < static_cast<int>(cells.size()); ++i) encode_one(i);
    std::uint64_t bytes_total = 0;
    for (const auto& b : blobs) bytes_total += b.size();

    auto enc_s = gp::time_median(repeats, [&] {
      for (int i = 0; i < time_n; ++i) encode_one(i);
    });

    auto dec_s = gp::time_median(repeats, [&] {
      for (int i = 0; i < time_n; ++i) {
        const auto& b = blobs[static_cast<std::size_t>(i)];
        if (b.empty()) continue;
        VipsImage* img = nullptr;
        int load_rc = -1;
        if (codec == "webp") {
          load_rc = vips_webpload_buffer(const_cast<unsigned char*>(b.data()), b.size(),
                                         &img, nullptr);
        } else if (codec == "avif") {
          load_rc = vips_heifload_buffer(const_cast<unsigned char*>(b.data()), b.size(),
                                         &img, nullptr);
        } else if (codec == "jxl") {
          load_rc = vips_jxlload_buffer(const_cast<unsigned char*>(b.data()), b.size(),
                                        &img, nullptr);
        } else {
          load_rc = vips_jpegload_buffer(const_cast<unsigned char*>(b.data()), b.size(),
                                         &img, nullptr);
        }
        if (load_rc == 0 && img) {
          size_t len = 0;
          void* buf = vips_image_write_to_memory(img, &len);
          if (buf) g_free(buf);
          g_object_unref(img);
        }
      }
    });

    const double mean_bytes =
        cells.empty() ? 0.0
                      : static_cast<double>(bytes_total) /
                            static_cast<double>(cells.size());

    if (!json_out) {
      std::printf("%d,%.3f,%.3f,%llu,%.1f\n", q, enc_s.median, dec_s.median,
                  static_cast<unsigned long long>(bytes_total), mean_bytes);
    } else {
      if (!first_row) std::cout << ",\n";
      first_row = false;
      std::cout << "    {\"codec\": \"" << codec << "\", \"quality\": " << q
                << ", \"encode_ms\": " << enc_s.median
                << ", \"decode_ms\": " << dec_s.median
                << ", \"bytes_total\": " << bytes_total
                << ", \"bytes_per_cell_mean\": " << mean_bytes << "}";
    }
  }

  if (json_out) std::cout << "\n  ]\n}\n";

  g_object_unref(mem);
  return 0;
}
