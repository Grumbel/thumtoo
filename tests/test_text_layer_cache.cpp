// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/database.hpp"
#include "thumtoo/text.hpp"

#include <filesystem>
#include <iostream>

namespace {

int g_failures = 0;
void expect(bool c, const char* m) {
  if (!c) {
    std::cerr << "FAIL: " << m << "\n";
    ++g_failures;
  }
}

}  // namespace

int main() {
  thumtoo::PageTextLayer layer;
  layer.page_1based = 3;
  layer.layout_key = "w=800,h=1200,fs=12";
  layer.page_bounds = {0, 0, 100, 200};
  thumtoo::TextRegion tr;
  tr.role = thumtoo::TextRegionRole::Text;
  tr.text = "Hello";
  tr.bbox = {10, 20, 50, 40};
  layer.regions.push_back(tr);
  thumtoo::TextRegion lr;
  lr.role = thumtoo::TextRegionRole::Link;
  lr.bbox = {1, 2, 3, 4};
  lr.target.kind = thumtoo::TextLinkTargetKind::Uri;
  lr.target.uri = "https://example.com";
  layer.regions.push_back(lr);

  auto bytes = thumtoo::serialize_page_text_layer(layer);
  expect(!bytes.empty(), "serialized non-empty");
  auto back = thumtoo::deserialize_page_text_layer(bytes);
  expect(back.has_value(), "deserialize ok");
  if (back) {
    expect(back->page_1based == 3, "page");
    expect(back->layout_key == layer.layout_key, "layout_key");
    expect(back->regions.size() == 2, "2 regions");
    expect(back->regions[0].text == "Hello", "text");
    expect(back->regions[1].target.uri == "https://example.com", "link uri");
  }

  thumtoo::DocumentOutline outline;
  outline.items.push_back({1, "Chapter", 5, ""});
  auto obytes = thumtoo::serialize_document_outline(outline);
  auto o2 = thumtoo::deserialize_document_outline(obytes);
  expect(o2 && o2->items.size() == 1 && o2->items[0].title == "Chapter", "outline");

  // Database round-trip
  const auto root = std::filesystem::temp_directory_path() / "thumtoo_text_cache_test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  auto db = thumtoo::Database::open(root);
  expect(db.schema_version() >= 3, "schema >= 3");
  db.upsert_text_layer("sha256:abc", 3, layer.layout_key, 0, 0, 100, 200, bytes);
  auto found = db.find_text_layer("sha256:abc", 3, layer.layout_key);
  expect(found.has_value() && *found == bytes, "db text layer roundtrip");
  db.upsert_document_outline("sha256:abc", "", obytes);
  auto ofound = db.find_document_outline("sha256:abc", "");
  expect(ofound.has_value() && *ofound == obytes, "db outline roundtrip");

  std::filesystem::remove_all(root);

  if (g_failures) {
    std::cerr << g_failures << " failures\n";
    return 1;
  }
  std::cout << "OK test_text_layer_cache\n";
  return 0;
}
