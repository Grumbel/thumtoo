// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/layout.hpp"
#include "thumtoo/constants.hpp"

#include "sqlite3.h"

#include <cstdlib>
#include <optional>
#include <string>

namespace thumtoo {
namespace {

bool env_flag_on(const char* name) {
  const char* e = std::getenv(name);
  if (!e || !e[0]) return false;
  if (e[0] == '0' || e[0] == 'f' || e[0] == 'F' || e[0] == 'n' || e[0] == 'N')
    return false;
  return true;
}

std::optional<int> probe_index_schema_version(
    const std::filesystem::path& index_path) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::is_regular_file(index_path, ec)) return std::nullopt;
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(index_path.string().c_str(), &db, SQLITE_OPEN_READONLY,
                      nullptr) != SQLITE_OK) {
    if (db) sqlite3_close(db);
    return std::nullopt;
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "SELECT value FROM schema_meta WHERE key = ?1;";
  std::optional<int> out;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, kSchemaMetaVersionKey, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW &&
        sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
      try {
        out = std::stoi(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
      } catch (...) {
      }
    }
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
  return out;
}

void rename_sqlite_bundle(const std::filesystem::path& from_base,
                          const std::filesystem::path& to_base,
                          const char* stem) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path from = from_base / stem;
  const fs::path to = to_base / stem;
  if (!fs::exists(from, ec)) return;
  if (fs::exists(to, ec)) return;
  fs::create_directories(to_base, ec);
  fs::rename(from, to, ec);
  for (const char* suf : {"-wal", "-shm"}) {
    const fs::path f2 = fs::path(from.string() + suf);
    const fs::path t2 = fs::path(to.string() + suf);
    if (fs::exists(f2, ec) && !fs::exists(t2, ec)) {
      fs::rename(f2, t2, ec);
    }
  }
}

}  // namespace

bool store_root_layout_enabled() {
  // Default: top-level Store layout (soak-confirmed). Opt out: THUMTOO_STORE_ROOT=0.
  const char* e = std::getenv("THUMTOO_STORE_ROOT");
  if (!e || !e[0]) return true;
  if (e[0] == '0' || e[0] == 'f' || e[0] == 'F' || e[0] == 'n' || e[0] == 'N')
    return false;
  return true;
}

bool store_only_mode() {
  // Default: no legacy Database/BlobStore (soak-confirmed with biltoo).
  // Opt out (dual-path): THUMTOO_STORE_ONLY=0.
  const char* e = std::getenv("THUMTOO_STORE_ONLY");
  if (!e || !e[0]) return true;
  if (e[0] == '0' || e[0] == 'f' || e[0] == 'F' || e[0] == 'n' || e[0] == 'N')
    return false;
  return true;
}

bool dual_write_to_store_enabled() { return !store_only_mode(); }

std::filesystem::path legacy_db_root(const std::filesystem::path& cache_root) {
  return store_root_layout_enabled() ? (cache_root / "legacy") : cache_root;
}

std::filesystem::path redesign_store_root(
    const std::filesystem::path& cache_root) {
  return store_root_layout_enabled() ? cache_root : (cache_root / "store");
}

void migrate_dual_path_to_store_root(const std::filesystem::path& cache_root) {
  if (!store_root_layout_enabled()) return;
  namespace fs = std::filesystem;
  const fs::path top_index = cache_root / "index.sqlite";
  const fs::path store_dir = cache_root / "store";
  const fs::path store_index = store_dir / "index.sqlite";
  const fs::path legacy_dir = cache_root / "legacy";
  const fs::path legacy_index = legacy_dir / "index.sqlite";

  auto top_ver = probe_index_schema_version(top_index);
  auto store_ver = probe_index_schema_version(store_index);
  auto legacy_ver = probe_index_schema_version(legacy_index);

  if (top_ver && *top_ver < kStoreIndexSchemaVersion && !legacy_ver) {
    rename_sqlite_bundle(cache_root, legacy_dir, "index.sqlite");
    rename_sqlite_bundle(cache_root, legacy_dir, "blobs.sqlite");
  }

  top_ver = probe_index_schema_version(top_index);
  store_ver = probe_index_schema_version(store_index);
  if (store_ver && *store_ver >= kStoreIndexSchemaVersion && !top_ver) {
    rename_sqlite_bundle(store_dir, cache_root, "index.sqlite");
    rename_sqlite_bundle(store_dir, cache_root, "bulk.sqlite");
  }
}

}  // namespace thumtoo
