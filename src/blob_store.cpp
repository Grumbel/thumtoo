// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/blob_store.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <utility>

namespace thumtoo {

BlobStore::BlobStore(sqlite3* db, std::filesystem::path db_path)
    : db_(db), db_path_(std::move(db_path)) {}

BlobStore::BlobStore(BlobStore&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)),
      db_path_(std::move(other.db_path_)) {}

BlobStore& BlobStore::operator=(BlobStore&& other) noexcept {
  if (this != &other) {
    if (db_) sqlite3_close(db_);
    db_ = std::exchange(other.db_, nullptr);
    db_path_ = std::move(other.db_path_);
  }
  return *this;
}

BlobStore::~BlobStore() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

void BlobStore::exec(const char* sql) const {
  char* err = nullptr;
  const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    std::string msg = err ? err : "sqlite3_exec failed";
    sqlite3_free(err);
    throw std::runtime_error(msg);
  }
}

BlobStore BlobStore::open(const std::filesystem::path& cache_root) {
  std::filesystem::create_directories(cache_root);
  const auto path = cache_root / "blobs.sqlite";
  sqlite3* db = nullptr;
  if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK) {
    const std::string msg = db ? sqlite3_errmsg(db) : "sqlite3_open failed";
    if (db) sqlite3_close(db);
    throw std::runtime_error(msg);
  }
  BlobStore out(db, path);
  out.exec("PRAGMA journal_mode=WAL;");
  out.exec("PRAGMA busy_timeout=5000;");
  // Large pages help a bit with multi-megabyte ladder blobs.
  out.exec("PRAGMA page_size=4096;");
  out.migrate_or_init();
  return out;
}

void BlobStore::migrate_or_init() {
  exec(
      "CREATE TABLE IF NOT EXISTS level_blobs ("
      "  content_id TEXT NOT NULL,"
      "  max_edge INTEGER NOT NULL,"
      "  frame_idx INTEGER NOT NULL DEFAULT 0,"
      "  width INTEGER,"
      "  height INTEGER,"
      "  codec TEXT,"
      "  quality INTEGER,"
      "  data BLOB NOT NULL,"
      "  PRIMARY KEY (content_id, max_edge, frame_idx)"
      ");");
}

void BlobStore::put_level(std::string_view content_id, int max_edge,
                          int frame_idx, int width, int height,
                          std::string_view codec, int quality,
                          const std::uint8_t* data, std::size_t size) {
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO level_blobs(content_id, max_edge, frame_idx, width, height, "
      "codec, quality, data) VALUES(?1,?2,?3,?4,?5,?6,?7,?8) "
      "ON CONFLICT(content_id, max_edge, frame_idx) DO UPDATE SET "
      "width=excluded.width, height=excluded.height, codec=excluded.codec, "
      "quality=excluded.quality, data=excluded.data;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(),
                    static_cast<int>(content_id.size()), SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, max_edge);
  sqlite3_bind_int(stmt, 3, frame_idx);
  sqlite3_bind_int(stmt, 4, width);
  sqlite3_bind_int(stmt, 5, height);
  sqlite3_bind_text(stmt, 6, codec.data(), static_cast<int>(codec.size()),
                    SQLITE_STATIC);
  sqlite3_bind_int(stmt, 7, quality);
  sqlite3_bind_blob(stmt, 8, data, static_cast<int>(size), SQLITE_STATIC);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_finalize(stmt);
}

std::optional<std::vector<std::uint8_t>> BlobStore::get_level(
    std::string_view content_id, int max_edge, int frame_idx) const {
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT data FROM level_blobs WHERE content_id = ?1 AND max_edge = ?2 "
      "AND frame_idx = ?3;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(),
                    static_cast<int>(content_id.size()), SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, max_edge);
  sqlite3_bind_int(stmt, 3, frame_idx);
  std::optional<std::vector<std::uint8_t>> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const auto* p = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt, 0));
    const int n = sqlite3_column_bytes(stmt, 0);
    if (p && n > 0) out = std::vector<std::uint8_t>(p, p + n);
  }
  sqlite3_finalize(stmt);
  return out;
}

std::int64_t BlobStore::count_levels() const {
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM level_blobs;", -1, &stmt,
                     nullptr);
  std::int64_t n = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return n;
}

}  // namespace thumtoo
