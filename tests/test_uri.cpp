// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/uri.hpp"

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

  if (g_fails) {
    std::cerr << g_fails << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "test_uri: ok\n";
  return EXIT_SUCCESS;
}
