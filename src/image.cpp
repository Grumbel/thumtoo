// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/image.hpp"
#include "thumtoo/constants.hpp"

#include <vips/vips.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>

namespace thumtoo {
namespace {

std::once_flag g_vips_once;

void ensure_vips() {
  std::call_once(g_vips_once, [] {
    // VIPS_INIT returns 0 on success.
    if (VIPS_INIT("thumtoo") != 0) {
      // Subsequent calls surface errors via NULL returns / vips_error.
    }
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

std::vector<LevelBlob> build_ladder(const std::filesystem::path& path,
                                    const std::string& content_id,
                                    int jxl_quality) {
  ensure_vips();
  std::vector<LevelBlob> levels;

  VipsImage* full = vips_image_new_from_file(path.string().c_str(), nullptr);
  if (!full) return levels;
  const int src_w = vips_image_get_width(full);
  const int src_h = vips_image_get_height(full);
  const int long_edge = std::max(src_w, src_h);
  g_object_unref(full);
  if (long_edge <= 0) return levels;

  const int q = std::clamp(jxl_quality, 1, 100);
  const std::string id_dir = content_id_to_blob_dir(content_id);
  const std::string path_str = path.string();

  for (int edge : kLadderEdges) {
    if (edge > long_edge) continue;

    VipsImage* thumb = nullptr;
    // int vips_thumbnail(filename, VipsImage **out, int width, ...)
    if (vips_thumbnail(path_str.c_str(), &thumb, edge, "size", VIPS_SIZE_DOWN,
                       nullptr) != 0 ||
        !thumb) {
      continue;
    }

    void* buf = nullptr;
    size_t len = 0;
    if (vips_jxlsave_buffer(thumb, &buf, &len, "Q", q, nullptr) != 0 || !buf) {
      g_object_unref(thumb);
      continue;
    }

    LevelBlob b;
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
    g_object_unref(thumb);
    levels.push_back(std::move(b));
  }

  if (levels.empty() && long_edge > 0) {
    VipsImage* img = vips_image_new_from_file(path_str.c_str(), nullptr);
    if (img) {
      void* buf = nullptr;
      size_t len = 0;
      if (vips_jxlsave_buffer(img, &buf, &len, "Q", q, nullptr) == 0 && buf) {
        LevelBlob b;
        b.max_edge = kLadderEdges.front();
        b.frame_idx = 0;
        b.width = vips_image_get_width(img);
        b.height = vips_image_get_height(img);
        b.codec = "jxl";
        b.quality = q;
        std::ostringstream rel;
        rel << "blobs/" << id_dir << "/" << b.max_edge << "_f0.jxl";
        b.relative_path = rel.str();
        auto* bytes = static_cast<std::uint8_t*>(buf);
        b.bytes.assign(bytes, bytes + len);
        g_free(buf);
        levels.push_back(std::move(b));
      }
      g_object_unref(img);
    }
  }
  return levels;
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

}  // namespace thumtoo
