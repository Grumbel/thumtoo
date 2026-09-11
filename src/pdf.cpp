// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/pdf.hpp"
#include "thumtoo/pdf_mupdf.hpp"
#include "thumtoo/constants.hpp"
#include "thumtoo/format.hpp"
#include "thumtoo/uri.hpp"
#include "thumtoo/image.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <cmath>
#include <cstring>
#include <string>

#if defined(THUMTOO_HAVE_POPPLER)
#include <poppler-document.h>
#include <poppler-page.h>
#include <poppler-page-renderer.h>
#if defined(THUMTOO_HAVE_POPPLER_GLIB)
#include <poppler.h>
#endif
#endif

namespace thumtoo {
namespace {

constexpr std::string_view kPagePipe = "//page:";
constexpr std::string_view kPopplerPagePipe = "//poppler-page:";
constexpr std::string_view kMupdfPagePipe = "//mupdf-page:";

#if defined(THUMTOO_HAVE_POPPLER)
/// Per-worker PDF document cache. Poppler documents are not shared across
/// threads; Client workers process jobs sequentially per thread, so the same
/// page's tiles reuse one open document instead of load_from_file per cell.
struct TlsPdfDocument {
  std::string path_key;
  std::filesystem::file_time_type mtime{};
  std::unique_ptr<poppler::document> doc;
};

thread_local TlsPdfDocument g_tls_pdf_doc;

struct TlsPdfLayout {
  std::string path_key;
  int page = 0;
  Size size72{};
};

thread_local TlsPdfLayout g_tls_pdf_layout;

/// Keep the last open page object (create_page is not free).
struct TlsPdfPage {
  std::string path_key;
  int page = 0;  // 1-based
  std::unique_ptr<poppler::page> page_obj;
};
thread_local TlsPdfPage g_tls_pdf_page;

/// Full-page RGB at a given DPI for crop-based cells. Scanned PDFs often
/// re-decode the same large image XObject per region render; one full raster
/// + memcpy crops is much cheaper when the page fits in memory.
struct TlsPdfPageRaster {
  std::string path_key;
  int page = 0;
  double dpi = 0;
  PdfRaster raster;
};
thread_local TlsPdfPageRaster g_tls_pdf_page_raster;

/// Cap cached full-page raster long edge (pixels). Above this, use region render.
constexpr int kPdfPageRasterCacheMaxEdge = 4096;

[[nodiscard]] poppler::page* cached_pdf_page(poppler::document* doc,
                                            const std::string& path_key,
                                            int page_1based) {
  if (!doc || page_1based < 1) return nullptr;
  if (g_tls_pdf_page.page_obj && g_tls_pdf_page.path_key == path_key &&
      g_tls_pdf_page.page == page_1based) {
    return g_tls_pdf_page.page_obj.get();
  }
  if (page_1based > doc->pages()) return nullptr;
  auto loaded = std::unique_ptr<poppler::page>(doc->create_page(page_1based - 1));
  if (!loaded) {
    g_tls_pdf_page = {};
    return nullptr;
  }
  g_tls_pdf_page.path_key = path_key;
  g_tls_pdf_page.page = page_1based;
  g_tls_pdf_page.page_obj = std::move(loaded);
  return g_tls_pdf_page.page_obj.get();
}

[[nodiscard]] poppler::document* cached_pdf_document(
    const std::filesystem::path& path) {
  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(path, ec);
  const std::string key = path.lexically_normal().string();
  if (g_tls_pdf_doc.doc && g_tls_pdf_doc.path_key == key && !ec &&
      g_tls_pdf_doc.mtime == mtime) {
    return g_tls_pdf_doc.doc.get();
  }
  auto loaded = std::unique_ptr<poppler::document>(
      poppler::document::load_from_file(path.string()));
  if (!loaded || loaded->is_locked()) {
    g_tls_pdf_doc = {};
    return nullptr;
  }
  g_tls_pdf_doc.path_key = key;
  g_tls_pdf_doc.mtime =
      ec ? std::filesystem::file_time_type{} : mtime;
  g_tls_pdf_doc.doc = std::move(loaded);
  // Path change invalidates layout/page/raster caches.
  if (g_tls_pdf_layout.path_key != key) {
    g_tls_pdf_layout = {};
  }
  g_tls_pdf_page = {};
  g_tls_pdf_page_raster = {};
  return g_tls_pdf_doc.doc.get();
}
#endif

}  // namespace

bool is_likely_pdf_path(const std::filesystem::path& path) {
  return is_pdf_path(path);
}

PdfBackend pdf_resolve_backend(PdfBackend requested) {
  if (requested == PdfBackend::Default) {
#if defined(THUMTOO_HAVE_MUPDF)
    return PdfBackend::MuPDF;
#elif defined(THUMTOO_HAVE_POPPLER)
    return PdfBackend::Poppler;
#else
    return PdfBackend::Default;
#endif
  }
  return requested;
}

bool pdf_backend_available(PdfBackend backend) {
  const PdfBackend b = pdf_resolve_backend(backend);
  switch (b) {
    case PdfBackend::Poppler:
#if defined(THUMTOO_HAVE_POPPLER)
      return true;
#else
      return false;
#endif
    case PdfBackend::MuPDF:
#if defined(THUMTOO_HAVE_MUPDF)
      return true;
#else
      return false;
#endif
    case PdfBackend::Default:
      return false;
  }
  return false;
}

const char* pdf_backend_name(PdfBackend backend) {
  switch (pdf_resolve_backend(backend)) {
    case PdfBackend::Poppler:
      return "poppler";
    case PdfBackend::MuPDF:
      return "mupdf";
    case PdfBackend::Default:
      return "none";
  }
  return "unknown";
}


std::string pdf_image_uri(const std::filesystem::path& pdf_path, int image_1based) {
  std::string uri = file_uri_from_path(pdf_path.lexically_normal());
  uri += "//pdfimage:";
  uri += std::to_string(std::max(1, image_1based));
  return uri;
}

std::optional<ParsedPdfImageUri> parse_pdf_image_uri(std::string_view uri) {
  constexpr std::string_view kPipe = "//pdfimage:";
  auto pos = uri.find(kPipe);
  if (pos == std::string_view::npos) return std::nullopt;
  const auto outer = uri.substr(0, pos);
  auto path = path_from_file_uri(outer);
  if (!path) {
    // Allow plain path + pipe (non-file URI) for session strings.
    if (outer.find("://") != std::string_view::npos) return std::nullopt;
    path = std::filesystem::path(std::string(outer));
  }
  if (!is_pdf_path(*path) && !is_likely_pdf_path(*path)) return std::nullopt;
  std::string_view rest = uri.substr(pos + kPipe.size());
  if (rest.empty()) return std::nullopt;
  int image = 0;
  for (char c : rest) {
    if (c < '0' || c > '9') break;
    image = image * 10 + (c - '0');
    if (image > 1'000'000) return std::nullopt;
  }
  if (image < 1) return std::nullopt;
  ParsedPdfImageUri out;
  out.pdf_path = *path;
  out.image = image;
  return out;
}

std::optional<int> pdf_embedded_image_count(const std::filesystem::path& path) {
#if defined(THUMTOO_HAVE_MUPDF)
  return mupdf_embedded_image_count(path);
#else
  (void)path;
  return std::nullopt;
#endif
}

std::optional<PdfRaster> pdf_rasterize_embedded_image(const std::filesystem::path& path,
                                                      int image_1based, int max_edge) {
#if defined(THUMTOO_HAVE_MUPDF)
  return mupdf_rasterize_embedded_image(path, image_1based, max_edge);
#else
  (void)path;
  (void)image_1based;
  (void)max_edge;
  return std::nullopt;
#endif
}

std::optional<Size> pdf_embedded_image_size(const std::filesystem::path& path,
                                            int image_1based) {
#if defined(THUMTOO_HAVE_MUPDF)
  return mupdf_embedded_image_size(path, image_1based);
#else
  (void)path;
  (void)image_1based;
  return std::nullopt;
#endif
}

std::string pdf_page_uri(const std::filesystem::path& pdf_path, int page_1based,
                         PdfBackend backend) {
  auto uri = file_uri_from_path(pdf_path.lexically_normal());
  switch (backend) {
    case PdfBackend::Poppler:
      uri += "//poppler-page:";
      break;
    case PdfBackend::MuPDF:
      uri += "//mupdf-page:";
      break;
    case PdfBackend::Default:
    default:
      uri += "//page:";
      break;
  }
  uri += std::to_string(std::max(1, page_1based));
  return uri;
}

std::optional<ParsedPdfUri> parse_pdf_uri(std::string_view uri) {
  PdfBackend backend = PdfBackend::Default;
  std::string_view tag = kPagePipe;
  auto pipe = uri.find(kPagePipe);
  auto pop = uri.find(kPopplerPagePipe);
  auto mu = uri.find(kMupdfPagePipe);

  // Prefer the leftmost explicit/default page pipe.
  std::size_t pos = std::string_view::npos;
  auto consider = [&](std::size_t p, PdfBackend b, std::string_view t) {
    if (p == std::string_view::npos) return;
    if (pos == std::string_view::npos || p < pos) {
      pos = p;
      backend = b;
      tag = t;
    }
  };
  consider(pipe, PdfBackend::Default, kPagePipe);
  consider(pop, PdfBackend::Poppler, kPopplerPagePipe);
  consider(mu, PdfBackend::MuPDF, kMupdfPagePipe);
  if (pos == std::string_view::npos) return std::nullopt;

  const auto outer = uri.substr(0, pos);
  auto path = path_from_file_uri(outer);
  if (!path) {
    // Accept plain absolute paths (session-style …/doc.pdf//page:N).
    if (!outer.empty() && outer.front() == '/') {
      path = std::filesystem::path(std::string(outer));
    }
  }
  if (!path) return std::nullopt;
  if (!is_pdf_path(*path)) return std::nullopt;

  std::string_view rest = uri.substr(pos + tag.size());
  if (rest.empty()) return std::nullopt;
  int page = 0;
  for (char c : rest) {
    if (c < '0' || c > '9') return std::nullopt;
    page = page * 10 + (c - '0');
    if (page > 1'000'000) return std::nullopt;
  }
  if (page < 1) return std::nullopt;

  ParsedPdfUri out;
  out.pdf_path = *path;
  out.page = page;
  out.backend = backend;
  return out;
}

std::optional<int> pdf_page_count(const std::filesystem::path& path,
                                  PdfBackend backend) {
  if (pdf_resolve_backend(backend) == PdfBackend::MuPDF) {
    return mupdf_page_count(path);
  }
#if !defined(THUMTOO_HAVE_POPPLER)
  (void)path;
  return std::nullopt;
#else
  poppler::document* doc = cached_pdf_document(path);
  if (!doc) return std::nullopt;
  const int n = doc->pages();
  if (n <= 0) return std::nullopt;
  return n;
#endif
}

std::optional<Size> pdf_page_size_72dpi(const std::filesystem::path& path,
                                        int page_1based, PdfBackend backend) {
  if (pdf_resolve_backend(backend) == PdfBackend::MuPDF) {
    return mupdf_page_size_72dpi(path, page_1based);
  }
#if !defined(THUMTOO_HAVE_POPPLER)
  (void)path;
  (void)page_1based;
  return std::nullopt;
#else
  if (page_1based < 1) return std::nullopt;
  const std::string key = path.lexically_normal().string();
  if (g_tls_pdf_layout.path_key == key && g_tls_pdf_layout.page == page_1based &&
      g_tls_pdf_layout.size72.width > 0) {
    return g_tls_pdf_layout.size72;
  }
  poppler::document* doc = cached_pdf_document(path);
  if (!doc) return std::nullopt;
  poppler::page* page = cached_pdf_page(doc, key, page_1based);
  if (!page) return std::nullopt;
  const poppler::rectf box = page->page_rect(poppler::media_box);
  const int w = std::max(1, static_cast<int>(std::lround(box.width())));
  const int h = std::max(1, static_cast<int>(std::lround(box.height())));
  g_tls_pdf_layout.path_key = key;
  g_tls_pdf_layout.page = page_1based;
  g_tls_pdf_layout.size72 = Size{w, h};
  return g_tls_pdf_layout.size72;
#endif
}

std::optional<Size> pdf_page_layout_size(const std::filesystem::path& path,
                                         int page_1based, PdfBackend backend) {
  auto s72 = pdf_page_size_72dpi(path, page_1based, backend);
  if (!s72) return std::nullopt;
  // Scale media-box points to layout DPI (default 144 = 2× 72).
  const double scale = static_cast<double>(kPdfLayoutDpi) / 72.0;
  const int w = std::max(1, static_cast<int>(std::lround(s72->width * scale)));
  const int h = std::max(1, static_cast<int>(std::lround(s72->height * scale)));
  return Size{w, h};
}

std::optional<PdfRaster> pdf_rasterize_page(const std::filesystem::path& path,
                                            int page_1based, int max_edge,
                                            PdfBackend backend) {
  if (pdf_resolve_backend(backend) == PdfBackend::MuPDF) {
    return mupdf_rasterize_page(path, page_1based, max_edge);
  }
#if !defined(THUMTOO_HAVE_POPPLER)
  (void)path;
  (void)page_1based;
  (void)max_edge;
  return std::nullopt;
#else
  if (page_1based < 1) return std::nullopt;
  poppler::document* doc = cached_pdf_document(path);
  if (!doc) return std::nullopt;
  if (page_1based > doc->pages()) return std::nullopt;
  const std::string path_key = path.lexically_normal().string();
  poppler::page* page = cached_pdf_page(doc, path_key, page_1based);
  if (!page) return std::nullopt;

  const poppler::rectf box = page->page_rect(poppler::media_box);
  const double bw = std::max(1.0, box.width());
  const double bh = std::max(1.0, box.height());
  const double long_pt = std::max(bw, bh);

  // 72 dpi → 1 pixel per point. Scale up so long edge ≈ max_edge when asked.
  double dpi = 72.0;
  if (max_edge > 0 && long_pt > 0.0) {
    dpi = 72.0 * (static_cast<double>(max_edge) / long_pt);
    dpi = std::clamp(dpi, 36.0, 2400.0);
  }

  poppler::page_renderer renderer;
  renderer.set_render_hint(poppler::page_renderer::antialiasing, true);
  renderer.set_render_hint(poppler::page_renderer::text_antialiasing, true);
  poppler::image img = renderer.render_page(page, dpi, dpi);
  if (!img.is_valid()) return std::nullopt;

  const int w = img.width();
  const int h = img.height();
  if (w <= 0 || h <= 0) return std::nullopt;

  PdfRaster out;
  out.width = w;
  out.height = h;
  out.rgb.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3u);

  // poppler::image is typically format_rgb24 or format_argb32.
  const auto fmt = img.format();
  const char* src = img.const_data();
  const int bpl = img.bytes_per_row();
  for (int y = 0; y < h; ++y) {
    const auto* row = reinterpret_cast<const unsigned char*>(src + y * bpl);
    auto* dst = out.rgb.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 3u;
    if (fmt == poppler::image::format_rgb24) {
      for (int x = 0; x < w; ++x) {
        dst[x * 3 + 0] = row[x * 3 + 0];
        dst[x * 3 + 1] = row[x * 3 + 1];
        dst[x * 3 + 2] = row[x * 3 + 2];
      }
    } else if (fmt == poppler::image::format_argb32) {
      for (int x = 0; x < w; ++x) {
        // Byte order: typically BGRA or ARGB depending on platform; poppler
        // documents ARGB32 as 0xAARRGGBB in native endian for data().
        const unsigned char* p = row + x * 4;
        // poppler ARGB32 is native-endian 0xAARRGGBB; on little-endian memory
        // that is B,G,R,A.
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        dst[x * 3 + 0] = p[1];
        dst[x * 3 + 1] = p[2];
        dst[x * 3 + 2] = p[3];
#else
        dst[x * 3 + 0] = p[2];
        dst[x * 3 + 1] = p[1];
        dst[x * 3 + 2] = p[0];
#endif
      }
    } else if (fmt == poppler::image::format_mono) {
      for (int x = 0; x < w; ++x) {
        const unsigned char bit = (row[x / 8] >> (7 - (x % 8))) & 1;
        const unsigned char v = bit ? 0 : 255;
        dst[x * 3 + 0] = v;
        dst[x * 3 + 1] = v;
        dst[x * 3 + 2] = v;
      }
    } else {
      // Fallback: treat as RGB24-ish first three bytes per pixel if possible.
      const int spp = std::max(1, bpl / std::max(1, w));
      for (int x = 0; x < w; ++x) {
        const unsigned char* p = row + x * spp;
        dst[x * 3 + 0] = p[0];
        dst[x * 3 + 1] = spp > 1 ? p[1] : p[0];
        dst[x * 3 + 2] = spp > 2 ? p[2] : p[0];
      }
    }
  }
  return out;
#endif
}


Size pdf_page_size_at_scale(Size layout, int scale) {
  if (layout.width <= 0 || layout.height <= 0) return Size{0, 0};
  if (scale == 0) return layout;
  // factor = 2^{-scale}: positive scale shrinks, negative grows.
  const double factor = std::ldexp(1.0, -scale);
  const int w = std::max(1, static_cast<int>(std::lround(layout.width * factor)));
  const int h = std::max(1, static_cast<int>(std::lround(layout.height * factor)));
  return Size{w, h};
}

double pdf_dpi_for_scale(int scale) {
  return static_cast<double>(kPdfLayoutDpi) * std::ldexp(1.0, -scale);
}

namespace {

#if defined(THUMTOO_HAVE_POPPLER)
std::optional<PdfRaster> image_to_rgb(const poppler::image& img) {
  if (!img.is_valid()) return std::nullopt;
  const int w = img.width();
  const int h = img.height();
  if (w <= 0 || h <= 0) return std::nullopt;

  PdfRaster out;
  out.width = w;
  out.height = h;
  out.rgb.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3u);

  const auto fmt = img.format();
  const char* src = img.const_data();
  const int bpl = img.bytes_per_row();
  for (int y = 0; y < h; ++y) {
    const auto* row = reinterpret_cast<const unsigned char*>(src + y * bpl);
    auto* dst = out.rgb.data() + static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(w) * 3u;
    if (fmt == poppler::image::format_rgb24) {
      for (int x = 0; x < w; ++x) {
        dst[x * 3 + 0] = row[x * 3 + 0];
        dst[x * 3 + 1] = row[x * 3 + 1];
        dst[x * 3 + 2] = row[x * 3 + 2];
      }
    } else if (fmt == poppler::image::format_argb32) {
      for (int x = 0; x < w; ++x) {
        const unsigned char* p = row + x * 4;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        dst[x * 3 + 0] = p[1];
        dst[x * 3 + 1] = p[2];
        dst[x * 3 + 2] = p[3];
#else
        dst[x * 3 + 0] = p[2];
        dst[x * 3 + 1] = p[1];
        dst[x * 3 + 2] = p[0];
#endif
      }
    } else if (fmt == poppler::image::format_mono) {
      for (int x = 0; x < w; ++x) {
        const unsigned char bit = (row[x / 8] >> (7 - (x % 8))) & 1;
        const unsigned char v = bit ? 0 : 255;
        dst[x * 3 + 0] = v;
        dst[x * 3 + 1] = v;
        dst[x * 3 + 2] = v;
      }
    } else {
      const int spp = std::max(1, bpl / std::max(1, w));
      for (int x = 0; x < w; ++x) {
        const unsigned char* p = row + x * spp;
        dst[x * 3 + 0] = p[0];
        dst[x * 3 + 1] = spp > 1 ? p[1] : p[0];
        dst[x * 3 + 2] = spp > 2 ? p[2] : p[0];
      }
    }
  }
  return out;
}
#endif

}  // namespace


PdfPageContentStats pdf_page_content_stats(const std::filesystem::path& path,
                                           int page_1based, PdfBackend backend) {
  if (pdf_resolve_backend(backend) == PdfBackend::MuPDF) {
    return mupdf_page_content_stats(path, page_1based);
  }
  PdfPageContentStats st;
#if !defined(THUMTOO_HAVE_POPPLER)
  (void)path;
  (void)page_1based;
  return st;
#else
  if (page_1based < 1) return st;
  struct TlsStats {
    std::string path_key;
    int page = 0;
    PdfPageContentStats st{};
    bool valid = false;
  };
  static thread_local TlsStats tls;
  const std::string key = path.lexically_normal().string();
  if (tls.valid && tls.path_key == key && tls.page == page_1based) {
    return tls.st;
  }
  poppler::document* doc = cached_pdf_document(path);
  if (!doc) return st;
  if (!doc) return st;
  poppler::page* page = cached_pdf_page(doc, key, page_1based);
  if (!page) return st;

  const poppler::rectf box = page->page_rect(poppler::media_box);
  const double page_w = std::max(1.0, box.width());
  const double page_h = std::max(1.0, box.height());
  const double page_area = page_w * page_h;

  // Text density (always available via poppler-cpp).
  try {
    // Full-page text length (UTF-16 code units in ustring::size).
    const poppler::ustring all = page->text(page->page_rect(poppler::media_box));
    st.text_chars = static_cast<int>(all.size());
  } catch (...) {
    try {
      const auto boxes = page->text_list();
      for (const auto& tb : boxes) {
        st.text_chars += static_cast<int>(tb.text().size());
      }
    } catch (...) {
    }
  }

  // Image coverage needs poppler-glib (not in poppler-cpp). Without it,
  // image_coverage stays -1 and we fall back to the sparse-text heuristic.
#if defined(THUMTOO_HAVE_POPPLER_GLIB)
  {
    // GLib document is separate from cpp; open once per path in TLS.
    struct TlsGlibPdf {
      std::string path_key;
      PopplerDocument* doc = nullptr;
    };
    static thread_local TlsGlibPdf glib_doc;
    if (glib_doc.path_key != key) {
      if (glib_doc.doc) {
        g_object_unref(glib_doc.doc);
        glib_doc.doc = nullptr;
      }
      glib_doc.path_key = key;
      GError* err = nullptr;
      std::string uri = "file://" + path.lexically_normal().string();
      glib_doc.doc = poppler_document_new_from_file(uri.c_str(), nullptr, &err);
      if (err) {
        g_error_free(err);
        glib_doc.doc = nullptr;
      }
    }
    if (glib_doc.doc) {
      PopplerPage* gpage =
          poppler_document_get_page(glib_doc.doc, page_1based - 1);
      if (gpage) {
        GList* maps = poppler_page_get_image_mapping(gpage);
        double covered = 0.0;
        for (GList* l = maps; l; l = l->next) {
          auto* m = static_cast<PopplerImageMapping*>(l->data);
          if (!m) continue;
          ++st.image_count;
          const double iw = std::abs(m->area.x2 - m->area.x1);
          const double ih = std::abs(m->area.y2 - m->area.y1);
          covered += iw * ih;
        }
        poppler_page_free_image_mapping(maps);
        g_object_unref(gpage);
        st.image_coverage = std::min(1.0, covered / page_area);
      }
    }
  }
#endif

  const double text_density = static_cast<double>(st.text_chars) / page_area;
  if (st.image_coverage >= 0.0) {
    st.image_heavy = st.image_coverage >= kPdfImageHeavyCoverage;
  } else {
    // No image map: sparse/no text → treat as scanned/photo page.
    st.image_heavy = text_density < kPdfSparseTextPerPoint2;
  }
  // Single near-full-page image is the classic scan even with a caption.
  if (st.image_count == 1 && st.image_coverage >= 0.35) {
    st.image_heavy = true;
  }
  tls.path_key = key;
  tls.page = page_1based;
  tls.st = st;
  tls.valid = true;
  return st;
#endif
}

bool pdf_page_allows_live_tiles(const std::filesystem::path& path,
                                int page_1based, PdfBackend backend) {
  return !pdf_page_content_stats(path, page_1based, backend).image_heavy;
}

std::optional<PdfRaster> pdf_rasterize_page_region(
    const std::filesystem::path& path, int page_1based, double dpi, int px,
    int py, int pw, int ph, PdfBackend backend) {
  if (pdf_resolve_backend(backend) == PdfBackend::MuPDF) {
    return mupdf_rasterize_page_region(path, page_1based, dpi, px, py, pw, ph);
  }
#if !defined(THUMTOO_HAVE_POPPLER)
  (void)path;
  (void)page_1based;
  (void)dpi;
  (void)px;
  (void)py;
  (void)pw;
  (void)ph;
  return std::nullopt;
#else
  if (page_1based < 1 || pw <= 0 || ph <= 0 || dpi <= 0.0) return std::nullopt;
  poppler::document* doc = cached_pdf_document(path);
  if (!doc) return std::nullopt;
  const std::string key = path.lexically_normal().string();
  poppler::page* page = cached_pdf_page(doc, key, page_1based);
  if (!page) return std::nullopt;

  poppler::page_renderer renderer;
  renderer.set_render_hint(poppler::page_renderer::antialiasing, true);
  renderer.set_render_hint(poppler::page_renderer::text_antialiasing, true);
  // x,y,w,h are pixel crop on the full page at the given DPI.
  poppler::image img =
      renderer.render_page(page, dpi, dpi, px, py, pw, ph);
  return image_to_rgb(img);
#endif
}

std::optional<PdfRaster> pdf_render_tile_cell(const std::filesystem::path& path,
                                               int page_1based, int scale, int x,
                                               int y, PdfBackend backend) {
  if (pdf_resolve_backend(backend) == PdfBackend::MuPDF) {
    return mupdf_render_tile_cell(path, page_1based, scale, x, y);
  }
#if !defined(THUMTOO_HAVE_POPPLER)
  (void)path;
  (void)page_1based;
  (void)scale;
  (void)x;
  (void)y;
  (void)backend;
  return std::nullopt;
#else
  if (x < 0 || y < 0) return std::nullopt;
  // Image-heavy (scanned) pages: refuse live finer-than-layout tiles. Region
  // render re-decodes large JPEG XObjects per cell; vector pages stay live.
  if (scale < kPdfMinLiveTileScaleImageHeavy &&
      !pdf_page_allows_live_tiles(path, page_1based, backend)) {
    return std::nullopt;
  }
  auto layout = pdf_page_layout_size(path, page_1based, backend);
  if (!layout || layout->width <= 0 || layout->height <= 0) return std::nullopt;

  const Size full = pdf_page_size_at_scale(*layout, scale);
  const int left = x * kTileSize;
  const int top = y * kTileSize;
  if (left >= full.width || top >= full.height) return std::nullopt;
  const int tw = std::min(kTileSize, full.width - left);
  const int th = std::min(kTileSize, full.height - top);
  if (tw <= 0 || th <= 0) return std::nullopt;

  const double dpi = pdf_dpi_for_scale(scale);
  const int full_edge = std::max(full.width, full.height);

  // Full-page cache only for coarse/layout scales (scale >= 0). Deep live zoom
  // would allocate enormous buffers; vector pages region-render fine without it.
  std::optional<PdfRaster> cell_raster;
  const std::string path_key = path.lexically_normal().string();
  if (scale >= 0 && full_edge > 0 && full_edge <= kPdfPageRasterCacheMaxEdge) {
    bool cache_hit =
        g_tls_pdf_page_raster.path_key == path_key &&
        g_tls_pdf_page_raster.page == page_1based &&
        std::abs(g_tls_pdf_page_raster.dpi - dpi) < 0.01 &&
        g_tls_pdf_page_raster.raster.width == full.width &&
        g_tls_pdf_page_raster.raster.height == full.height &&
        !g_tls_pdf_page_raster.raster.rgb.empty();
    if (!cache_hit) {
      auto page_raster = pdf_rasterize_page(path, page_1based, full_edge);
      if (page_raster && page_raster->width == full.width &&
          page_raster->height == full.height && !page_raster->rgb.empty()) {
        g_tls_pdf_page_raster.path_key = path_key;
        g_tls_pdf_page_raster.page = page_1based;
        g_tls_pdf_page_raster.dpi = dpi;
        g_tls_pdf_page_raster.raster = *page_raster;
        cache_hit = true;
      }
    }
    if (cache_hit) {
      auto const& page_raster = g_tls_pdf_page_raster.raster;
      PdfRaster cropped;
      cropped.width = tw;
      cropped.height = th;
      cropped.rgb.resize(static_cast<std::size_t>(tw) *
                         static_cast<std::size_t>(th) * 3u);
      for (int row = 0; row < th; ++row) {
        const auto* src =
            page_raster.rgb.data() +
            (static_cast<std::size_t>(top + row) *
                 static_cast<std::size_t>(page_raster.width) +
             static_cast<std::size_t>(left)) *
                3u;
        auto* dst = cropped.rgb.data() +
                    static_cast<std::size_t>(row) *
                        static_cast<std::size_t>(tw) * 3u;
        std::memcpy(dst, src, static_cast<std::size_t>(tw) * 3u);
      }
      cell_raster = std::move(cropped);
    }
  }

  if (!cell_raster) {
    cell_raster =
        pdf_rasterize_page_region(path, page_1based, dpi, left, top, tw, th);
  }

  if (cell_raster && cell_raster->width > 0 && cell_raster->height > 0) {
    const int tol = 2;
    if (std::abs(cell_raster->width - tw) > tol ||
        std::abs(cell_raster->height - th) > tol) {
      cell_raster.reset();
    }
  }

  if (!cell_raster || cell_raster->rgb.empty()) {
    auto page_raster = pdf_rasterize_page(path, page_1based, full_edge);
    if (!page_raster || page_raster->rgb.empty() ||
        page_raster->width <= 0 || page_raster->height <= 0) {
      return std::nullopt;
    }
    const double sx = static_cast<double>(page_raster->width) /
                      static_cast<double>(full.width);
    const double sy = static_cast<double>(page_raster->height) /
                      static_cast<double>(full.height);
    int const px = std::clamp(static_cast<int>(std::lround(left * sx)), 0,
                              std::max(0, page_raster->width - 1));
    int const py = std::clamp(static_cast<int>(std::lround(top * sy)), 0,
                              std::max(0, page_raster->height - 1));
    int const pw = std::clamp(static_cast<int>(std::lround(tw * sx)), 1,
                              page_raster->width - px);
    int const ph = std::clamp(static_cast<int>(std::lround(th * sy)), 1,
                              page_raster->height - py);

    PdfRaster cropped;
    cropped.width = pw;
    cropped.height = ph;
    cropped.rgb.resize(static_cast<std::size_t>(pw) *
                       static_cast<std::size_t>(ph) * 3u);
    for (int row = 0; row < ph; ++row) {
      const auto* src =
          page_raster->rgb.data() +
          (static_cast<std::size_t>(py + row) *
               static_cast<std::size_t>(page_raster->width) +
           static_cast<std::size_t>(px)) *
              3u;
      auto* dst = cropped.rgb.data() +
                  static_cast<std::size_t>(row) *
                      static_cast<std::size_t>(pw) * 3u;
      std::memcpy(dst, src, static_cast<std::size_t>(pw) * 3u);
    }
    cell_raster = std::move(cropped);
  }

  if (!cell_raster || cell_raster->rgb.empty()) return std::nullopt;
  return cell_raster;
#endif
}

std::optional<TileBlob> pdf_build_tile_cell(const std::filesystem::path& path,
                                            int page_1based, int scale, int x,
                                            int y, int jpeg_quality,
                                            PdfBackend backend) {
  auto raster = pdf_render_tile_cell(path, page_1based, scale, x, y, backend);
  if (!raster || raster->rgb.empty()) return std::nullopt;
  return encode_tile_cell_rgb(raster->rgb.data(), raster->width, raster->height,
                              scale, x, y, jpeg_quality);
}

std::optional<PageTextLayer> pdf_page_text_layer(const std::filesystem::path& path,
                                                 int page_1based,
                                                 PdfBackend backend) {
  // Text/outline always prefer MuPDF when built — even if the page *raster*
  // URI is //poppler-page:N. Poppler has no text-layer path here yet.
  (void)backend;
#if defined(THUMTOO_HAVE_MUPDF)
  return mupdf_page_text_layer(path, page_1based);
#else
  (void)path;
  (void)page_1based;
  return std::nullopt;
#endif
}

std::optional<DocumentOutline> pdf_document_outline(const std::filesystem::path& path,
                                                    PdfBackend backend) {
  (void)backend;
#if defined(THUMTOO_HAVE_MUPDF)
  return mupdf_document_outline(path);
#else
  (void)path;
  return std::nullopt;
#endif
}

}  // namespace thumtoo
