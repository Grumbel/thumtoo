// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Golden-path tile encode/decode matrix (vips only — no thumtoo Client/Store).
// Splits an image into 256² cells at scale 0 and measures encode/decode
// (jpeg|webp|avif|jxl) at several quality settings, plus PSNR of the
// decoded cells against the source.
//
// Usage:
//   thumtoo-gp-tile [--tile N] [--repeat R] [--codec C] [--quality 60,80,90]
//                   [--max-cells K] [--reference CODEC:Q] [--tie-pct P]
//                   [--json] FILE
//
//   C = jpeg | webp | avif | jxl   single codec, quality sweep table
//     | all | a,b,…                compare codecs and judge which wins
//
// Comparison mode never compares codecs at the same Q number (JPEG Q80 and
// AVIF Q80 are unrelated). It sweeps qualities, takes the PSNR of the
// reference setting (default jpeg:80 = thumtoo's kDefaultTileCodec /
// kDefaultTileQuality) as the target, represents each codec by its smallest
// output that reaches the target, and judges bytes / encode / decode there.
//
// Codec availability is probed at runtime (libvips may lack an encoder,
// e.g. heifsave without an AV1 encoder plugin). An unavailable codec is an
// error, never a zero-byte / zero-ms result.

#include <vips/vips.h>

#include "gp_common.hpp"
#include "gp_verdict.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// PSNR reported for bit-exact output (MSE == 0).
constexpr double kLosslessPsnr = 100.0;

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

/// First line of the vips error buffer (then clears it).
std::string take_vips_error() {
  std::string s = vips_error_buffer();
  vips_error_clear();
  if (auto nl = s.find('\n'); nl != std::string::npos) s.resize(nl);
  return s.empty() ? "unknown libvips error" : s;
}

// --- codecs ---------------------------------------------------------------------

enum class Codec { Jpeg, Webp, Avif, Jxl };

constexpr Codec kAllCodecs[] = {Codec::Jpeg, Codec::Webp, Codec::Avif, Codec::Jxl};

const char* codec_name(Codec c) {
  switch (c) {
    case Codec::Jpeg: return "jpeg";
    case Codec::Webp: return "webp";
    case Codec::Avif: return "avif";
    case Codec::Jxl: return "jxl";
  }
  return "?";
}

std::optional<Codec> parse_codec(const std::string& s) {
  for (Codec c : kAllCodecs) {
    if (s == codec_name(c)) return c;
  }
  return std::nullopt;
}

/// Encode with libvips defaults for everything but Q. false on failure.
bool encode(Codec codec, VipsImage* in, int q, std::vector<unsigned char>& out) {
  void* buf = nullptr;
  size_t len = 0;
  int rc = -1;
  switch (codec) {
    case Codec::Jpeg:
      rc = vips_jpegsave_buffer(in, &buf, &len, "Q", q, "strip", TRUE, nullptr);
      break;
    case Codec::Webp:
      rc = vips_webpsave_buffer(in, &buf, &len, "Q", q, nullptr);
      break;
    case Codec::Avif:
      // AVIF via the HEIF saver. Needs libvips + libheif with an AV1 encoder.
      rc = vips_heifsave_buffer(in, &buf, &len, "Q", q, "compression",
                                VIPS_FOREIGN_HEIF_COMPRESSION_AV1, nullptr);
      break;
    case Codec::Jxl:
      rc = vips_jxlsave_buffer(in, &buf, &len, "Q", q, nullptr);
      break;
  }
  if (rc != 0 || !buf || len == 0) {
    if (buf) g_free(buf);
    return false;
  }
  out.assign(static_cast<unsigned char*>(buf), static_cast<unsigned char*>(buf) + len);
  g_free(buf);
  return true;
}

/// Decode a blob; nullptr on failure. Caller unrefs.
VipsImage* decode(Codec codec, const std::vector<unsigned char>& blob) {
  void* data = const_cast<unsigned char*>(blob.data());
  VipsImage* img = nullptr;
  int rc = -1;
  switch (codec) {
    case Codec::Jpeg: rc = vips_jpegload_buffer(data, blob.size(), &img, nullptr); break;
    case Codec::Webp: rc = vips_webpload_buffer(data, blob.size(), &img, nullptr); break;
    case Codec::Avif: rc = vips_heifload_buffer(data, blob.size(), &img, nullptr); break;
    case Codec::Jxl: rc = vips_jxlload_buffer(data, blob.size(), &img, nullptr); break;
  }
  if (rc != 0) {
    if (img) g_object_unref(img);
    return nullptr;
  }
  return img;
}

/// Decode fully into pixels (the cost a tile consumer pays). false on failure.
bool decode_pixels(Codec codec, const std::vector<unsigned char>& blob,
                   std::vector<unsigned char>* pixels, int* w, int* h, int* bands) {
  VipsImage* img = decode(codec, blob);
  if (!img) return false;
  size_t len = 0;
  void* buf = vips_image_write_to_memory(img, &len);
  const bool ok = buf != nullptr;
  if (ok && pixels) {
    if (vips_image_get_format(img) != VIPS_FORMAT_UCHAR) {
      // Tiles are 8-bit; a wider decode would not be comparable.
      g_free(buf);
      g_object_unref(img);
      return false;
    }
    pixels->assign(static_cast<unsigned char*>(buf), static_cast<unsigned char*>(buf) + len);
    *w = vips_image_get_width(img);
    *h = vips_image_get_height(img);
    *bands = vips_image_get_bands(img);
  }
  if (buf) g_free(buf);
  g_object_unref(img);
  return ok;
}

// --- source ---------------------------------------------------------------------

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

/// Decoded source image held in memory, as 8-bit sRGB or B_W without alpha
/// (what thumtoo tiles store), plus each cell's crop and reference pixels.
struct Source {
  VipsImage* mem = nullptr;
  int width = 0, height = 0, bands = 0;
  std::string loader;  // e.g. "jpegload", "pngload"
  bool lossy = false;  // source itself was lossy-coded
  std::vector<Cell> cells;
  std::vector<VipsImage*> crops;
  std::vector<std::vector<unsigned char>> ref_pixels;

  Source() = default;
  Source(const Source&) = delete;
  Source& operator=(const Source&) = delete;
  ~Source() {
    for (auto* c : crops) g_object_unref(c);
    if (mem) g_object_unref(mem);
  }
};

/// Replace *img with the result of op (unref old) — false on failure.
template <typename Op>
bool apply(VipsImage** img, Op op) {
  VipsImage* out = nullptr;
  if (op(*img, &out) != 0 || !out) return false;
  g_object_unref(*img);
  *img = out;
  return true;
}

bool load_source(const fs::path& file, int tile, Source& src, std::string& err) {
  VipsImage* img = vips_image_new_from_file(file.string().c_str(), nullptr);
  if (!img) {
    err = "open failed: " + take_vips_error();
    return false;
  }
  const char* loader = nullptr;
  if (vips_image_get_typeof(img, "vips-loader") &&
      vips_image_get_string(img, "vips-loader", &loader) == 0 && loader) {
    src.loader = loader;
  }
  for (const char* lossy : {"jpegload", "webpload", "heifload", "jxlload"}) {
    if (src.loader.rfind(lossy, 0) == 0) src.lossy = true;  // also *_source etc.
  }
  bool ok = true;
  if (vips_image_hasalpha(img)) {
    ok = apply(&img, [](VipsImage* in, VipsImage** out) {
      VipsArrayDouble* white = vips_array_double_newv(1, 255.0);
      const int rc = vips_flatten(in, out, "background", white, nullptr);
      vips_area_unref(VIPS_AREA(white));
      return rc;
    });
  }
  if (ok && vips_image_get_format(img) != VIPS_FORMAT_UCHAR) {
    // colourspace() rescales 16-bit / float data; a plain cast would clip.
    const VipsInterpretation target = vips_image_get_bands(img) >= 3
                                          ? VIPS_INTERPRETATION_sRGB
                                          : VIPS_INTERPRETATION_B_W;
    ok = apply(&img, [target](VipsImage* in, VipsImage** out) {
      return vips_colourspace(in, out, target, nullptr);
    });
  }
  if (ok) {
    // Decode once, fully, so encode timings never include source decode.
    VipsImage* mem = vips_image_copy_memory(img);
    ok = mem != nullptr;
    if (ok) {
      g_object_unref(img);
      img = mem;
    }
  }
  if (!ok) {
    g_object_unref(img);
    err = "prepare failed: " + take_vips_error();
    return false;
  }

  src.mem = img;
  src.width = vips_image_get_width(img);
  src.height = vips_image_get_height(img);
  src.bands = vips_image_get_bands(img);
  src.cells = grid_cells(src.width, src.height, tile);
  for (const Cell& c : src.cells) {
    VipsImage* crop = nullptr;
    if (vips_crop(src.mem, &crop, c.x, c.y, c.w, c.h, nullptr) != 0 || !crop) {
      err = "crop failed: " + take_vips_error();
      return false;
    }
    src.crops.push_back(crop);
    size_t len = 0;
    void* buf = vips_image_write_to_memory(crop, &len);
    if (!buf) {
      err = "crop read failed: " + take_vips_error();
      return false;
    }
    src.ref_pixels.emplace_back(static_cast<unsigned char*>(buf),
                                static_cast<unsigned char*>(buf) + len);
    g_free(buf);
  }
  return true;
}

// --- measurement ----------------------------------------------------------------

enum class Status { Ok, Unsupported, Failed };

const char* status_name(Status s) {
  switch (s) {
    case Status::Ok: return "ok";
    case Status::Unsupported: return "unsupported";
    case Status::Failed: return "failed";
  }
  return "failed";
}

struct Row {
  int quality = 0;
  gp::Timing enc, dec;
  std::uint64_t bytes_total = 0;
  double bytes_per_cell_mean = 0;
  double psnr_db = 0;
};

struct CodecResult {
  Codec codec = Codec::Jpeg;
  Status status = Status::Failed;
  std::string reason;
  std::vector<Row> rows;
};

/// Encode + decode a tiny synthetic image: does this libvips build have a
/// working encoder *and* decoder for `codec`? Returns "" or the reason.
std::string probe_codec(Codec codec) {
  VipsImage* black = nullptr;
  VipsImage* rgb = nullptr;
  std::string why;
  if (vips_black(&black, 16, 16, "bands", 3, nullptr) != 0 ||
      vips_cast_uchar(black, &rgb, nullptr) != 0) {
    why = "probe image: " + take_vips_error();
  } else {
    std::vector<unsigned char> blob;
    if (!encode(codec, rgb, 80, blob)) {
      why = "encoder unavailable: " + take_vips_error();
    } else if (!decode_pixels(codec, blob, nullptr, nullptr, nullptr, nullptr)) {
      why = "decoder unavailable: " + take_vips_error();
    }
  }
  if (rgb) g_object_unref(rgb);
  if (black) g_object_unref(black);
  return why;
}

/// Sum of squared errors over the bands both images share. Decoders may add
/// bands (gray → RGB); fewer bands or a size change is a failure.
std::optional<double> squared_error(const std::vector<unsigned char>& ref, int ref_bands,
                                    const std::vector<unsigned char>& got, int got_bands,
                                    std::uint64_t pixels) {
  if (got_bands < ref_bands ||
      got.size() != pixels * static_cast<std::uint64_t>(got_bands) ||
      ref.size() != pixels * static_cast<std::uint64_t>(ref_bands)) {
    return std::nullopt;
  }
  double sse = 0;
  for (std::uint64_t p = 0; p < pixels; ++p) {
    for (int b = 0; b < ref_bands; ++b) {
      const double d = static_cast<double>(ref[p * ref_bands + b]) -
                       static_cast<double>(got[p * got_bands + b]);
      sse += d * d;
    }
  }
  return sse;
}

/// Full-grid bytes + PSNR for one setting; timings only when `timed`.
std::optional<Row> measure_quality(Codec codec, int q, const Source& src, int time_n,
                                   int repeats, bool timed, std::string& err) {
  const std::size_t n = src.cells.size();
  std::vector<std::vector<unsigned char>> blobs(n);
  Row row;
  row.quality = q;

  // Full grid once: bytes and PSNR over every cell.
  double sse = 0;
  std::uint64_t samples = 0;
  for (std::size_t i = 0; i < n; ++i) {
    if (!encode(codec, src.crops[i], q, blobs[i])) {
      err = "encode failed at cell " + std::to_string(i) + ": " + take_vips_error();
      return std::nullopt;
    }
    row.bytes_total += blobs[i].size();
    std::vector<unsigned char> px;
    int w = 0, h = 0, bands = 0;
    if (!decode_pixels(codec, blobs[i], &px, &w, &h, &bands)) {
      err = "decode failed at cell " + std::to_string(i) + ": " + take_vips_error();
      return std::nullopt;
    }
    const Cell& c = src.cells[i];
    if (w != c.w || h != c.h) {
      err = "decoded cell " + std::to_string(i) + " has wrong size";
      return std::nullopt;
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(w) * h;
    auto e = squared_error(src.ref_pixels[i], src.bands, px, bands, pixels);
    if (!e) {
      err = "decoded cell " + std::to_string(i) + " has incompatible bands";
      return std::nullopt;
    }
    sse += *e;
    samples += pixels * static_cast<std::uint64_t>(src.bands);
  }
  const double mse = samples ? sse / static_cast<double>(samples) : 0.0;
  row.psnr_db = mse > 0 ? 10.0 * std::log10(255.0 * 255.0 / mse) : kLosslessPsnr;
  row.bytes_per_cell_mean = n ? static_cast<double>(row.bytes_total) / n : 0.0;
  if (!timed) return row;

  // Timed subset (all outputs were verified above).
  std::vector<unsigned char> scratch;
  row.enc = gp::time_median(repeats, [&] {
    for (int i = 0; i < time_n; ++i) (void)encode(codec, src.crops[i], q, scratch);
  });
  row.dec = gp::time_median(repeats, [&] {
    for (int i = 0; i < time_n; ++i) {
      (void)decode_pixels(codec, blobs[i], nullptr, nullptr, nullptr, nullptr);
    }
  });
  return row;
}

CodecResult measure_codec(Codec codec, const std::vector<int>& qualities,
                          const Source& src, int time_n, int repeats) {
  CodecResult r;
  r.codec = codec;
  if (auto why = probe_codec(codec); !why.empty()) {
    r.status = Status::Unsupported;
    r.reason = why;
    return r;
  }
  for (int q : qualities) {
    std::string err;
    auto row = measure_quality(codec, q, src, time_n, repeats, /*timed=*/true, err);
    if (!row) {
      r.status = Status::Failed;
      r.reason = "q=" + std::to_string(q) + ": " + err;
      r.rows.clear();
      return r;
    }
    r.rows.push_back(*row);
  }
  r.status = Status::Ok;
  return r;
}

// --- output ---------------------------------------------------------------------

void write_row_json(std::ostream& os, Codec codec, const Row& row) {
  os << "{\"codec\": \"" << codec_name(codec) << "\", \"quality\": " << row.quality
     << ", \"encode_ms\": " << row.enc.median << ", \"decode_ms\": " << row.dec.median
     << ", \"bytes_total\": " << row.bytes_total
     << ", \"bytes_per_cell_mean\": " << row.bytes_per_cell_mean
     << ", \"psnr_db\": " << row.psnr_db << "}";
}

void write_header_json(std::ostream& os, const fs::path& file, const Source& src,
                       int tile, int time_n, int repeats) {
  os << "  \"tool\": \"thumtoo-gp-tile\",\n  \"file\": ";
  gp::json_string(os, file.string());
  os << ",\n  \"width\": " << src.width << ",\n  \"height\": " << src.height
     << ",\n  \"tile\": " << tile << ",\n  \"cells\": " << src.cells.size()
     << ",\n  \"timed_cells\": " << time_n << ",\n  \"repeats\": " << repeats << ",\n";
}

int report_single(const CodecResult& r, const fs::path& file, const Source& src,
                  int tile, int time_n, int repeats, bool json) {
  if (r.status != Status::Ok) {
    std::cerr << codec_name(r.codec) << ": " << status_name(r.status) << ": "
              << r.reason << "\n";
    return 1;
  }
  if (!json) {
    std::cout << "file=" << file.filename().string() << " " << src.width << "x"
              << src.height << " tiles=" << src.cells.size() << " tile=" << tile
              << " timed_cells=" << time_n << " codec=" << codec_name(r.codec) << "\n";
    std::cout << "quality,encode_ms_median,decode_ms_median,bytes_total,"
                 "bytes_per_cell_mean,psnr_db\n";
    for (const Row& row : r.rows) {
      std::printf("%d,%.3f,%.3f,%llu,%.1f,%.2f\n", row.quality, row.enc.median,
                  row.dec.median, static_cast<unsigned long long>(row.bytes_total),
                  row.bytes_per_cell_mean, row.psnr_db);
    }
    return 0;
  }
  std::cout << "{\n  \"schema\": 1,\n";
  write_header_json(std::cout, file, src, tile, time_n, repeats);
  std::cout << "  \"rows\": [";
  for (std::size_t i = 0; i < r.rows.size(); ++i) {
    std::cout << (i ? ",\n    " : "\n    ");
    write_row_json(std::cout, r.codec, r.rows[i]);
  }
  std::cout << "\n  ]\n}\n";
  return 0;
}

// --- comparison -----------------------------------------------------------------

struct Reference {
  Codec codec = Codec::Jpeg;
  int quality = 80;  // thumtoo kDefaultTileCodec / kDefaultTileQuality
};

std::optional<Reference> parse_reference(const std::string& s) {
  const auto colon = s.find(':');
  if (colon == std::string::npos) return std::nullopt;
  auto codec = parse_codec(s.substr(0, colon));
  const int q = std::atoi(s.c_str() + colon + 1);
  if (!codec || q < 1 || q > 100) return std::nullopt;
  return Reference{*codec, q};
}

/// A codec's representative row at the reference quality level.
struct Match {
  Codec codec = Codec::Jpeg;
  const Row* row = nullptr;   // nullptr: no setting reached the target
  const Row* best = nullptr;  // highest-PSNR row (for the "below target" note)
};

/// Smallest output whose PSNR reaches `target_db` (ties: faster decode).
Match match_quality(const CodecResult& r, double target_db) {
  Match m;
  m.codec = r.codec;
  for (const Row& row : r.rows) {
    if (!m.best || row.psnr_db > m.best->psnr_db) m.best = &row;
    if (row.psnr_db + 1e-9 < target_db) continue;
    if (!m.row || row.bytes_total < m.row->bytes_total ||
        (row.bytes_total == m.row->bytes_total && row.dec.median < m.row->dec.median)) {
      m.row = &row;
    }
  }
  return m;
}

/// Narrow a codec's match to the lowest Q that reaches `target_db`.
///
/// A coarse sweep overshoots: if q70 is just below the target the match is
/// q80, charging the codec for quality it was not asked for. Bisect Q between
/// the bracketing sweep points with untimed bytes+PSNR evaluations (assumes
/// PSNR is non-decreasing in Q, which holds closely for these encoders), then
/// time only the final setting and add it to the rows.
void refine_match(CodecResult& r, double target_db, const Source& src, int time_n,
                  int repeats) {
  if (r.status != Status::Ok) return;
  int hi = 0;  // lowest sweep Q reaching the target
  for (const Row& row : r.rows) {
    if (row.psnr_db + 1e-9 >= target_db && (hi == 0 || row.quality < hi)) hi = row.quality;
  }
  if (hi == 0) return;  // nothing reaches it; leave as "below target"
  int lo = 0;  // highest sweep Q below hi that misses (0 = Q1 never measured)
  for (const Row& row : r.rows) {
    if (row.quality < hi && row.psnr_db + 1e-9 < target_db) lo = std::max(lo, row.quality);
  }
  while (hi - lo > 1) {
    const int mid = lo + (hi - lo) / 2;
    std::string err;
    auto row = measure_quality(r.codec, mid, src, time_n, repeats, /*timed=*/false, err);
    if (!row) return;  // keep the sweep result rather than guess
    if (row->psnr_db + 1e-9 >= target_db) hi = mid;
    else lo = mid;
  }
  for (const Row& row : r.rows) {
    if (row.quality == hi) return;  // already measured with timings
  }
  std::string err;
  auto row = measure_quality(r.codec, hi, src, time_n, repeats, /*timed=*/true, err);
  if (!row) return;
  r.rows.push_back(*row);
  std::sort(r.rows.begin(), r.rows.end(),
            [](const Row& a, const Row& b) { return a.quality < b.quality; });
}

const std::vector<gp::MetricSpec>& matched_metrics() {
  static const std::vector<gp::MetricSpec> specs = {
      {"bytes_total", gp::Better::Lower},
      {"encode_ms", gp::Better::Lower},
      {"decode_ms", gp::Better::Lower},
  };
  return specs;
}

int report_compare(const std::vector<CodecResult>& results, const Reference& ref,
                   const fs::path& file, const Source& src, int tile, int time_n,
                   int repeats, double tie_pct, bool json) {
  const CodecResult* ref_result = nullptr;
  for (const auto& r : results) {
    if (r.codec == ref.codec) ref_result = &r;
  }
  const Row* ref_row = nullptr;
  if (ref_result && ref_result->status == Status::Ok) {
    for (const Row& row : ref_result->rows) {
      if (row.quality == ref.quality) ref_row = &row;
    }
  }
  const double target = ref_row ? ref_row->psnr_db : 0.0;

  std::vector<Match> matches;
  std::vector<gp::Candidate> candidates;
  if (ref_row) {
    for (const auto& r : results) {
      if (r.status != Status::Ok) continue;
      Match m = match_quality(r, target);
      if (m.row) {
        candidates.push_back({codec_name(r.codec),
                              {static_cast<double>(m.row->bytes_total),
                               m.row->enc.median, m.row->dec.median}});
      }
      matches.push_back(m);
    }
  }
  const gp::Verdict verdict = gp::judge(matched_metrics(), candidates, tie_pct);

  if (!json) {
    std::cout << "file=" << file.filename().string() << " " << src.width << "x"
              << src.height << " tiles=" << src.cells.size() << " tile=" << tile
              << " timed_cells=" << time_n << " repeats=" << repeats << "\n";
    if (src.lossy) {
      std::cout << "warning: source was decoded by " << src.loader
                << "; its own codec is favored (re-encoding near the source quality "
                   "is nearly lossless). Prefer a lossless source (PNG) for codec "
                   "comparisons.\n";
    }
    std::printf("%-6s %7s %10s %10s %12s %8s\n", "codec", "quality", "encode_ms",
                "decode_ms", "bytes_total", "psnr_db");
    for (const auto& r : results) {
      if (r.status != Status::Ok) {
        std::printf("%-6s %s: %s\n", codec_name(r.codec), status_name(r.status),
                    r.reason.c_str());
        continue;
      }
      for (const Row& row : r.rows) {
        std::printf("%-6s %7d %10.3f %10.3f %12llu %8.2f\n", codec_name(r.codec),
                    row.quality, row.enc.median, row.dec.median,
                    static_cast<unsigned long long>(row.bytes_total), row.psnr_db);
      }
    }
    if (!ref_row) {
      std::cout << "verdict: refused — reference " << codec_name(ref.codec) << ":"
                << ref.quality << " was not measured successfully\n";
      return 1;
    }
    std::printf("\nreference %s:%d -> %.2f dB; each codec at its smallest setting "
                "reaching that:\n",
                codec_name(ref.codec), ref.quality, target);
    std::printf("%-6s %7s %10s %10s %12s %8s\n", "codec", "quality", "encode_ms",
                "decode_ms", "bytes_total", "psnr_db");
    for (const Match& m : matches) {
      if (m.row) {
        std::printf("%-6s %7d %10.3f %10.3f %12llu %8.2f\n", codec_name(m.codec),
                    m.row->quality, m.row->enc.median, m.row->dec.median,
                    static_cast<unsigned long long>(m.row->bytes_total),
                    m.row->psnr_db);
      } else {
        std::printf("%-6s below target in this sweep (best %.2f dB at q%d)\n",
                    codec_name(m.codec), m.best ? m.best->psnr_db : 0.0,
                    m.best ? m.best->quality : 0);
      }
    }
    gp::print_verdict(std::cout, verdict);
  } else {
    std::cout << "{\n  \"schema\": 1,\n  \"kind\": \"compare\",\n";
    write_header_json(std::cout, file, src, tile, time_n, repeats);
    std::cout << "  \"source_loader\": ";
    gp::json_string(std::cout, src.loader);
    std::cout << ",\n  \"source_lossy\": " << (src.lossy ? "true" : "false") << ",\n";
    std::cout << "  \"reference\": {\"codec\": \"" << codec_name(ref.codec)
              << "\", \"quality\": " << ref.quality << ", \"psnr_db\": ";
    if (ref_row) std::cout << target;
    else std::cout << "null";
    std::cout << "},\n  \"codecs\": [";
    for (std::size_t i = 0; i < results.size(); ++i) {
      const CodecResult& r = results[i];
      std::cout << (i ? ",\n" : "\n") << "    {\"codec\": \"" << codec_name(r.codec)
                << "\", \"status\": \"" << status_name(r.status) << "\"";
      if (r.status == Status::Ok) {
        std::cout << ", \"rows\": [";
        for (std::size_t j = 0; j < r.rows.size(); ++j) {
          std::cout << (j ? ",\n      " : "\n      ");
          write_row_json(std::cout, r.codec, r.rows[j]);
        }
        std::cout << "\n    ]}";
      } else {
        std::cout << ", \"reason\": ";
        gp::json_string(std::cout, r.reason);
        std::cout << "}";
      }
    }
    std::cout << "\n  ],\n  \"matched\": [";
    bool first = true;
    for (const Match& m : matches) {
      std::cout << (first ? "\n    " : ",\n    ");
      first = false;
      if (m.row) {
        write_row_json(std::cout, m.codec, *m.row);
      } else {
        std::cout << "{\"codec\": \"" << codec_name(m.codec)
                  << "\", \"below_target\": true, \"best_psnr_db\": "
                  << (m.best ? m.best->psnr_db : 0.0) << "}";
      }
    }
    std::cout << (matches.empty() ? "],\n" : "\n  ],\n") << "  \"verdict\": ";
    gp::write_verdict_json(std::cout, verdict, "  ");
    std::cout << "\n}\n";
  }
  return (ref_row && !candidates.empty()) ? 0 : 1;
}

std::vector<int> parse_qualities(const std::string& s) {
  std::vector<int> out;
  std::stringstream ss(s);
  std::string part;
  while (std::getline(ss, part, ',')) {
    if (part.empty()) continue;
    const int q = std::clamp(std::atoi(part.c_str()), 1, 100);
    if (std::find(out.begin(), out.end(), q) == out.end()) out.push_back(q);
  }
  return out;
}

void usage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " [--tile N] [--repeat R] [--codec jpeg|webp|avif|jxl|all|a,b] "
               "[--quality 60,80,90] [--max-cells K] [--reference CODEC:Q] "
               "[--tie-pct P] [--json] FILE\n"
            << "Golden-path tile encode/decode matrix (vips only) with PSNR.\n"
            << "--codec all (or a list) sweeps qualities (default "
               "30,40,50,60,70,80,90),\n"
            << "matches every codec to the PSNR of --reference (default jpeg:80) "
               "and reports which wins.\n";
}

}  // namespace

int main(int argc, char** argv) {
  int tile = 256;
  int repeats = 3;
  bool json_out = false;
  std::string quality_list;  // empty: mode-dependent default
  std::string codec_flag = "jpeg";
  std::string reference_flag = "jpeg:80";
  double tie_pct = 5.0;
  fs::path file;
  // Cap cells timed for large images (full grid still encoded for bytes/PSNR).
  int max_time_cells = 16;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--tile" && i + 1 < argc) {
      tile = std::max(16, std::atoi(argv[++i]));
    } else if (a == "--repeat" && i + 1 < argc) {
      repeats = std::max(1, std::atoi(argv[++i]));
    } else if (a == "--codec" && i + 1 < argc) {
      codec_flag = argv[++i];
    } else if (a == "--quality" && i + 1 < argc) {
      quality_list = argv[++i];
    } else if (a == "--max-cells" && i + 1 < argc) {
      max_time_cells = std::max(1, std::atoi(argv[++i]));
    } else if (a == "--reference" && i + 1 < argc) {
      reference_flag = argv[++i];
    } else if (a == "--tie-pct" && i + 1 < argc) {
      tie_pct = std::max(0.0, std::atof(argv[++i]));
    } else if (a == "--json") {
      json_out = true;
    } else if (a == "-h" || a == "--help") {
      usage(argv[0]);
      return 0;
    } else if (!a.empty() && a[0] == '-') {
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
  std::vector<Codec> codecs;
  if (codec_flag == "all") {
    codecs.assign(std::begin(kAllCodecs), std::end(kAllCodecs));
  } else {
    std::stringstream ss(codec_flag);
    std::string part;
    while (std::getline(ss, part, ',')) {
      if (part.empty()) continue;
      const auto c = parse_codec(part);
      if (!c) {
        std::cerr << "unknown codec (want jpeg|webp|avif|jxl|all): " << part << "\n";
        return 2;
      }
      if (std::find(codecs.begin(), codecs.end(), *c) == codecs.end()) codecs.push_back(*c);
    }
  }
  if (codecs.empty()) {
    std::cerr << "need --codec\n";
    return 2;
  }
  const bool compare = codecs.size() > 1;

  const auto reference = parse_reference(reference_flag);
  if (!reference) {
    std::cerr << "bad --reference (want CODEC:Q, e.g. jpeg:80): " << reference_flag << "\n";
    return 2;
  }
  if (compare &&
      std::find(codecs.begin(), codecs.end(), reference->codec) == codecs.end()) {
    std::cerr << "reference codec " << codec_name(reference->codec)
              << " is not in --codec " << codec_flag << "\n";
    return 2;
  }

  auto qualities = parse_qualities(quality_list);
  if (qualities.empty()) {
    qualities = compare ? std::vector<int>{30, 40, 50, 60, 70, 80, 90}
                        : std::vector<int>{60, 80, 90};
  }

  ensure_vips();
  Source src;
  std::string err;
  if (!load_source(file, tile, src, err)) {
    std::cerr << file.string() << ": " << err << "\n";
    return 1;
  }
  const int time_n = std::min(max_time_cells, static_cast<int>(src.cells.size()));

  if (!compare) {
    const CodecResult result = measure_codec(codecs.front(), qualities, src, time_n, repeats);
    return report_single(result, file, src, tile, time_n, repeats, json_out);
  }

  std::vector<CodecResult> results;
  for (Codec c : codecs) {
    std::vector<int> qs = qualities;
    if (c == reference->codec &&
        std::find(qs.begin(), qs.end(), reference->quality) == qs.end()) {
      qs.push_back(reference->quality);  // the target must be measured
      std::sort(qs.begin(), qs.end());
    }
    results.push_back(measure_codec(c, qs, src, time_n, repeats));
  }
  if (src.lossy && json_out) {
    std::cerr << "warning: lossy source (" << src.loader
              << ") biases the comparison toward that codec; prefer a PNG source\n";
  }
  // Refine every codec's match against the reference PSNR (see refine_match).
  double target = -1;
  for (const auto& r : results) {
    if (r.codec != reference->codec || r.status != Status::Ok) continue;
    for (const Row& row : r.rows) {
      if (row.quality == reference->quality) target = row.psnr_db;
    }
  }
  if (target >= 0) {
    for (auto& r : results) {
      if (r.codec != reference->codec) refine_match(r, target, src, time_n, repeats);
    }
  }
  return report_compare(results, *reference, file, src, tile, time_n, repeats, tie_pct,
                        json_out);
}
