// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/image.hpp"
#include "thumtoo/constants.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

namespace thumtoo {
namespace {

// Minimal SHA-256 (public-domain style compact implementation).
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

std::string format_from_path(const std::filesystem::path& path) {
  auto ext = path.extension().string();
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (ext == ".jpg" || ext == ".jpeg") return "jpeg";
  if (ext == ".png") return "png";
  if (ext == ".gif") return "gif";
  if (ext == ".bmp") return "bmp";
  if (ext == ".tga") return "tga";
  if (ext == ".webp") return "webp";
  return "unknown";
}

}  // namespace

std::optional<ProbeResult> probe_image_file(const std::filesystem::path& path) {
  int w = 0, h = 0, n = 0;
  if (!stbi_info(path.string().c_str(), &w, &h, &n)) return std::nullopt;
  if (w <= 0 || h <= 0) return std::nullopt;
  return ProbeResult{Size{w, h}, format_from_path(path)};
}

std::optional<DecodedImage> load_image_file(const std::filesystem::path& path) {
  int w = 0, h = 0, n = 0;
  unsigned char* data = stbi_load(path.string().c_str(), &w, &h, &n, 4);
  if (!data || w <= 0 || h <= 0) {
    if (data) stbi_image_free(data);
    return std::nullopt;
  }
  DecodedImage img;
  img.width = w;
  img.height = h;
  img.channels = 4;
  img.rgba.assign(data, data + static_cast<std::size_t>(w) * h * 4);
  stbi_image_free(data);
  return img;
}

DecodedImage resize_to_max_edge(const DecodedImage& src, int max_edge) {
  const int long_edge = std::max(src.width, src.height);
  if (long_edge <= max_edge || max_edge <= 0) return src;

  const double scale = static_cast<double>(max_edge) / long_edge;
  const int nw = std::max(1, static_cast<int>(src.width * scale + 0.5));
  const int nh = std::max(1, static_cast<int>(src.height * scale + 0.5));

  DecodedImage out;
  out.width = nw;
  out.height = nh;
  out.channels = 4;
  out.rgba.resize(static_cast<std::size_t>(nw) * nh * 4);

  unsigned char* ok = stbir_resize_uint8_linear(
      src.rgba.data(), src.width, src.height, 0, out.rgba.data(), nw, nh, 0,
      STBIR_RGBA);
  if (!ok) {
    // Fallback: return original if resize fails.
    return src;
  }
  return out;
}

namespace {
struct JpegMem {
  std::vector<std::uint8_t> bytes;
};

void jpeg_write_callback(void* context, void* data, int size) {
  auto* mem = static_cast<JpegMem*>(context);
  auto* p = static_cast<const std::uint8_t*>(data);
  mem->bytes.insert(mem->bytes.end(), p, p + size);
}
}  // namespace

std::optional<std::vector<std::uint8_t>> encode_jpeg(const DecodedImage& img,
                                                     int quality) {
  if (img.width <= 0 || img.height <= 0 || img.rgba.empty()) return std::nullopt;
  JpegMem mem;
  const int q = std::clamp(quality, 1, 100);
  if (!stbi_write_jpg_to_func(jpeg_write_callback, &mem, img.width, img.height,
                              4, img.rgba.data(), q)) {
    return std::nullopt;
  }
  return mem.bytes;
}

std::vector<LevelBlob> build_ladder(const DecodedImage& src,
                                    const std::string& content_id,
                                    int quality) {
  std::vector<LevelBlob> levels;
  const int long_edge = std::max(src.width, src.height);
  for (int edge : kLadderEdges) {
    if (edge > long_edge && edge != kLadderEdges.front()) {
      // Still emit the native long edge once via the smallest edge that
      // does not upscale — skip pure upscale tiers except we always want
      // at least one level. Continue to next; native covered when edge>=long.
    }
    if (edge > long_edge) continue;

    auto scaled = resize_to_max_edge(src, edge);
    auto jpeg = encode_jpeg(scaled, quality);
    if (!jpeg) continue;

    LevelBlob b;
    b.max_edge = edge;
    b.frame_idx = 0;
    b.width = scaled.width;
    b.height = scaled.height;
    b.codec = "jpeg";
    b.quality = quality;
    std::string id_path = content_id;
    for (char& c : id_path) {
      if (c == ':' || c == '/') c = '_';
    }
    std::ostringstream path;
    path << "blobs/" << id_path << "/" << edge << "_f0.jpg";
    b.relative_path = path.str();
    b.bytes = std::move(*jpeg);
    levels.push_back(std::move(b));
  }

  // If image is smaller than the smallest ladder edge, still store one level
  // at the smallest edge value as key (actual pixels are native size).
  if (levels.empty() && long_edge > 0) {
    auto jpeg = encode_jpeg(src, quality);
    if (jpeg) {
      LevelBlob b;
      b.max_edge = kLadderEdges.front();
      b.frame_idx = 0;
      b.width = src.width;
      b.height = src.height;
      b.codec = "jpeg";
      b.quality = quality;
      std::string id_path = content_id;
      for (char& c : id_path) {
        if (c == ':' || c == '/') c = '_';
      }
      std::ostringstream path;
      path << "blobs/" << id_path << "/" << b.max_edge << "_f0.jpg";
      b.relative_path = path.str();
      b.bytes = std::move(*jpeg);
      levels.push_back(std::move(b));
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
