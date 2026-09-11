// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/text.hpp"

#include "thumtoo/djvu.hpp"
#include "thumtoo/epub.hpp"
#include "thumtoo/pdf.hpp"
#include "thumtoo/uri.hpp"

#include <charconv>
#include <cstring>
#include <vector>
#include <cstdint>
#include <sstream>
#include <string_view>

namespace thumtoo {
namespace {

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

void append_f64(std::vector<std::uint8_t>& out, double v) {
  static_assert(sizeof(double) == 8);
  const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
  out.insert(out.end(), p, p + 8);
}

void append_str(std::vector<std::uint8_t>& out, std::string_view s) {
  append_u32(out, static_cast<std::uint32_t>(s.size()));
  out.insert(out.end(), s.begin(), s.end());
}

bool read_u32(const std::uint8_t*& p, const std::uint8_t* end, std::uint32_t& v) {
  if (end - p < 4) return false;
  v = static_cast<std::uint32_t>(p[0]) |
      (static_cast<std::uint32_t>(p[1]) << 8) |
      (static_cast<std::uint32_t>(p[2]) << 16) |
      (static_cast<std::uint32_t>(p[3]) << 24);
  p += 4;
  return true;
}

bool read_f64(const std::uint8_t*& p, const std::uint8_t* end, double& v) {
  if (end - p < 8) return false;
  std::memcpy(&v, p, 8);
  p += 8;
  return true;
}

bool read_str(const std::uint8_t*& p, const std::uint8_t* end, std::string& s) {
  std::uint32_t n = 0;
  if (!read_u32(p, end, n)) return false;
  if (n > static_cast<std::uint32_t>(end - p)) return false;
  s.assign(reinterpret_cast<const char*>(p), n);
  p += n;
  return true;
}

constexpr std::uint32_t kLayerMagic = 0x334C5454;  // "TTL3" — valid UTF-8 text extract
constexpr std::uint32_t kOutlineMagic = 0x324F5454; // "TTO2" — spine path → page

}  // namespace

std::vector<std::uint8_t> serialize_page_text_layer(const PageTextLayer& layer) {
  std::vector<std::uint8_t> out;
  out.reserve(64 + layer.regions.size() * 64);
  append_u32(out, kLayerMagic);
  append_u32(out, static_cast<std::uint32_t>(layer.page_1based));
  append_str(out, layer.layout_key);
  append_f64(out, layer.page_bounds.x0);
  append_f64(out, layer.page_bounds.y0);
  append_f64(out, layer.page_bounds.x1);
  append_f64(out, layer.page_bounds.y1);
  append_u32(out, static_cast<std::uint32_t>(layer.regions.size()));
  for (const auto& r : layer.regions) {
    out.push_back(static_cast<std::uint8_t>(r.role));
    append_f64(out, r.bbox.x0);
    append_f64(out, r.bbox.y0);
    append_f64(out, r.bbox.x1);
    append_f64(out, r.bbox.y1);
    append_str(out, r.text);
    out.push_back(static_cast<std::uint8_t>(r.target.kind));
    append_u32(out, static_cast<std::uint32_t>(r.target.page_1based));
    append_f64(out, r.target.x);
    append_f64(out, r.target.y);
    append_str(out, r.target.uri);
  }
  return out;
}

std::optional<PageTextLayer> deserialize_page_text_layer(
    const std::vector<std::uint8_t>& bytes) {
  const std::uint8_t* p = bytes.data();
  const std::uint8_t* end = p + bytes.size();
  std::uint32_t magic = 0;
  if (!read_u32(p, end, magic) || magic != kLayerMagic) return std::nullopt;

  PageTextLayer layer;
  std::uint32_t page = 0;
  if (!read_u32(p, end, page)) return std::nullopt;
  layer.page_1based = static_cast<int>(page);
  if (!read_str(p, end, layer.layout_key)) return std::nullopt;
  if (!read_f64(p, end, layer.page_bounds.x0)) return std::nullopt;
  if (!read_f64(p, end, layer.page_bounds.y0)) return std::nullopt;
  if (!read_f64(p, end, layer.page_bounds.x1)) return std::nullopt;
  if (!read_f64(p, end, layer.page_bounds.y1)) return std::nullopt;
  std::uint32_t n = 0;
  if (!read_u32(p, end, n)) return std::nullopt;
  layer.regions.reserve(n);
  for (std::uint32_t i = 0; i < n; ++i) {
    if (p >= end) return std::nullopt;
    TextRegion r;
    r.role = static_cast<TextRegionRole>(*p++);
    if (!read_f64(p, end, r.bbox.x0)) return std::nullopt;
    if (!read_f64(p, end, r.bbox.y0)) return std::nullopt;
    if (!read_f64(p, end, r.bbox.x1)) return std::nullopt;
    if (!read_f64(p, end, r.bbox.y1)) return std::nullopt;
    if (!read_str(p, end, r.text)) return std::nullopt;
    if (p >= end) return std::nullopt;
    r.target.kind = static_cast<TextLinkTargetKind>(*p++);
    std::uint32_t tp = 0;
    if (!read_u32(p, end, tp)) return std::nullopt;
    r.target.page_1based = static_cast<int>(tp);
    if (!read_f64(p, end, r.target.x)) return std::nullopt;
    if (!read_f64(p, end, r.target.y)) return std::nullopt;
    if (!read_str(p, end, r.target.uri)) return std::nullopt;
    layer.regions.push_back(std::move(r));
  }
  return layer;
}

std::vector<std::uint8_t> serialize_document_outline(const DocumentOutline& outline) {
  std::vector<std::uint8_t> out;
  append_u32(out, kOutlineMagic);
  append_u32(out, static_cast<std::uint32_t>(outline.items.size()));
  for (const auto& it : outline.items) {
    append_u32(out, static_cast<std::uint32_t>(it.level));
    append_str(out, it.title);
    append_u32(out, static_cast<std::uint32_t>(it.page_1based));
    append_str(out, it.uri);
  }
  return out;
}

std::optional<DocumentOutline> deserialize_document_outline(
    const std::vector<std::uint8_t>& bytes) {
  const std::uint8_t* p = bytes.data();
  const std::uint8_t* end = p + bytes.size();
  std::uint32_t magic = 0;
  if (!read_u32(p, end, magic) || magic != kOutlineMagic) return std::nullopt;
  std::uint32_t n = 0;
  if (!read_u32(p, end, n)) return std::nullopt;
  DocumentOutline out;
  out.items.reserve(n);
  for (std::uint32_t i = 0; i < n; ++i) {
    OutlineItem it;
    std::uint32_t level = 0, page = 0;
    if (!read_u32(p, end, level)) return std::nullopt;
    it.level = static_cast<int>(level);
    if (!read_str(p, end, it.title)) return std::nullopt;
    if (!read_u32(p, end, page)) return std::nullopt;
    it.page_1based = static_cast<int>(page);
    if (!read_str(p, end, it.uri)) return std::nullopt;
    out.items.push_back(std::move(it));
  }
  return out;
}

std::optional<PageTextLayer> extract_page_text_layer(std::string_view uri) {
  if (auto epub = parse_epub_uri(uri)) {
    return epub_page_text_layer(epub->epub_path, epub->page, epub->layout);
  }
  if (auto djvu = parse_djvu_uri(uri)) {
    if (is_likely_djvu_path(djvu->djvu_path)) {
      return djvu_page_text_layer(djvu->djvu_path, djvu->page);
    }
  }
  if (auto pdf = parse_pdf_uri(uri)) {
    return pdf_page_text_layer(pdf->pdf_path, pdf->page, pdf->backend);
  }
  return std::nullopt;
}

std::optional<DocumentOutline> extract_document_outline(std::string_view uri) {
  if (auto epub = parse_epub_uri(uri)) {
    return epub_document_outline(epub->epub_path, epub->layout);
  }
  if (auto djvu = parse_djvu_uri(uri)) {
    if (is_likely_djvu_path(djvu->djvu_path)) {
      return djvu_document_outline(djvu->djvu_path);
    }
  }
  if (auto pdf = parse_pdf_uri(uri)) {
    return pdf_document_outline(pdf->pdf_path, pdf->backend);
  }
  // Bare file path?
  return std::nullopt;
}

}  // namespace thumtoo
