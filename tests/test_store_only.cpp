// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

/// THUMTOO_STORE_ONLY: durable Store on disk; legacy Database/BlobStore in-memory.

#include "thumtoo/client.hpp"
#include "thumtoo/constants.hpp"
#include "thumtoo/layout.hpp"
#include "thumtoo/store.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
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

fs::path make_tmpdir() {
  const fs::path base =
      fs::temp_directory_path() / "thumtoo-store-only-XXXXXX";
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
  setenv("THUMTOO_STORE_ONLY", "1", 1);
  // Default STORE_ROOT on → Store at cache root.
  unsetenv("THUMTOO_STORE_ROOT");

  const fs::path cache = make_tmpdir();
  auto client = thumtoo::Client::open(cache);
  expect(client != nullptr, "Client::open STORE_ONLY");
  expect(thumtoo::store_only_mode(), "store_only_mode");

  // Durable redesign Store on disk.
  expect(file_nonempty(cache / "index.sqlite"), "Store index on disk");
  expect(file_nonempty(cache / "bulk.sqlite"), "Store bulk on disk");

  // No legacy files under cache (or legacy/).
  expect(!fs::exists(cache / "legacy" / "index.sqlite"),
         "no legacy index on disk");
  expect(!fs::exists(cache / "legacy" / "blobs.sqlite"),
         "no legacy blobs on disk");
  // Top-level legacy names also absent (would be dual-path / opt-out).
  // Store index uses the same name as legacy index at root under STORE_ROOT —
  // that is the redesign Store, not legacy (schema ≥ 100).
  expect(client->store().index_schema_version() ==
             thumtoo::kStoreIndexSchemaVersion,
         "on-disk index is Store schema");

  // In-memory legacy is still usable in-process (schema version query).
  expect(client->db().schema_version() == thumtoo::kSchemaVersion,
         "ephemeral legacy schema");
  expect(client->db().db_path() == ":memory:", "legacy db_path is memory");

  if (g_failures) {
    std::cerr << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "OK test_store_only\n";
  return 0;
}
