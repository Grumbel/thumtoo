// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/constants.hpp"
#include "thumtoo/database.hpp"
#include "thumtoo/blob_store.hpp"
#include "thumtoo/uri.hpp"
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


  {
    auto db = Database::open(root);
    expect(db.count_tiles() == 0, "no tiles initially");
    const std::string cid = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    Database::ContentRow c;
    c.content_id = cid;
    c.width = 512;
    c.height = 384;
    c.status = ContentStatus::Ready;
    db.upsert_content(c);
    Database::TileRow t;
    t.content_id = cid;
    t.scale = 0;
    t.x = 0;
    t.y = 0;
    t.width = 256;
    t.height = 256;
    t.codec = "jpeg";
    t.quality = 80;
    db.upsert_tile(t);
    t.x = 1;
    db.upsert_tile(t);
    expect(db.count_tiles() == 2, "two tiles");
    auto got = db.find_tile(cid, 0, 1, 0);
    expect(got.has_value(), "find_tile");
    expect(got && got->x == 1, "tile x");
    int min_s = -1, max_s = -1;
    expect(db.tile_min_max_scale(cid, min_s, max_s), "min max scale");
    expect(min_s == 0 && max_s == 0, "scale range");
    expect(db.list_tiles(cid).size() == 2, "list tiles");
  }

  // Content-id URI and reverse locator lookup
  {
    using namespace thumtoo;
    auto db = Database::open(root / "id-index");
    const std::string cid = "sha256:deadbeef";
    Database::ContentRow c;
    c.content_id = cid;
    c.width = 100;
    c.height = 50;
    c.status = ContentStatus::Ready;
    db.upsert_content(c);
    Database::LocatorRow a;
    a.uri = file_uri_from_path("/tmp/a.jpg");
    a.content_id = cid;
    db.upsert_locator(a);
    Database::LocatorRow b;
    b.uri = file_uri_from_path("/tmp/renamed.jpg");
    b.content_id = cid;
    db.upsert_locator(b);

    auto by_id = db.meta_for_content_id(cid);
    expect(by_id.has_value() && by_id->size && by_id->size->width == 100,
           "meta_for_content_id");
    auto by_uri = db.meta_for_uri(cid);
    expect(by_uri.has_value() && by_uri->content_id == cid, "meta_for_uri content-id");
    auto locs = db.list_locators_for_content_id(cid);
    expect(locs.size() == 2, "two locators same content");
  }


  // Locator prefix / LIKE query
  {
    auto db = Database::open(root / "query-index");
    Database::LocatorRow a;
    a.uri = "file:///home/user/photos/a.jpg";
    a.outer_path = "/home/user/photos/a.jpg";
    a.content_id = "sha256:aa";
    db.upsert_locator(a);
    Database::LocatorRow b;
    b.uri = "file:///home/user/photos/b.jpg";
    b.outer_path = "/home/user/photos/b.jpg";
    b.content_id = "sha256:bb";
    db.upsert_locator(b);
    Database::LocatorRow c;
    c.uri = "file:///home/user/other/c.jpg";
    c.outer_path = "/home/user/other/c.jpg";
    c.content_id = "sha256:cc";
    db.upsert_locator(c);

    auto by_uri = db.list_locators_by_uri_prefix("file:///home/user/photos/");
    expect(by_uri.size() == 2, "uri prefix photos");
    auto by_path = db.list_locators_by_outer_path_prefix("/home/user/photos/");
    expect(by_path.size() == 2, "outer_path prefix");
    auto like = db.list_locators_like("%other%");
    expect(like.size() == 1, "like other");
  }

  // Durable HTTP body cache
  {
    auto blobs = BlobStore::open(root / "http-blobs");
    const std::string url = "https://example.com/a.jpg";
    const std::vector<std::uint8_t> body = {1, 2, 3, 4, 5};
    blobs.put_http_body(url, body.data(), body.size(), 1'700'000'000);
    expect(blobs.count_http_bodies() == 1, "one http body");
    auto got = blobs.get_http_body(url, 0);
    expect(got.has_value() && *got == body, "get_http_body");
    // max_age 1 second vs fetched long ago → miss
    auto stale = blobs.get_http_body(url, 1);
    expect(!stale.has_value(), "ttl miss");
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
