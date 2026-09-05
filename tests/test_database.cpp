// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/constants.hpp"
#include "thumtoo/database.hpp"
#include "thumtoo/status.hpp"
#include "thumtoo/archive.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    ++g_failures;
  }
}

fs::path temp_cache() {
  auto base = fs::temp_directory_path() / "thumtoo-test";
  fs::create_directories(base);
  auto dir = base / std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count());
  fs::create_directories(dir);
  return dir;
}

}  // namespace

int main() {
  using namespace thumtoo;
  const auto root = temp_cache();

  {
    auto db = Database::open(root);
    expect(db.schema_version() == kSchemaVersion, "schema_version == 1");
    expect(db.count_content() == 0, "empty content");
    expect(db.count_locators() == 0, "empty locators");

    auto edges = db.meta_get(kSchemaMetaLadderEdgesKey);
    expect(edges.has_value(), "ladder_edges present");
    expect(edges && edges->find("128") != std::string::npos, "ladder has 128");

    Database::ContentRow c;
    c.content_id = "prov:00000000-0000-4000-8000-000000000001";
    c.width = 1920;
    c.height = 1080;
    c.format = "jpeg";
    c.status = ContentStatus::Ready;
    db.upsert_content(c);

    Database::LocatorRow loc;
    loc.uri = "file:///tmp/example.jpg";
    loc.content_id = c.content_id;
    loc.outer_path = "/tmp/example.jpg";
    loc.size = 12345;
    db.upsert_locator(loc);

    expect(db.count_content() == 1, "one content");
    expect(db.count_locators() == 1, "one locator");

    auto contents = db.list_content();
    expect(contents.size() == 1, "list content size");
    expect(contents[0].status == ContentStatus::Ready, "status ready");
    expect(contents[0].width && *contents[0].width == 1920, "width 1920");
  }

  // Re-open same DB
  {
    auto db = Database::open(root);
    expect(db.schema_version() == kSchemaVersion, "reopen schema");
    expect(db.count_content() == 1, "persisted content");
    expect(db.count_locators() == 1, "persisted locator");
  }

  {
    auto uri = archive_uri("/tmp/book.zip", "inner/a.jpg");
    expect(uri.find("//archive:inner/a.jpg") != std::string::npos, "archive uri member");
    auto root_uri = archive_uri("/tmp/book.zip");
    expect(root_uri.ends_with("//archive"), "archive root uri");
    expect(!read_archive_toc("/no/such/archive.zip").has_value(), "missing archive");
  }

  {
    auto db = Database::open(root);
    const std::string cid = "sha256:deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    Database::ContentRow c;
    c.content_id = cid;
    c.status = ContentStatus::Ready;
    db.upsert_content(c);
    db.add_tag(cid, "favorite", "user");
    db.add_tag(cid, "wallpaper", "user");
    db.add_tag(cid, "favorite", "user");  // ignore dup
    auto tags = db.tags_for_content(cid);
    expect(tags.size() == 2, "two tags");
    expect(db.content_ids_for_tag("favorite").size() == 1, "one content for favorite");
    expect(db.remove_tag(cid, "wallpaper"), "remove wallpaper");
    expect(db.tags_for_content(cid).size() == 1, "one tag left");
  }

  std::error_code ec;
  fs::remove_all(root, ec);

  if (g_failures) {
    std::cerr << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "ok\n";
  return 0;
}

// Archive TOC on missing path returns nullopt (libarchive).
#include "thumtoo/archive.hpp"
