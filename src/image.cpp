// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/image.hpp"
#include "thumtoo/build_stats.hpp"
#include "thumtoo/constants.hpp"

#include <vips/vips.h>

#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <string>
#include <memory>
#include <functional>
#include <chrono>
#include <array>
#include <cctype>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
#include <sstream>
#include <cstring>

namespace thumtoo {
namespace {

std::once_flag g_vips_once;

void ensure_vips() {
  std::call_once(g_vips_once, [] {
    // VIPS_INIT returns 0 on success.
    if (VIPS_INIT("thumtoo") != 0) {
      // Subsequent calls surface errors via NULL returns / vips_error.
    }
    // Let libvips use multiple cores for its own ops (shrink, etc.).
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 1;
    vips_concurrency_set(static_cast<int>(hw));
  });
}

std::string format_from_path(const std::filesystem::path& path) {
  auto ext = path.extension().string();
  for (char& c : ext)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (ext == ".jpg" || ext == ".jpeg") return "jpeg";
  if (ext == ".png") return "png";
  if (ext == ".gif") return "gif";
  if (ext == ".bmp") return "bmp";
  if (ext == ".tif" || ext == ".tiff") return "tiff";
  if (ext == ".webp") return "webp";
  if (ext == ".jxl") return "jxl";
  if (ext == ".heic" || ext == ".heif") return "heif";
  return "unknown";
}

std::string content_id_to_blob_dir(const std::string& content_id) {
  std::string id_path = content_id;
  for (char& c : id_path) {
    if (c == ':' || c == '/') c = '_';
  }
  return id_path;
}

// Minimal SHA-256
class Sha256 {
 public:
  Sha256() { reset(); }
  void update(const std::uint8_t* data, std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
      data_[datalen_++] = data[i];
      if (datalen_ == 64) {
        transform();
        bitlen_ += 512;
        datalen_ = 0;
      }
    }
  }
  std::array<std::uint8_t, 32> final() {
    std::uint32_t i = datalen_;
    if (datalen_ < 56) {
      data_[i++] = 0x80;
      while (i < 56) data_[i++] = 0x00;
    } else {
      data_[i++] = 0x80;
      while (i < 64) data_[i++] = 0x00;
      transform();
      std::fill(data_.begin(), data_.begin() + 56, 0);
    }
    bitlen_ += static_cast<std::uint64_t>(datalen_) * 8;
    data_[63] = static_cast<std::uint8_t>(bitlen_);
    data_[62] = static_cast<std::uint8_t>(bitlen_ >> 8);
    data_[61] = static_cast<std::uint8_t>(bitlen_ >> 16);
    data_[60] = static_cast<std::uint8_t>(bitlen_ >> 24);
    data_[59] = static_cast<std::uint8_t>(bitlen_ >> 32);
    data_[58] = static_cast<std::uint8_t>(bitlen_ >> 40);
    data_[57] = static_cast<std::uint8_t>(bitlen_ >> 48);
    data_[56] = static_cast<std::uint8_t>(bitlen_ >> 56);
    transform();
    std::array<std::uint8_t, 32> hash{};
    for (i = 0; i < 4; ++i) {
      hash[i] = (state_[0] >> (24 - i * 8)) & 0xff;
      hash[i + 4] = (state_[1] >> (24 - i * 8)) & 0xff;
      hash[i + 8] = (state_[2] >> (24 - i * 8)) & 0xff;
      hash[i + 12] = (state_[3] >> (24 - i * 8)) & 0xff;
      hash[i + 16] = (state_[4] >> (24 - i * 8)) & 0xff;
      hash[i + 20] = (state_[5] >> (24 - i * 8)) & 0xff;
      hash[i + 24] = (state_[6] >> (24 - i * 8)) & 0xff;
      hash[i + 28] = (state_[7] >> (24 - i * 8)) & 0xff;
    }
    return hash;
  }

 private:
  void reset() {
    datalen_ = 0;
    bitlen_ = 0;
    state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  }
  static std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
    return (x >> n) | (x << (32 - n));
  }
  void transform() {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::uint32_t m[64];
    for (std::uint32_t i = 0, j = 0; i < 16; ++i, j += 4) {
      m[i] = (static_cast<std::uint32_t>(data_[j]) << 24) |
             (static_cast<std::uint32_t>(data_[j + 1]) << 16) |
             (static_cast<std::uint32_t>(data_[j + 2]) << 8) |
             (static_cast<std::uint32_t>(data_[j + 3]));
    }
    for (std::uint32_t i = 16; i < 64; ++i) {
      const std::uint32_t s0 =
          rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
      const std::uint32_t s1 =
          rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
      m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }
    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (std::uint32_t i = 0; i < 64; ++i) {
      const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const std::uint32_t ch = (e & f) ^ ((~e) & g);
      const std::uint32_t t1 = h + S1 + ch + k[i] + m[i];
      const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t t2 = S0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }
  std::array<std::uint8_t, 64> data_{};
  std::uint32_t datalen_ = 0;
  std::uint64_t bitlen_ = 0;
  std::array<std::uint32_t, 8> state_{};
};

}  // namespace

void image_library_init() { ensure_vips(); }

std::optional<std::vector<std::uint8_t>> read_file_bytes(
    const std::filesystem::path& path, std::uint64_t max_bytes) {
  std::error_code ec;
  const auto sz = std::filesystem::file_size(path, ec);
  if (ec || !std::filesystem::is_regular_file(path, ec)) return std::nullopt;
  if (sz > max_bytes) return std::nullopt;
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
  if (sz > 0) {
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
    if (static_cast<std::uint64_t>(in.gcount()) != sz) return std::nullopt;
  }
  return buf;
}

std::optional<ProbeResult> probe_image_file(const std::filesystem::path& path) {
  ensure_vips();
  // Returns VipsImage* (or NULL) — not an int error code.
  VipsImage* img = vips_image_new_from_file(
      path.string().c_str(), "access", VIPS_ACCESS_SEQUENTIAL, nullptr);
  if (!img) return std::nullopt;
  const int w = vips_image_get_width(img);
  const int h = vips_image_get_height(img);
  g_object_unref(img);
  if (w <= 0 || h <= 0) return std::nullopt;
  return ProbeResult{Size{w, h}, format_from_path(path)};
}

std::optional<ProbeResult> probe_image_buffer(const std::uint8_t* data,
                                             std::size_t size,
                                             std::string_view hint_format) {
  ensure_vips();
  if (!data || size == 0) return std::nullopt;
  VipsImage* img = vips_image_new_from_buffer(
      data, size, nullptr, "access", VIPS_ACCESS_SEQUENTIAL, nullptr);
  if (!img) return std::nullopt;
  const int w = vips_image_get_width(img);
  const int h = vips_image_get_height(img);
  g_object_unref(img);
  if (w <= 0 || h <= 0) return std::nullopt;
  std::string fmt = std::string(hint_format);
  if (fmt.empty()) fmt = "unknown";
  return ProbeResult{Size{w, h}, fmt};
}

namespace {


/// Map Galapix scale to libjpeg/libvips jpegload shrink factor (1,2,4,8).
int jpeg_shrink_factor_for_scale(int scale) {
  if (scale >= 3) return 8;
  if (scale >= 2) return 4;
  if (scale >= 1) return 2;
  return 1;
}

int scale_steps_after_jpeg_shrink(int scale, int jpeg_shrink) {
  int applied = 0;
  if (jpeg_shrink >= 8) applied = 3;
  else if (jpeg_shrink >= 4) applied = 2;
  else if (jpeg_shrink >= 2) applied = 1;
  return std::max(0, scale - applied);
}

bool path_looks_jpeg(const std::filesystem::path& path) {
  auto ext = path.extension().string();
  for (char& c : ext)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext == ".jpg" || ext == ".jpeg" || ext == ".jpe";
}

/// Extract embedded JPEG thumbnail from EXIF APP1 (IFD1), if present.
std::optional<std::vector<std::uint8_t>> extract_exif_jpeg_thumbnail(
    const std::uint8_t* data, std::size_t size) {
  if (!data || size < 4 || data[0] != 0xff || data[1] != 0xd8) return std::nullopt;

  std::size_t i = 2;
  while (i + 4 <= size) {
    if (data[i] != 0xff) {
      ++i;
      continue;
    }
    while (i < size && data[i] == 0xff) ++i;
    if (i >= size) break;
    const std::uint8_t marker = data[i++];
    if (marker == 0xd9 || marker == 0xda) break;  // EOI / SOS
    if (i + 2 > size) break;
    const std::uint16_t seglen =
        (static_cast<std::uint16_t>(data[i]) << 8) | data[i + 1];
    if (seglen < 2 || i + seglen > size) break;
    if (marker == 0xe1 && seglen >= 8) {
      const std::uint8_t* seg = data + i + 2;
      const std::size_t segpayload = static_cast<std::size_t>(seglen - 2);
      if (segpayload >= 6 && std::memcmp(seg, "Exif\0\0", 6) == 0) {
        const std::uint8_t* tiff = seg + 6;
        const std::size_t tiff_len = segpayload - 6;
        if (tiff_len < 8) break;
        const bool le = (tiff[0] == 'I' && tiff[1] == 'I');
        const bool be = (tiff[0] == 'M' && tiff[1] == 'M');
        if (!le && !be) break;
        auto ru16 = [&](std::size_t off) -> std::uint16_t {
          if (off + 2 > tiff_len) return 0;
          return le ? static_cast<std::uint16_t>(tiff[off] |
                                                 (tiff[off + 1] << 8))
                    : static_cast<std::uint16_t>((tiff[off] << 8) |
                                                 tiff[off + 1]);
        };
        auto ru32 = [&](std::size_t off) -> std::uint32_t {
          if (off + 4 > tiff_len) return 0;
          return le ? static_cast<std::uint32_t>(tiff[off]) |
                          (static_cast<std::uint32_t>(tiff[off + 1]) << 8) |
                          (static_cast<std::uint32_t>(tiff[off + 2]) << 16) |
                          (static_cast<std::uint32_t>(tiff[off + 3]) << 24)
                    : (static_cast<std::uint32_t>(tiff[off]) << 24) |
                          (static_cast<std::uint32_t>(tiff[off + 1]) << 16) |
                          (static_cast<std::uint32_t>(tiff[off + 2]) << 8) |
                          static_cast<std::uint32_t>(tiff[off + 3]);
        };
        const std::uint32_t ifd0 = ru32(4);
        if (ifd0 + 2 > tiff_len) break;
        const std::uint16_t n0 = ru16(ifd0);
        const std::size_t ifd1_ptr =
            ifd0 + 2 + static_cast<std::size_t>(n0) * 12;
        if (ifd1_ptr + 4 > tiff_len) break;
        const std::uint32_t ifd1 = ru32(ifd1_ptr);
        if (ifd1 == 0 || ifd1 + 2 > tiff_len) break;
        const std::uint16_t n1 = ru16(ifd1);
        std::uint32_t jpeg_off = 0;
        std::uint32_t jpeg_len = 0;
        for (std::uint16_t e = 0; e < n1; ++e) {
          const std::size_t eo = ifd1 + 2 + static_cast<std::size_t>(e) * 12;
          const std::uint16_t tag = ru16(eo);
          const std::uint32_t val = ru32(eo + 8);
          if (tag == 0x0201) jpeg_off = val;
          if (tag == 0x0202) jpeg_len = val;
        }
        if (jpeg_off && jpeg_len &&
            static_cast<std::size_t>(jpeg_off) + jpeg_len <= tiff_len &&
            jpeg_len >= 4) {
          const std::uint8_t* jp = tiff + jpeg_off;
          if (jp[0] == 0xff && jp[1] == 0xd8) {
            return std::vector<std::uint8_t>(jp, jp + jpeg_len);
          }
        }
      }
    }
    i += seglen;
  }
  return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> extract_exif_jpeg_thumbnail_file(
    const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::vector<std::uint8_t> buf(256 * 1024);
  in.read(reinterpret_cast<char*>(buf.data()),
          static_cast<std::streamsize>(buf.size()));
  const auto n = static_cast<std::size_t>(in.gcount());
  if (n < 4) return std::nullopt;
  buf.resize(n);
  return extract_exif_jpeg_thumbnail(buf.data(), buf.size());
}

/// Largest policy edge ≤ both the request limit and the source long edge.
/// max_edge_limit ≤ 0 → no request cap (still capped by source / kLadderEdges).
int pick_preview_edge(int long_edge, int max_edge_limit) {
  if (long_edge <= 0) return 0;
  int best = 0;
  for (int edge : kLadderEdges) {
    if (edge > long_edge) continue;
    if (max_edge_limit > 0 && edge > max_edge_limit) continue;
    if (edge > best) best = edge;
  }
  // Source smaller than every ladder step: still emit one level keyed as the
  // smallest policy edge so get_pixels(max_edge) can find it.
  if (best == 0) best = kLadderEdges.front();
  return best;
}

LevelBlob encode_jxl_level(VipsImage* thumb, int edge,
                           const std::string& id_dir, int q) {
  LevelBlob b;
  void* buf = nullptr;
  size_t len = 0;
  {
    ScopedNsAccumulator timer(global_build_stats().jxl_encode_ns);
    if (vips_jxlsave_buffer(thumb, &buf, &len, "Q", q, nullptr) != 0 || !buf) {
      return b;
    }
  }
  b.max_edge = edge;
  b.frame_idx = 0;
  b.width = vips_image_get_width(thumb);
  b.height = vips_image_get_height(thumb);
  b.codec = "jxl";
  b.quality = q;
  std::ostringstream rel;
  rel << "blobs/" << id_dir << "/" << edge << "_f0.jxl";
  b.relative_path = rel.str();
  auto* bytes = static_cast<std::uint8_t*>(buf);
  b.bytes.assign(bytes, bytes + len);
  g_free(buf);
  global_build_stats().levels_encoded.fetch_add(1, std::memory_order_relaxed);
  return b;
}

}  // namespace

std::vector<LevelBlob> build_ladder(const std::filesystem::path& path,
                                    const std::string& content_id,
                                    int jxl_quality, int max_edge_limit) {
  ensure_vips();
  std::vector<LevelBlob> levels;

  VipsImage* header = vips_image_new_from_file(
      path.string().c_str(), "access", VIPS_ACCESS_SEQUENTIAL, nullptr);
  if (!header) return levels;
  const int long_edge =
      std::max(vips_image_get_width(header), vips_image_get_height(header));
  g_object_unref(header);
  if (long_edge <= 0) return levels;

  const int edge = pick_preview_edge(long_edge, max_edge_limit);
  const int q = std::clamp(jxl_quality, 1, 100);
  const std::string id_dir = content_id_to_blob_dir(content_id);
  const std::string path_str = path.string();

  // Prefer EXIF embedded JPEG thumbnail when large enough for the edge.
  if (path_looks_jpeg(path)) {
    if (auto exif_jpeg = extract_exif_jpeg_thumbnail_file(path)) {
      VipsImage* emb = nullptr;
      ScopedNsAccumulator timer(global_build_stats().thumb_ns);
      if (vips_jpegload_buffer(exif_jpeg->data(), exif_jpeg->size(), &emb,
                               nullptr) == 0 &&
          emb) {
        const int emb_edge =
            std::max(vips_image_get_width(emb), vips_image_get_height(emb));
        if (emb_edge >= edge || emb_edge >= kLadderEdges.front()) {
          VipsImage* thumb = nullptr;
          if (emb_edge > edge) {
            if (vips_thumbnail_image(emb, &thumb, edge, "size", VIPS_SIZE_DOWN,
                                     nullptr) != 0) {
              thumb = nullptr;
            }
          } else {
            thumb = emb;
            g_object_ref(thumb);
          }
          g_object_unref(emb);
          if (thumb) {
            LevelBlob b = encode_jxl_level(thumb, edge, id_dir, q);
            g_object_unref(thumb);
            if (!b.bytes.empty()) {
              global_build_stats().exif_thumb_hits.fetch_add(
                  1, std::memory_order_relaxed);
              levels.push_back(std::move(b));
              return levels;
            }
          }
        } else {
          g_object_unref(emb);
        }
      }
    }
  }

  VipsImage* thumb = nullptr;
  {
    ScopedNsAccumulator timer(global_build_stats().thumb_ns);
    if (vips_thumbnail(path_str.c_str(), &thumb, edge, "size", VIPS_SIZE_DOWN,
                       nullptr) != 0 ||
        !thumb) {
      return levels;
    }
  }

  LevelBlob b = encode_jxl_level(thumb, edge, id_dir, q);
  g_object_unref(thumb);
  if (!b.bytes.empty()) levels.push_back(std::move(b));
  return levels;
}

std::vector<LevelBlob> build_ladder_buffer(const std::uint8_t* data,
                                           std::size_t size,
                                           const std::string& content_id,
                                           int jxl_quality, int max_edge_limit) {
  ensure_vips();
  std::vector<LevelBlob> levels;
  if (!data || size == 0) return levels;

  VipsImage* header = vips_image_new_from_buffer(
      data, size, nullptr, "access", VIPS_ACCESS_SEQUENTIAL, nullptr);
  if (!header) return levels;
  const int long_edge =
      std::max(vips_image_get_width(header), vips_image_get_height(header));
  g_object_unref(header);
  if (long_edge <= 0) return levels;

  const int edge = pick_preview_edge(long_edge, max_edge_limit);
  const int q = std::clamp(jxl_quality, 1, 100);
  const std::string id_dir = content_id_to_blob_dir(content_id);

  if (auto exif_jpeg = extract_exif_jpeg_thumbnail(data, size)) {
    VipsImage* emb = nullptr;
    ScopedNsAccumulator timer(global_build_stats().thumb_ns);
    if (vips_jpegload_buffer(exif_jpeg->data(), exif_jpeg->size(), &emb,
                             nullptr) == 0 &&
        emb) {
      const int emb_edge =
          std::max(vips_image_get_width(emb), vips_image_get_height(emb));
      if (emb_edge >= edge || emb_edge >= kLadderEdges.front()) {
        VipsImage* thumb = nullptr;
        if (emb_edge > edge) {
          if (vips_thumbnail_image(emb, &thumb, edge, "size", VIPS_SIZE_DOWN,
                                   nullptr) != 0) {
            thumb = nullptr;
          }
        } else {
          thumb = emb;
          g_object_ref(thumb);
        }
        g_object_unref(emb);
        if (thumb) {
          LevelBlob b = encode_jxl_level(thumb, edge, id_dir, q);
          g_object_unref(thumb);
          if (!b.bytes.empty()) {
            global_build_stats().exif_thumb_hits.fetch_add(
                1, std::memory_order_relaxed);
            levels.push_back(std::move(b));
            return levels;
          }
        }
      } else {
        g_object_unref(emb);
      }
    }
  }

  VipsImage* thumb = nullptr;
  {
    ScopedNsAccumulator timer(global_build_stats().thumb_ns);
    if (vips_thumbnail_buffer(const_cast<std::uint8_t*>(data), size, &thumb, edge,
                              "size", VIPS_SIZE_DOWN, nullptr) != 0 ||
        !thumb) {
      return levels;
    }
  }

  LevelBlob b = encode_jxl_level(thumb, edge, id_dir, q);
  g_object_unref(thumb);
  if (!b.bytes.empty()) levels.push_back(std::move(b));
  return levels;
}

std::vector<LevelBlob> build_ladder_rgb(const std::uint8_t* rgb, int width,
                                        int height, const std::string& content_id,
                                        int jxl_quality, int max_edge_limit) {
  ensure_vips();
  std::vector<LevelBlob> levels;
  if (!rgb || width <= 0 || height <= 0) return levels;

  VipsImage* full = vips_image_new_from_memory_copy(
      rgb, static_cast<size_t>(width) * static_cast<size_t>(height) * 3u, width,
      height, 3, VIPS_FORMAT_UCHAR);
  if (!full) return levels;

  const int long_edge = std::max(width, height);
  const int edge = pick_preview_edge(long_edge, max_edge_limit);
  const std::string id_dir = content_id_to_blob_dir(content_id);
  const int q = jxl_quality > 0 ? jxl_quality : kDefaultJxlQuality;

  VipsImage* thumb = nullptr;
  {
    ScopedNsAccumulator timer(global_build_stats().thumb_ns);
    if (vips_thumbnail_image(full, &thumb, edge, "size", VIPS_SIZE_DOWN,
                             nullptr) != 0 ||
        !thumb) {
      g_object_unref(full);
      return levels;
    }
  }
  g_object_unref(full);

  LevelBlob b = encode_jxl_level(thumb, edge, id_dir, q);
  g_object_unref(thumb);
  if (!b.bytes.empty()) levels.push_back(std::move(b));
  return levels;
}


std::optional<LevelBlob> downscale_preview_jxl(const std::uint8_t* jxl_data,
                                               std::size_t jxl_size,
                                               const std::string& content_id,
                                               int target_edge,
                                               int jxl_quality) {
  ensure_vips();
  if (!jxl_data || jxl_size == 0 || target_edge <= 0) return std::nullopt;

  VipsImage* full = nullptr;
  {
    ScopedNsAccumulator timer(global_build_stats().thumb_ns);
    // libvips JXL loader from memory buffer
    if (vips_jxlload_buffer(const_cast<void*>(static_cast<const void*>(jxl_data)),
                            jxl_size, &full, nullptr) != 0 ||
        !full) {
      return std::nullopt;
    }
  }
  const int long_edge =
      std::max(vips_image_get_width(full), vips_image_get_height(full));
  const int edge = pick_preview_edge(long_edge, target_edge);
  const int q = std::clamp(jxl_quality, 1, 100);
  const std::string id_dir = content_id_to_blob_dir(content_id);

  VipsImage* thumb = nullptr;
  {
    ScopedNsAccumulator timer(global_build_stats().thumb_ns);
    if (vips_thumbnail_image(full, &thumb, edge, "size", VIPS_SIZE_DOWN,
                             nullptr) != 0 ||
        !thumb) {
      g_object_unref(full);
      return std::nullopt;
    }
  }
  g_object_unref(full);

  LevelBlob b = encode_jxl_level(thumb, edge, id_dir, q);
  g_object_unref(thumb);
  if (b.bytes.empty()) return std::nullopt;
  return b;
}

std::string sha256_file_hex(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  Sha256 ctx;
  std::array<char, 8192> buf{};
  while (in) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    const auto n = in.gcount();
    if (n > 0) {
      ctx.update(reinterpret_cast<const std::uint8_t*>(buf.data()),
                 static_cast<std::size_t>(n));
    }
  }
  const auto hash = ctx.final();
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.resize(64);
  for (int i = 0; i < 32; ++i) {
    out[static_cast<std::size_t>(i) * 2] = kHex[hash[i] >> 4];
    out[static_cast<std::size_t>(i) * 2 + 1] = kHex[hash[i] & 0xf];
  }
  return out;
}

std::string sha256_bytes_hex(const std::uint8_t* data, std::size_t size) {
  if (!data || size == 0) return {};
  Sha256 ctx;
  ctx.update(data, size);
  const auto hash = ctx.final();
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.resize(64);
  for (int i = 0; i < 32; ++i) {
    out[static_cast<std::size_t>(i) * 2] = kHex[hash[i] >> 4];
    out[static_cast<std::size_t>(i) * 2 + 1] = kHex[hash[i] & 0xf];
  }
  return out;
}


namespace {

std::vector<TileBlob> cut_pyramid_from_vips(VipsImage* full, int min_scale,
                                            int max_scale, int jpeg_quality) {
  std::vector<TileBlob> tiles;
  if (!full) return tiles;

  const int src_w = vips_image_get_width(full);
  const int src_h = vips_image_get_height(full);
  if (src_w <= 0 || src_h <= 0) return tiles;

  // Memory guard: do not build pyramids for extremely large rasters.
  const std::int64_t pixels =
      static_cast<std::int64_t>(src_w) * static_cast<std::int64_t>(src_h);
  if (pixels > kTileMaxSourcePixels) return tiles;

  const int q = std::clamp(jpeg_quality, 1, 100);

  // max_scale: single-tile coverage if not specified
  int computed_max = 0;
  {
    int w = src_w;
    int h = src_h;
    while (w > kTileSize || h > kTileSize) {
      w = (w + 1) / 2;
      h = (h + 1) / 2;
      ++computed_max;
    }
  }
  if (max_scale < 0) max_scale = computed_max;
  if (min_scale < 0) min_scale = 0;
  if (min_scale > max_scale) return tiles;

  unsigned hw = std::thread::hardware_concurrency();
  if (hw == 0) hw = 1;

  VipsImage* current = full;
  g_object_ref(current);

  for (int scale = 0; scale <= max_scale; ++scale) {
    if (scale > 0) {
      VipsImage* halved = nullptr;
      {
        ScopedNsAccumulator timer(global_build_stats().shrink_ns);
        // Integer factor-2 shrink (Galapix-style halve).
        if (vips_shrink(current, &halved, 2.0, 2.0, nullptr) != 0 || !halved) {
          break;
        }
      }
      g_object_unref(current);
      current = halved;
    }

    if (scale < min_scale) continue;

    const int sw = vips_image_get_width(current);
    const int sh = vips_image_get_height(current);
    const int tiles_x = (sw + kTileSize - 1) / kTileSize;
    const int tiles_y = (sh + kTileSize - 1) / kTileSize;

    struct Cell {
      int tx = 0;
      int ty = 0;
      int tw = 0;
      int th = 0;
    };
    std::vector<Cell> cells;
    cells.reserve(static_cast<std::size_t>(tiles_x * tiles_y));
    for (int ty = 0; ty < tiles_y; ++ty) {
      for (int tx = 0; tx < tiles_x; ++tx) {
        const int left = tx * kTileSize;
        const int top = ty * kTileSize;
        const int tw = std::min(kTileSize, sw - left);
        const int th = std::min(kTileSize, sh - top);
        if (tw <= 0 || th <= 0) continue;
        cells.push_back(Cell{tx, ty, tw, th});
      }
    }

    std::vector<TileBlob> scale_tiles(cells.size());
    std::atomic<std::size_t> next{0};
    auto encode_worker = [&]() {
      for (;;) {
        const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
        if (i >= cells.size()) break;
        const Cell& c = cells[i];
        const int left = c.tx * kTileSize;
        const int top = c.ty * kTileSize;
        VipsImage* crop = nullptr;
        if (vips_crop(current, &crop, left, top, c.tw, c.th, nullptr) != 0 ||
            !crop) {
          continue;
        }
        void* buf = nullptr;
        size_t len = 0;
        {
          ScopedNsAccumulator timer(global_build_stats().jpeg_encode_ns);
          if (vips_jpegsave_buffer(crop, &buf, &len, "Q", q, nullptr) != 0 ||
              !buf) {
            g_object_unref(crop);
            continue;
          }
        }
        g_object_unref(crop);
        TileBlob tb;
        tb.scale = scale;
        tb.x = c.tx;
        tb.y = c.ty;
        tb.width = c.tw;
        tb.height = c.th;
        tb.codec = kDefaultTileCodec;
        tb.bytes.assign(static_cast<std::uint8_t*>(buf),
                        static_cast<std::uint8_t*>(buf) + len);
        g_free(buf);
        scale_tiles[i] = std::move(tb);
        global_build_stats().tiles_encoded.fetch_add(1, std::memory_order_relaxed);
      }
    };

    const unsigned n_workers =
        std::min(hw, std::max<unsigned>(1, static_cast<unsigned>(cells.size())));
    if (n_workers <= 1) {
      encode_worker();
    } else {
      std::vector<std::thread> pool;
      pool.reserve(n_workers);
      for (unsigned w = 0; w < n_workers; ++w) {
        pool.emplace_back(encode_worker);
      }
      for (auto& th : pool) th.join();
    }

    for (auto& tb : scale_tiles) {
      if (!tb.bytes.empty()) tiles.push_back(std::move(tb));
    }
  }

  g_object_unref(current);
  return tiles;
}


// Interactive single-cell path: cache a shrink ladder so concurrent
// build_tile_cell* calls share one decode + successive vips_shrink levels.
constexpr std::size_t kLadderCacheMaxEntries = 4;

struct ShrinkLadder {
  std::mutex mu;
  std::vector<VipsImage*> levels;  // levels[s] at Galapix scale s
  std::chrono::steady_clock::time_point last_used{};
};

std::mutex g_ladder_mu;
std::unordered_map<std::string, std::shared_ptr<ShrinkLadder>> g_ladders;

std::int64_t file_mtime_ns_local(const std::filesystem::path& path) {
  std::error_code ec;
  auto ft = std::filesystem::last_write_time(path, ec);
  if (ec) return 0;
  return static_cast<std::int64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(ft.time_since_epoch())
          .count());
}

std::string ladder_key_for_file(const std::filesystem::path& path) {
  return "f:" + path.string() + ":" + std::to_string(file_mtime_ns_local(path));
}

void ladder_entry_clear(ShrinkLadder& e) {
  for (VipsImage* img : e.levels) {
    if (img) g_object_unref(img);
  }
  e.levels.clear();
}

void ladder_cache_evict_unlocked(const std::string& keep_key) {
  while (g_ladders.size() >= kLadderCacheMaxEntries) {
    std::string victim;
    auto oldest = std::chrono::steady_clock::time_point::max();
    for (auto& kv : g_ladders) {
      if (kv.first == keep_key) continue;
      if (kv.second->last_used <= oldest) {
        oldest = kv.second->last_used;
        victim = kv.first;
      }
    }
    if (victim.empty()) break;
    auto it = g_ladders.find(victim);
    if (it == g_ladders.end()) break;
    {
      std::lock_guard lock(it->second->mu);
      ladder_entry_clear(*it->second);
    }
    g_ladders.erase(it);
  }
}

std::shared_ptr<ShrinkLadder> ladder_get_or_create(const std::string& key) {
  std::lock_guard lock(g_ladder_mu);
  auto it = g_ladders.find(key);
  if (it != g_ladders.end()) {
    it->second->last_used = std::chrono::steady_clock::now();
    return it->second;
  }
  ladder_cache_evict_unlocked(key);
  auto e = std::make_shared<ShrinkLadder>();
  e->last_used = std::chrono::steady_clock::now();
  g_ladders.emplace(key, e);
  return e;
}

VipsImage* ladder_acquire_level(const std::string& key, int scale,
                                const std::function<VipsImage*()>& load_full) {
  if (scale < 0 || key.empty()) return nullptr;
  auto entry = ladder_get_or_create(key);
  std::lock_guard lock(entry->mu);
  entry->last_used = std::chrono::steady_clock::now();

  if (entry->levels.empty()) {
    VipsImage* full = load_full();
    if (!full) return nullptr;
    entry->levels.push_back(full);
  }

  while (static_cast<int>(entry->levels.size()) <= scale) {
    entry->levels.push_back(nullptr);
  }

  for (int s = 1; s <= scale; ++s) {
    if (entry->levels[static_cast<std::size_t>(s)]) continue;
    VipsImage* prev = entry->levels[static_cast<std::size_t>(s - 1)];
    if (!prev) return nullptr;
    VipsImage* halved = nullptr;
    {
      ScopedNsAccumulator timer(global_build_stats().shrink_ns);
      if (vips_shrink(prev, &halved, 2.0, 2.0, nullptr) != 0 || !halved) {
        return nullptr;
      }
    }
    entry->levels[static_cast<std::size_t>(s)] = halved;
  }

  VipsImage* out = entry->levels[static_cast<std::size_t>(scale)];
  if (!out) return nullptr;
  g_object_ref(out);
  return out;
}

std::optional<TileBlob> encode_cell_from_level(VipsImage* level, int scale, int x,
                                              int y, int jpeg_quality) {
  if (!level || x < 0 || y < 0) return std::nullopt;

  const int sw = vips_image_get_width(level);
  const int sh = vips_image_get_height(level);
  if (sw <= 0 || sh <= 0) return std::nullopt;

  const int left = x * kTileSize;
  const int top = y * kTileSize;
  if (left >= sw || top >= sh) return std::nullopt;
  const int tw = std::min(kTileSize, sw - left);
  const int th = std::min(kTileSize, sh - top);
  if (tw <= 0 || th <= 0) return std::nullopt;

  const int q = std::clamp(jpeg_quality, 1, 100);
  VipsImage* crop = nullptr;
  if (vips_crop(level, &crop, left, top, tw, th, nullptr) != 0 || !crop) {
    return std::nullopt;
  }

  void* buf = nullptr;
  size_t len = 0;
  {
    ScopedNsAccumulator timer(global_build_stats().jpeg_encode_ns);
    if (vips_jpegsave_buffer(crop, &buf, &len, "Q", q, nullptr) != 0 || !buf) {
      g_object_unref(crop);
      return std::nullopt;
    }
  }
  g_object_unref(crop);

  TileBlob tb;
  tb.scale = scale;
  tb.x = x;
  tb.y = y;
  tb.width = tw;
  tb.height = th;
  tb.codec = kDefaultTileCodec;
  tb.bytes.assign(static_cast<std::uint8_t*>(buf),
                  static_cast<std::uint8_t*>(buf) + len);
  g_free(buf);
  global_build_stats().tiles_encoded.fetch_add(1, std::memory_order_relaxed);
  return tb;
}


std::optional<TileBlob> cut_cell_from_vips(VipsImage* full, int scale, int x,
                                           int y, int jpeg_quality) {
  if (!full || scale < 0 || x < 0 || y < 0) return std::nullopt;

  const int src_w = vips_image_get_width(full);
  const int src_h = vips_image_get_height(full);
  if (src_w <= 0 || src_h <= 0) return std::nullopt;

  const std::int64_t pixels =
      static_cast<std::int64_t>(src_w) * static_cast<std::int64_t>(src_h);
  if (pixels > kTileMaxSourcePixels) return std::nullopt;

  const int q = std::clamp(jpeg_quality, 1, 100);

  VipsImage* current = full;
  g_object_ref(current);
  for (int s = 0; s < scale; ++s) {
    VipsImage* halved = nullptr;
    {
      ScopedNsAccumulator timer(global_build_stats().shrink_ns);
      if (vips_shrink(current, &halved, 2.0, 2.0, nullptr) != 0 || !halved) {
        g_object_unref(current);
        return std::nullopt;
      }
    }
    g_object_unref(current);
    current = halved;
  }

  const int sw = vips_image_get_width(current);
  const int sh = vips_image_get_height(current);
  const int left = x * kTileSize;
  const int top = y * kTileSize;
  if (left >= sw || top >= sh) {
    g_object_unref(current);
    return std::nullopt;
  }
  const int tw = std::min(kTileSize, sw - left);
  const int th = std::min(kTileSize, sh - top);
  if (tw <= 0 || th <= 0) {
    g_object_unref(current);
    return std::nullopt;
  }

  VipsImage* crop = nullptr;
  if (vips_crop(current, &crop, left, top, tw, th, nullptr) != 0 || !crop) {
    g_object_unref(current);
    return std::nullopt;
  }
  g_object_unref(current);

  void* buf = nullptr;
  size_t len = 0;
  {
    ScopedNsAccumulator timer(global_build_stats().jpeg_encode_ns);
    if (vips_jpegsave_buffer(crop, &buf, &len, "Q", q, nullptr) != 0 || !buf) {
      g_object_unref(crop);
      return std::nullopt;
    }
  }
  g_object_unref(crop);

  TileBlob tb;
  tb.scale = scale;
  tb.x = x;
  tb.y = y;
  tb.width = tw;
  tb.height = th;
  tb.codec = kDefaultTileCodec;
  tb.bytes.assign(static_cast<std::uint8_t*>(buf),
                  static_cast<std::uint8_t*>(buf) + len);
  g_free(buf);
  global_build_stats().tiles_encoded.fetch_add(1, std::memory_order_relaxed);
  return tb;
}

}  // namespace

std::vector<TileBlob> build_tile_pyramid(const std::filesystem::path& path,
                                         int min_scale, int max_scale,
                                         int jpeg_quality) {
  ensure_vips();
  std::vector<TileBlob> tiles;
  VipsImage* full = nullptr;
  {
    ScopedNsAccumulator timer(global_build_stats().image_load_ns);
    full = vips_image_new_from_file(path.string().c_str(), nullptr);
  }
  if (!full) return tiles;
  tiles = cut_pyramid_from_vips(full, min_scale, max_scale, jpeg_quality);
  g_object_unref(full);
  return tiles;
}

std::vector<TileBlob> build_tile_pyramid_buffer(const std::uint8_t* data,
                                                std::size_t size, int min_scale,
                                                int max_scale,
                                                int jpeg_quality) {
  ensure_vips();
  std::vector<TileBlob> tiles;
  if (!data || size == 0) return tiles;
  VipsImage* full = nullptr;
  {
    ScopedNsAccumulator timer(global_build_stats().image_load_ns);
    full = vips_image_new_from_buffer(data, size, nullptr, nullptr);
  }
  if (!full) return tiles;
  tiles = cut_pyramid_from_vips(full, min_scale, max_scale, jpeg_quality);
  g_object_unref(full);
  return tiles;
}


std::optional<TileBlob> build_tile_cell(const std::filesystem::path& path,
                                        int scale, int x, int y,
                                        int jpeg_quality) {
  ensure_vips();
  if (scale < 0) {
    VipsImage* full = nullptr;
    {
      ScopedNsAccumulator timer(global_build_stats().image_load_ns);
      full = vips_image_new_from_file(path.string().c_str(), nullptr);
    }
    if (!full) return std::nullopt;
    auto tile = cut_cell_from_vips(full, scale, x, y, jpeg_quality);
    g_object_unref(full);
    if (tile) tile->scale = scale;
    return tile;
  }

  const std::string key = ladder_key_for_file(path);
  VipsImage* level = ladder_acquire_level(key, scale, [&]() -> VipsImage* {
    ScopedNsAccumulator timer(global_build_stats().image_load_ns);
    return vips_image_new_from_file(path.string().c_str(), nullptr);
  });
  if (!level) return std::nullopt;
  auto tile = encode_cell_from_level(level, scale, x, y, jpeg_quality);
  g_object_unref(level);
  return tile;
}

std::optional<TileBlob> build_tile_cell_buffer(const std::uint8_t* data,
                                               std::size_t size, int scale,
                                               int x, int y, int jpeg_quality,
                                               std::string_view decode_cache_key) {
  ensure_vips();
  if (!data || size == 0) return std::nullopt;

  if (scale < 0 || decode_cache_key.empty()) {
    VipsImage* full = nullptr;
    int remain = scale;
    {
      ScopedNsAccumulator timer(global_build_stats().image_load_ns);
      const bool maybe_jpeg =
          size >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff;
      if (maybe_jpeg && scale > 0) {
        const int js = jpeg_shrink_factor_for_scale(scale);
        if (vips_jpegload_buffer(const_cast<std::uint8_t*>(data), size, &full,
                                 "shrink", js, nullptr) == 0 &&
            full) {
          remain = scale_steps_after_jpeg_shrink(scale, js);
        }
      }
      if (!full) {
        full = vips_image_new_from_buffer(data, size, nullptr, nullptr);
        remain = scale;
      }
    }
    if (!full) return std::nullopt;
    auto tile = cut_cell_from_vips(full, remain, x, y, jpeg_quality);
    g_object_unref(full);
    if (tile) tile->scale = scale;
    return tile;
  }

  const std::string key(decode_cache_key);
  VipsImage* level = ladder_acquire_level(key, scale, [&]() -> VipsImage* {
    ScopedNsAccumulator timer(global_build_stats().image_load_ns);
    return vips_image_new_from_buffer(data, size, nullptr, nullptr);
  });
  if (!level) return std::nullopt;
  auto tile = encode_cell_from_level(level, scale, x, y, jpeg_quality);
  g_object_unref(level);
  return tile;
}

namespace {

VipsImage* vips_from_rgb888(const std::uint8_t* rgb, int width, int height) {
  if (!rgb || width <= 0 || height <= 0) return nullptr;
  // vips_image_new_from_memory does not copy; keep data alive for the caller's
  // scope (cut_cell / cut_pyramid finish before returning).
  VipsImage* img = vips_image_new_from_memory(
      const_cast<std::uint8_t*>(rgb),
      static_cast<size_t>(width) * static_cast<size_t>(height) * 3u, width,
      height, 3, VIPS_FORMAT_UCHAR);
  if (!img) return nullptr;
  // 3-band uchar from memory is guessed as sRGB; set Type explicitly so
  // JPEG encode does not treat pixels as multiband.
  img->Type = VIPS_INTERPRETATION_sRGB;
  return img;
}

}  // namespace

std::optional<TileBlob> build_tile_cell_rgb(const std::uint8_t* rgb, int width,
                                            int height, int scale, int x, int y,
                                            int jpeg_quality) {
  ensure_vips();
  VipsImage* full = vips_from_rgb888(rgb, width, height);
  if (!full) return std::nullopt;
  auto tile = cut_cell_from_vips(full, scale, x, y, jpeg_quality);
  g_object_unref(full);
  if (tile) tile->scale = scale;
  return tile;
}

std::vector<TileBlob> build_tile_pyramid_rgb(const std::uint8_t* rgb, int width,
                                             int height, int min_scale,
                                             int max_scale, int jpeg_quality) {
  ensure_vips();
  std::vector<TileBlob> tiles;
  VipsImage* full = vips_from_rgb888(rgb, width, height);
  if (!full) return tiles;
  tiles = cut_pyramid_from_vips(full, min_scale, max_scale, jpeg_quality);
  g_object_unref(full);
  return tiles;
}


std::optional<TileBlob> encode_tile_cell_rgb(const std::uint8_t* rgb, int width,
                                             int height, int scale, int x, int y,
                                             int jpeg_quality) {
  if (!rgb || width <= 0 || height <= 0 || x < 0 || y < 0) return std::nullopt;
  ensure_vips();
  const int q = std::clamp(jpeg_quality, 1, 100);
  VipsImage* img = vips_image_new_from_memory_copy(
      rgb, static_cast<size_t>(width) * static_cast<size_t>(height) * 3u, width,
      height, 3, VIPS_FORMAT_UCHAR);
  if (!img) return std::nullopt;

  void* buf = nullptr;
  size_t len = 0;
  {
    ScopedNsAccumulator timer(global_build_stats().jpeg_encode_ns);
    if (vips_jpegsave_buffer(img, &buf, &len, "Q", q, nullptr) != 0 || !buf) {
      g_object_unref(img);
      return std::nullopt;
    }
  }
  g_object_unref(img);

  TileBlob tb;
  tb.scale = scale;
  tb.x = x;
  tb.y = y;
  tb.width = width;
  tb.height = height;
  tb.codec = kDefaultTileCodec;
  tb.bytes.assign(static_cast<std::uint8_t*>(buf),
                  static_cast<std::uint8_t*>(buf) + len);
  g_free(buf);
  global_build_stats().tiles_encoded.fetch_add(1, std::memory_order_relaxed);
  return tb;
}

}  // namespace thumtoo
