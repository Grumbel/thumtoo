// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/store.hpp"
#include "thumtoo/constants.hpp"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;

static int g_failed = 0;

static void expect(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failed;
  }
}

int main() {
  const fs::path root =
      fs::temp_directory_path() / "thumtoo-test-store-XXXXXX";
  // mkdtemp needs a mutable buffer ending in XXXXXX
  std::string tmpl = root.string();
  if (tmpl.size() < 6) {
    std::cerr << "temp path too short\n";
    return 1;
  }
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  if (!mkdtemp(buf.data())) {
    std::perror("mkdtemp");
    return 1;
  }
  const fs::path dir(buf.data());

  try {
    {
      auto store = thumtoo::Store::open(dir);
      expect(store.index_schema_version() == thumtoo::kStoreIndexSchemaVersion,
             "schema version 100");
      expect(store.meta_get(thumtoo::kSchemaMetaVersionKey).has_value(),
             "meta version present");

      const auto blob_id =
          store.insert_blob(12345, thumtoo::BlobStatus::Ok);
      expect(blob_id >= 1, "blob id assigned");
      auto blob = store.find_blob(blob_id);
      expect(blob.has_value(), "find blob");
      expect(blob && blob->size && *blob->size == 12345, "blob size");

      // 32-byte fake SHA-256
      std::vector<std::uint8_t> digest(32);
      for (int i = 0; i < 32; ++i) digest[static_cast<std::size_t>(i)] =
          static_cast<std::uint8_t>(i + 1);
      store.put_hash(blob_id, thumtoo::HashAlgoId::Sha256, digest);

      auto found = store.find_blob_by_hash(thumtoo::HashAlgoId::Sha256, digest);
      expect(found && *found == blob_id, "find by hash");

      auto ref = store.blob_ref_sha256(blob_id);
      expect(ref.has_value(), "blob ref");
      expect(ref && ref->starts_with("blob:sha256:"), "blob ref prefix");
      expect(ref && ref->size() == std::string("blob:sha256:").size() + 64,
             "blob ref length");

      auto parsed = thumtoo::Store::parse_sha256_digest(*ref);
      expect(parsed && *parsed == digest, "parse blob ref");

      const std::string uri = "file:///tmp/example.jpg";
      const auto loc_id = store.upsert_locator(uri, blob_id, 12345, 99);
      expect(loc_id >= 1, "locator id");
      auto loc = store.find_locator(uri);
      expect(loc && loc->blob_id && *loc->blob_id == blob_id, "locator blob");
      expect(store.count_blobs() == 1, "one blob");
      expect(store.count_locators() == 1, "one locator");

      auto locs = store.list_locators_for_blob(blob_id);
      expect(locs.size() == 1 && locs[0].uri == uri, "list locators");
    }

    // Re-open preserves rows
    {
      auto store = thumtoo::Store::open(dir);
      expect(store.count_blobs() == 1, "reopen blobs");
      expect(store.count_locators() == 1, "reopen locators");
    }

    // Re-create after deleting index+bulk (simulates operator cache wipe).
    {
      fs::remove(dir / "index.sqlite");
      fs::remove(dir / "bulk.sqlite");
      auto store = thumtoo::Store::open(dir);
      expect(store.index_schema_version() == thumtoo::kStoreIndexSchemaVersion,
             "fresh after remove");
      expect(store.count_blobs() == 0, "empty after wipe");
    }
  } catch (const std::exception& ex) {
    std::cerr << "exception: " << ex.what() << '\n';
    ++g_failed;
  }

  std::error_code ec;
  fs::remove_all(dir, ec);

  if (g_failed) {
    std::cerr << g_failed << " failure(s)\n";
    return 1;
  }
  std::cout << "ok\n";
  return 0;
}
