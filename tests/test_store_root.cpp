// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/// Top-level Store layout (default) + dual-path migrate + STORE_ROOT=0 opt-out.

#include "thumtoo/blob_store.hpp"
#include "thumtoo/client.hpp"
#include "thumtoo/constants.hpp"
#include "thumtoo/database.hpp"
#include "thumtoo/store.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    ++g_failures;
  }
}

fs::path make_tmpdir(const char* suffix) {
  const fs::path base =
      fs::temp_directory_path() / (std::string("thumtoo-") + suffix + "-XXXXXX");
  std::string tmpl = base.string();
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');
  if (!mkdtemp(buf.data())) {
    std::perror("mkdtemp");
    std::exit(1);
  }
  return fs::path(buf.data());
}

bool file_nonempty(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec) && fs::file_size(p, ec) > 0;
}

}  // namespace

int main() {
  setenv("THUMTOO_STORE_ROOT", "1", 1);
  unsetenv("THUMTOO_STORE_ONLY");

  // --- A: fresh open places Store at cache root; no legacy Client open ---
  {
    const fs::path cache = make_tmpdir("store-root-fresh");
    auto client = thumtoo::Client::open(cache);
    expect(client != nullptr, "Client::open STORE_ROOT");
    expect(file_nonempty(cache / "index.sqlite"),
           "fresh: Store index at cache root");
    expect(file_nonempty(cache / "bulk.sqlite"),
           "fresh: Store bulk at cache root");
    expect(!client->has_legacy(), "fresh: no legacy Database");
    expect(!fs::exists(cache / "legacy"), "fresh: no legacy/ from Client");
    expect(!fs::exists(cache / "store" / "index.sqlite"),
           "fresh: no store/ subdirectory index");
  }

  // --- B: classic dual-path cache migrates on open ---
  {
    const fs::path cache = make_tmpdir("store-root-migrate");
    // Seed legacy at top (schema < 100): index + blobs (classic dual-path).
    {
      auto db = thumtoo::Database::open(cache);
      expect(db.schema_version() == thumtoo::kSchemaVersion, "seed legacy schema");
      (void)db;
      auto blobs = thumtoo::BlobStore::open(cache);
      expect(file_nonempty(cache / "blobs.sqlite"), "seed legacy blobs.sqlite");
      (void)blobs;
    }
    // Seed redesign under store/ (schema ≥ 100).
    {
      thumtoo::Store::Paths sp;
      sp.cache_root = cache / "store";
      sp.data_root = cache / "store";
      auto store = thumtoo::Store::open(sp);
      expect(store.index_schema_version() == thumtoo::kStoreIndexSchemaVersion,
             "seed store schema");
      const auto blob_id = store.insert_blob(std::optional<std::int64_t>{42},
                                         thumtoo::BlobStatus::Ok);
      expect(blob_id >= 1, "seed store blob");
    }
    expect(file_nonempty(cache / "index.sqlite"), "pre: top legacy index");
    expect(file_nonempty(cache / "blobs.sqlite"), "pre: top legacy blobs");
    expect(file_nonempty(cache / "store" / "index.sqlite"), "pre: store/ index");

    auto client = thumtoo::Client::open(cache);
    expect(client != nullptr, "Client::open migrates dual-path");
    expect(!client->has_legacy(), "migrate: Client has no legacy");

    expect(file_nonempty(cache / "legacy" / "index.sqlite"),
           "migrate: legacy index under legacy/ (on disk)");
    expect(file_nonempty(cache / "legacy" / "blobs.sqlite"),
           "migrate: legacy blobs under legacy/ (on disk)");
    expect(file_nonempty(cache / "index.sqlite"),
           "migrate: Store index at cache root");
    expect(file_nonempty(cache / "bulk.sqlite"),
           "migrate: Store bulk at cache root");
    // After move, store/ should not still hold the only copy of index.
    // Destination existed free → files renamed from store/ to top.
    expect(!file_nonempty(cache / "store" / "index.sqlite"),
           "migrate: store/ index moved away");

    // Store is usable after migrate (blob from seed should still resolve).
    auto& store = client->store();
    expect(store.index_schema_version() == thumtoo::kStoreIndexSchemaVersion,
           "post-migrate store schema");
  }

  // --- C: default (env unset) is top-level Store + STORE_ONLY ---
  {
    unsetenv("THUMTOO_STORE_ROOT");
    unsetenv("THUMTOO_STORE_ONLY");
    const fs::path cache = make_tmpdir("store-root-default");
    auto client = thumtoo::Client::open(cache);
    expect(client != nullptr, "Client::open default");
    expect(file_nonempty(cache / "index.sqlite"),
           "default: Store index at cache root");
    expect(file_nonempty(cache / "bulk.sqlite"),
           "default: Store bulk at cache root");
    expect(!client->has_legacy(), "default: STORE_ONLY, no legacy Database");
    expect(!fs::exists(cache / "legacy"), "default: no legacy/ directory");
    expect(!fs::exists(cache / "store" / "index.sqlite"),
           "default: no store/ subdirectory");
  }

  // --- D: opt-out dual-path with THUMTOO_STORE_ROOT=0 ---
  {
    setenv("THUMTOO_STORE_ROOT", "0", 1);
    unsetenv("THUMTOO_STORE_ONLY");
    const fs::path cache = make_tmpdir("store-root-optout");
    auto client = thumtoo::Client::open(cache);
    expect(client != nullptr, "Client::open STORE_ROOT=0");
    expect(!client->has_legacy(), "opt-out: no legacy Database");
    // Nested Store layout; no top-level legacy index from Client.
    expect(file_nonempty(cache / "store" / "index.sqlite"),
           "opt-out: Store under store/");
    expect(file_nonempty(cache / "store" / "bulk.sqlite"),
           "opt-out: bulk under store/");
    expect(!fs::exists(cache / "legacy"), "opt-out: no legacy/ dir");
  }

  if (g_failures) {
    std::cerr << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "OK test_store_root\n";
  return 0;
}
