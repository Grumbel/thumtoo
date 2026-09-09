// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/uri.hpp"
#include "thumtoo/network.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_fails = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    ++g_fails;
  }
}

}  // namespace

int main() {
  using namespace thumtoo;

  const auto file = file_uri_from_path("/tmp/photo.jpg");
  expect(file == "file:///tmp/photo.jpg", "file uri");
  expect(path_from_file_uri(file).value_or("") == "/tmp/photo.jpg", "path roundtrip");

  const auto nested = with_archive_member(file_uri_from_path("/tmp/book.zip"), "a/b.jpg");
  expect(is_archive_uri(nested), "archive detect");
  auto loc = parse_location(nested);
  expect(loc.has_value(), "parse nested");
  expect(loc->scheme == UriScheme::File, "scheme file");
  expect(loc->pipes.size() == 1 && loc->pipes[0].kind == LocationPipeKind::ArchiveMember,
         "archive member pipe");
  expect(loc->pipes[0].value == "a/b.jpg", "member path");
  expect(format_location(*loc) == nested, "format nested");

  const auto page = with_pdf_page(file_uri_from_path("/tmp/doc.pdf"), 3);
  expect(is_pdf_page_uri(page), "page detect");
  auto ploc = parse_location(page);
  expect(ploc && ploc->pipes.size() == 1 && ploc->pipes[0].kind == LocationPipeKind::PdfPage,
         "page pipe");
  expect(ploc->pipes[0].value == "3", "page number");
  expect(path_from_file_uri(page).value_or("") == "/tmp/doc.pdf", "path strips page");

  const auto page_pop = with_pdf_page_poppler(file_uri_from_path("/tmp/doc.pdf"), 2);
  expect(is_pdf_page_uri(page_pop), "poppler-page detect");
  auto poploc = parse_location(page_pop);
  expect(poploc && poploc->pipes.size() == 1 &&
             poploc->pipes[0].kind == LocationPipeKind::PdfPagePoppler,
         "poppler page pipe");
  expect(format_location(*poploc) == page_pop, "format poppler-page");

  const auto page_mu = with_pdf_page_mupdf(file_uri_from_path("/tmp/doc.pdf"), 4);
  expect(is_pdf_page_uri(page_mu), "mupdf-page detect");
  auto muloc = parse_location(page_mu);
  expect(muloc && muloc->pipes.size() == 1 &&
             muloc->pipes[0].kind == LocationPipeKind::PdfPageMupdf,
         "mupdf page pipe");
  expect(format_location(*muloc) == page_mu, "format mupdf-page");
  {
    auto img = with_pdf_image(file_uri_from_path("/tmp/scan.pdf"), 3);
    expect(is_pdf_image_uri(img), "pdfimage detect");
    auto loc = parse_location(img);
    expect(loc && !loc->pipes.empty() &&
               loc->pipes.back().kind == LocationPipeKind::PdfImage &&
               loc->pipes.back().value == "3",
           "pdfimage pipe");
  }

  {
    const auto layout = default_epub_layout();
    const auto ep =
        with_pdf_page(with_epub_layout(file_uri_from_path("/tmp/book.epub"), layout), 7);
    expect(is_epub_layout_uri(ep), "epub layout detect");
    expect(is_pdf_page_uri(ep), "epub still has page pipe");
    auto eloc = parse_location(ep);
    expect(eloc && eloc->pipes.size() == 2, "epub two pipes");
    expect(eloc && eloc->pipes[0].kind == LocationPipeKind::EpubLayout, "epub layout pipe");
    expect(eloc && eloc->pipes[1].kind == LocationPipeKind::PdfPage, "epub page pipe");
    expect(format_location(*eloc) == ep, "format epub uri");
    auto parsed = parse_epub_layout_params(eloc->pipes[0].value);
    expect(parsed.width_px == layout.width_px && parsed.height_px == layout.height_px &&
               parsed.fs_pt == layout.fs_pt && parsed.mt_px == 0 && parsed.ml_px == 0,
           "epub layout params roundtrip");
    EpubLayout with_m = layout;
    with_m.mt_px = 24;
    with_m.mr_px = 16;
    with_m.mb_px = 24;
    with_m.ml_px = 16;
    auto formatted = format_epub_layout_params(with_m);
    expect(formatted.find("mt=24") != std::string::npos &&
               formatted.find("ml=16") != std::string::npos,
           "epub margins in format");
    auto parsed_m = parse_epub_layout_params(formatted);
    expect(parsed_m.mt_px == 24 && parsed_m.mr_px == 16 && parsed_m.mb_px == 24 &&
               parsed_m.ml_px == 16,
           "epub margin params roundtrip");
    EpubLayout rich = layout;
    rich.lh_percent = 150;
    rich.cols = 2;
    rich.cgap_px = 48;
    rich.align = EpubAlign::Justify;
    rich.font = EpubFontFamily::Serif;
    rich.theme = EpubTheme::Night;
    rich.use_document_css = false;
    auto fr = format_epub_layout_params(rich);
    expect(fr.find("lh=150") != std::string::npos && fr.find("cols=2") != std::string::npos &&
               fr.find("cgap=48") != std::string::npos && fr.find("align=justify") != std::string::npos &&
               fr.find("ff=serif") != std::string::npos && fr.find("theme=night") != std::string::npos &&
               fr.find("pubcss=0") != std::string::npos,
           "epub reader keys in format");
    auto pr = parse_epub_layout_params(fr);
    expect(pr.lh_percent == 150 && pr.cols == 2 && pr.cgap_px == 48 &&
               pr.align == EpubAlign::Justify && pr.font == EpubFontFamily::Serif &&
               pr.theme == EpubTheme::Night && !pr.use_document_css,
           "epub reader keys roundtrip");
    // Default layout includes lh=140
    auto def = format_epub_layout_params(default_epub_layout());
    expect(def.find("lh=140") != std::string::npos, "default emits lh");
    // Key order must not affect canonical form / cache identity
    auto scrambled = parse_epub_layout_params("fs=15,lh=140,w=900,h=1350");
    auto ordered = parse_epub_layout_params("w=900,h=1350,fs=15,lh=140");
    expect(format_epub_layout_params(scrambled) == format_epub_layout_params(ordered),
           "epub layout key order normalized");
    auto loc_sc = parse_location(
        "file:///tmp/book.epub//epub:fs=15,w=900,h=1350,lh=140//page:2");
    expect(loc_sc && !loc_sc->pipes.empty() &&
               loc_sc->pipes[0].kind == LocationPipeKind::EpubLayout &&
               loc_sc->pipes[0].value == format_epub_layout_params(ordered),
           "parse_location canonicalizes epub pipe");
  }

  // PDF inside archive
  const auto deep = with_pdf_page(
      with_archive_member(file_uri_from_path("/tmp/outer.zip"), "docs/x.pdf"), 2);
  auto dloc = parse_location(deep);
  expect(dloc && dloc->pipes.size() == 2, "two pipes");
  expect(dloc->pipes[0].kind == LocationPipeKind::ArchiveMember, "outer archive");
  expect(dloc->pipes[1].kind == LocationPipeKind::PdfPage, "inner page");
  expect(format_location(*dloc) == deep, "format deep");

  expect(is_http_uri("https://example.com/a.jpg"), "https");
  expect(!is_http_uri(file), "file not http");

  const auto cid = content_id_uri_from_sha256_hex("abc");
  expect(cid == "sha256:abc", "content id");
  expect(is_content_id_uri(cid), "content detect");
  expect(content_id_hex(cid).value_or("") == "abc", "hex");

  auto http_loc = parse_location("https://cdn.example/x.jpg");
  expect(http_loc && http_loc->scheme == UriScheme::Https, "parse https");
  // Availability depends on build; just exercise the symbol.
  (void)http_fetch_available();

  if (g_fails) {
    std::cerr << g_fails << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "test_uri: ok\n";
  return EXIT_SUCCESS;
}
