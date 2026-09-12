#include <mutex>
// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/database.hpp"
#include "thumtoo/constants.hpp"

#include "sqlite3.h"

#include <chrono>
#include <filesystem>
#include <system_error>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace thumtoo {
namespace {


Database::LocatorRow locator_from_stmt(sqlite3_stmt* stmt) {
  Database::LocatorRow r;
  r.uri = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  if (sqlite3_column_type(stmt, 1) != SQLITE_NULL)
    r.content_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
  if (sqlite3_column_type(stmt, 2) != SQLITE_NULL)
    r.outer_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
  if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
    r.member_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
  if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
    r.size = sqlite3_column_int64(stmt, 4);
  if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
    r.mtime_ns = sqlite3_column_int64(stmt, 5);
  return r;
}

std::string escape_like_prefix(std::string_view prefix) {
  std::string out;
  out.reserve(prefix.size() + 8);
  for (char c : prefix) {
    if (c == '%' || c == '_' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

std::int64_t now_unix_s() {
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string ladder_edges_csv() {
  std::ostringstream os;
  for (std::size_t i = 0; i < kLadderEdges.size(); ++i) {
    if (i) os << ',';
    os << kLadderEdges[i];
  }
  return os.str();
}

// Embedded schema (src/schema.sql).
constexpr char kSchemaSql[] = R"SQL(
CREATE TABLE IF NOT EXISTS schema_meta (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS content (
  content_id TEXT PRIMARY KEY,
  width INTEGER,
  height INTEGER,
  format TEXT,
  duration_ms INTEGER,
  still_count INTEGER,
  status INTEGER NOT NULL DEFAULT 0,
  error_code TEXT,
  updated_at INTEGER,
  lqip BLOB,
  lqip_kind INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS locators (
  uri TEXT PRIMARY KEY,
  content_id TEXT,
  outer_path TEXT,
  member_path TEXT,
  size INTEGER,
  mtime_ns INTEGER,
  updated_at INTEGER
);
CREATE TABLE IF NOT EXISTS archive_entries (
  archive_uri TEXT NOT NULL,
  member_path TEXT NOT NULL,
  uncompressed_size INTEGER,
  PRIMARY KEY (archive_uri, member_path)
);
CREATE TABLE IF NOT EXISTS directory_snapshots (
  dir_uri TEXT PRIMARY KEY,
  size INTEGER,
  mtime_ns INTEGER,
  listed_at INTEGER,
  incomplete INTEGER
);
CREATE TABLE IF NOT EXISTS directory_entries (
  dir_uri TEXT NOT NULL,
  name TEXT NOT NULL,
  child_uri TEXT,
  is_dir INTEGER,
  size INTEGER,
  mtime_ns INTEGER,
  PRIMARY KEY (dir_uri, name)
);
CREATE TABLE IF NOT EXISTS levels (
  content_id TEXT NOT NULL,
  max_edge INTEGER NOT NULL,
  frame_idx INTEGER NOT NULL DEFAULT 0,
  pts_ms INTEGER,
  width INTEGER,
  height INTEGER,
  codec TEXT,
  quality INTEGER,
  path TEXT,
  source INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (content_id, max_edge, frame_idx)
);
CREATE TABLE IF NOT EXISTS tags (
  content_id TEXT NOT NULL,
  tag TEXT NOT NULL,
  source TEXT,
  created_at INTEGER,
  PRIMARY KEY (content_id, tag)
);
CREATE TABLE IF NOT EXISTS tiles (
  content_id TEXT NOT NULL,
  scale INTEGER NOT NULL,
  x INTEGER NOT NULL,
  y INTEGER NOT NULL,
  width INTEGER,
  height INTEGER,
  codec TEXT,
  quality INTEGER,
  source INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (content_id, scale, x, y)
);
CREATE INDEX IF NOT EXISTS idx_locators_content_id ON locators(content_id);
CREATE INDEX IF NOT EXISTS idx_levels_content_id ON levels(content_id);
CREATE INDEX IF NOT EXISTS idx_tiles_content_id ON tiles(content_id);
CREATE TABLE IF NOT EXISTS text_layers (
  content_id TEXT NOT NULL,
  page_1based INTEGER NOT NULL,
  layout_key TEXT NOT NULL DEFAULT '',
  page_x0 REAL,
  page_y0 REAL,
  page_x1 REAL,
  page_y1 REAL,
  payload BLOB NOT NULL,
  updated_at INTEGER,
  PRIMARY KEY (content_id, page_1based, layout_key)
);
CREATE TABLE IF NOT EXISTS document_outlines (
  content_id TEXT NOT NULL,
  layout_key TEXT NOT NULL DEFAULT '',
  payload BLOB NOT NULL,
  updated_at INTEGER,
  PRIMARY KEY (content_id, layout_key)
);
CREATE INDEX IF NOT EXISTS idx_text_layers_content_id ON text_layers(content_id);
)SQL";

}  // namespace

Database::Database(sqlite3* db, std::filesystem::path cache_root,
                   std::filesystem::path db_path, int schema_version)
    : db_(db),
      cache_root_(std::move(cache_root)),
      db_path_(std::move(db_path)),
      schema_version_(schema_version) {}

Database::Database(Database&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)),
      cache_root_(std::move(other.cache_root_)),
      db_path_(std::move(other.db_path_)),
      schema_version_(other.schema_version_) {}

Database& Database::operator=(Database&& other) noexcept {
  if (this != &other) {
    if (db_) sqlite3_close(db_);
    db_ = std::exchange(other.db_, nullptr);
    cache_root_ = std::move(other.cache_root_);
    db_path_ = std::move(other.db_path_);
    schema_version_ = other.schema_version_;
  }
  return *this;
}

Database::~Database() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

void Database::exec(const char* sql) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  char* err = nullptr;
  const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    std::string msg = err ? err : "sqlite3_exec failed";
    sqlite3_free(err);
    throw std::runtime_error(msg);
  }
}

Database Database::open(const std::filesystem::path& cache_root) {
  std::filesystem::create_directories(cache_root);
  const auto db_path = cache_root / "index.sqlite";

  sqlite3* db = nullptr;
  if (sqlite3_open(db_path.string().c_str(), &db) != SQLITE_OK) {
    const std::string msg = db ? sqlite3_errmsg(db) : "sqlite3_open failed";
    if (db) sqlite3_close(db);
    throw std::runtime_error(msg);
  }

  Database out(db, cache_root, db_path, 0);
  // WAL + busy timeout for multi-process safety (DESIGN).
  out.exec("PRAGMA journal_mode=WAL;");
  out.exec("PRAGMA busy_timeout=5000;");
  out.exec("PRAGMA foreign_keys=ON;");
  out.migrate_or_init();
  // After schema: optional indexes not yet in older cache dirs.
  out.exec("CREATE INDEX IF NOT EXISTS idx_locators_outer_path ON locators(outer_path);");
  return out;
}

void Database::migrate_or_init() {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  exec(kSchemaSql);

  auto ver = meta_get(kSchemaMetaVersionKey);
  if (!ver) {
    meta_set(kSchemaMetaVersionKey, std::to_string(kSchemaVersion));
    meta_set(kSchemaMetaLadderEdgesKey, ladder_edges_csv());
    meta_set(kSchemaMetaJxlQualityKey, std::to_string(kDefaultJxlQuality));
    schema_version_ = kSchemaVersion;
    return;
  }

  schema_version_ = std::stoi(*ver);
  if (schema_version_ > kSchemaVersion) {
    throw std::runtime_error("database schema_version is newer than this build");
  }
  if (schema_version_ < kSchemaVersion) {
    if (schema_version_ < 2) {
      // Inline LQIP (ThumbHash) on content rows — no blob store.
      exec("ALTER TABLE content ADD COLUMN lqip BLOB;");
      exec("ALTER TABLE content ADD COLUMN lqip_kind INTEGER NOT NULL DEFAULT 0;");
    }
    // v3: text_layers + document_outlines created by kSchemaSql IF NOT EXISTS.
    meta_set(kSchemaMetaVersionKey, std::to_string(kSchemaVersion));
    schema_version_ = kSchemaVersion;
  }
  // Additive tile decode path (TileSource). Safe on every open for old caches.
  {
    bool has_source = false;
    sqlite3_stmt* info = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA table_info(tiles);", -1, &info,
                           nullptr) == SQLITE_OK) {
      while (sqlite3_step(info) == SQLITE_ROW) {
        const char* name =
            reinterpret_cast<const char*>(sqlite3_column_text(info, 1));
        if (name && std::string(name) == "source") has_source = true;
      }
      sqlite3_finalize(info);
    }
    if (!has_source) {
      exec("ALTER TABLE tiles ADD COLUMN source INTEGER NOT NULL DEFAULT 0;");
    }
  }
  // Ladder level provenance (PixelSource). Legacy rows stay 0 = Unknown.
  {
    bool has_source = false;
    sqlite3_stmt* info = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA table_info(levels);", -1, &info,
                           nullptr) == SQLITE_OK) {
      while (sqlite3_step(info) == SQLITE_ROW) {
        const char* name =
            reinterpret_cast<const char*>(sqlite3_column_text(info, 1));
        if (name && std::string(name) == "source") has_source = true;
      }
      sqlite3_finalize(info);
    }
    if (!has_source) {
      exec("ALTER TABLE levels ADD COLUMN source INTEGER NOT NULL DEFAULT 0;");
    }
  }
}

std::optional<std::string> Database::meta_get(std::string_view key) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "SELECT value FROM schema_meta WHERE key = ?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, key.data(), static_cast<int>(key.size()),
                    SQLITE_STATIC);
  std::optional<std::string> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char* t = sqlite3_column_text(stmt, 0);
    if (t) out = reinterpret_cast<const char*>(t);
  }
  sqlite3_finalize(stmt);
  return out;
}

void Database::meta_set(std::string_view key, std::string_view value) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO schema_meta(key, value) VALUES(?1, ?2) "
      "ON CONFLICT(key) DO UPDATE SET value = excluded.value;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, key.data(), static_cast<int>(key.size()),
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, value.data(), static_cast<int>(value.size()),
                    SQLITE_STATIC);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_finalize(stmt);
}

std::int64_t Database::count_content() const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM content;", -1, &stmt, nullptr);
  std::int64_t n = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return n;
}

std::int64_t Database::count_locators() const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM locators;", -1, &stmt, nullptr);
  std::int64_t n = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return n;
}

std::int64_t Database::count_levels() const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM levels;", -1, &stmt, nullptr);
  std::int64_t n = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return n;
}

std::int64_t Database::count_tiles() const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM tiles;", -1, &stmt, nullptr);
  std::int64_t n = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return n;
}

std::int64_t Database::count_directory_snapshots() const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM directory_snapshots;", -1,
                     &stmt, nullptr);
  std::int64_t n = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return n;
}

std::vector<Database::LocatorRow> Database::list_locators(int limit) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT uri, content_id, outer_path, member_path, size, mtime_ns "
      "FROM locators ORDER BY uri LIMIT ?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int(stmt, 1, limit);
  std::vector<LocatorRow> rows;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    LocatorRow r;
    r.uri = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (sqlite3_column_type(stmt, 1) != SQLITE_NULL)
      r.content_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    if (sqlite3_column_type(stmt, 2) != SQLITE_NULL)
      r.outer_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
      r.member_path =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
      r.size = sqlite3_column_int64(stmt, 4);
    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
      r.mtime_ns = sqlite3_column_int64(stmt, 5);
    rows.push_back(std::move(r));
  }
  sqlite3_finalize(stmt);
  return rows;
}

std::vector<Database::LocatorRow> Database::list_locators_for_content_id(
    std::string_view content_id, int limit) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT uri, content_id, outer_path, member_path, size, mtime_ns "
      "FROM locators WHERE content_id = ?1 ORDER BY uri LIMIT ?2;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(), static_cast<int>(content_id.size()),
                    SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, limit > 0 ? limit : 100);
  std::vector<LocatorRow> rows;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    LocatorRow r;
    r.uri = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (sqlite3_column_type(stmt, 1) != SQLITE_NULL)
      r.content_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    if (sqlite3_column_type(stmt, 2) != SQLITE_NULL)
      r.outer_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
      r.member_path =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
      r.size = sqlite3_column_int64(stmt, 4);
    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
      r.mtime_ns = sqlite3_column_int64(stmt, 5);
    rows.push_back(std::move(r));
  }
  sqlite3_finalize(stmt);
  return rows;
}



std::vector<Database::LocatorRow> Database::list_locators_by_uri_prefix(
    std::string_view uri_prefix, int limit) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  const std::string pat = escape_like_prefix(uri_prefix) + "%";
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT uri, content_id, outer_path, member_path, size, mtime_ns "
      "FROM locators WHERE uri LIKE ?1 ESCAPE '\\' ORDER BY uri LIMIT ?2;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, pat.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, limit > 0 ? limit : 100);
  std::vector<LocatorRow> rows;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    rows.push_back(locator_from_stmt(stmt));
  }
  sqlite3_finalize(stmt);
  return rows;
}

std::vector<Database::LocatorRow> Database::list_locators_by_outer_path_prefix(
    std::string_view path_prefix, int limit) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  const std::string pat = escape_like_prefix(path_prefix) + "%";
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT uri, content_id, outer_path, member_path, size, mtime_ns "
      "FROM locators WHERE outer_path LIKE ?1 ESCAPE '\\' ORDER BY uri LIMIT ?2;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, pat.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, limit > 0 ? limit : 100);
  std::vector<LocatorRow> rows;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    rows.push_back(locator_from_stmt(stmt));
  }
  sqlite3_finalize(stmt);
  return rows;
}

std::vector<Database::LocatorRow> Database::list_locators_like(
    std::string_view uri_like_pattern, int limit) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT uri, content_id, outer_path, member_path, size, mtime_ns "
      "FROM locators WHERE uri LIKE ?1 ORDER BY uri LIMIT ?2;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, uri_like_pattern.data(),
                    static_cast<int>(uri_like_pattern.size()), SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, limit > 0 ? limit : 100);
  std::vector<LocatorRow> rows;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    rows.push_back(locator_from_stmt(stmt));
  }
  sqlite3_finalize(stmt);
  return rows;
}

std::vector<Database::ContentRow> Database::list_content(int limit) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT content_id, width, height, format, duration_ms, still_count, "
      "status, error_code FROM content ORDER BY content_id LIMIT ?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int(stmt, 1, limit);
  std::vector<ContentRow> rows;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    ContentRow r;
    r.content_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (sqlite3_column_type(stmt, 1) != SQLITE_NULL)
      r.width = sqlite3_column_int(stmt, 1);
    if (sqlite3_column_type(stmt, 2) != SQLITE_NULL)
      r.height = sqlite3_column_int(stmt, 2);
    if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
      r.format = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
      r.duration_ms = sqlite3_column_int64(stmt, 4);
    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
      r.still_count = sqlite3_column_int(stmt, 5);
    r.status = static_cast<ContentStatus>(sqlite3_column_int(stmt, 6));
    if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
      r.error_code =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    rows.push_back(std::move(r));
  }
  sqlite3_finalize(stmt);
  return rows;
}

void Database::upsert_locator(const LocatorRow& row) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO locators(uri, content_id, outer_path, member_path, size, "
      "mtime_ns, updated_at) VALUES(?1,?2,?3,?4,?5,?6,?7) "
      "ON CONFLICT(uri) DO UPDATE SET "
      "content_id=excluded.content_id, outer_path=excluded.outer_path, "
      "member_path=excluded.member_path, size=excluded.size, "
      "mtime_ns=excluded.mtime_ns, updated_at=excluded.updated_at;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  const auto ts = now_unix_s();
  sqlite3_bind_text(stmt, 1, row.uri.c_str(), -1, SQLITE_TRANSIENT);
  if (row.content_id)
    sqlite3_bind_text(stmt, 2, row.content_id->c_str(), -1, SQLITE_TRANSIENT);
  else
    sqlite3_bind_null(stmt, 2);
  if (row.outer_path)
    sqlite3_bind_text(stmt, 3, row.outer_path->c_str(), -1, SQLITE_TRANSIENT);
  else
    sqlite3_bind_null(stmt, 3);
  if (row.member_path)
    sqlite3_bind_text(stmt, 4, row.member_path->c_str(), -1, SQLITE_TRANSIENT);
  else
    sqlite3_bind_null(stmt, 4);
  if (row.size)
    sqlite3_bind_int64(stmt, 5, *row.size);
  else
    sqlite3_bind_null(stmt, 5);
  if (row.mtime_ns)
    sqlite3_bind_int64(stmt, 6, *row.mtime_ns);
  else
    sqlite3_bind_null(stmt, 6);
  sqlite3_bind_int64(stmt, 7, ts);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_finalize(stmt);
}

void Database::upsert_content(const ContentRow& row) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO content(content_id, width, height, format, duration_ms, "
      "still_count, status, error_code, updated_at) "
      "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9) "
      "ON CONFLICT(content_id) DO UPDATE SET "
      "width=excluded.width, height=excluded.height, format=excluded.format, "
      "duration_ms=excluded.duration_ms, still_count=excluded.still_count, "
      "status=excluded.status, error_code=excluded.error_code, "
      "updated_at=excluded.updated_at;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  const auto ts = now_unix_s();
  sqlite3_bind_text(stmt, 1, row.content_id.c_str(), -1, SQLITE_TRANSIENT);
  if (row.width)
    sqlite3_bind_int(stmt, 2, *row.width);
  else
    sqlite3_bind_null(stmt, 2);
  if (row.height)
    sqlite3_bind_int(stmt, 3, *row.height);
  else
    sqlite3_bind_null(stmt, 3);
  if (row.format)
    sqlite3_bind_text(stmt, 4, row.format->c_str(), -1, SQLITE_TRANSIENT);
  else
    sqlite3_bind_null(stmt, 4);
  if (row.duration_ms)
    sqlite3_bind_int64(stmt, 5, *row.duration_ms);
  else
    sqlite3_bind_null(stmt, 5);
  if (row.still_count)
    sqlite3_bind_int(stmt, 6, *row.still_count);
  else
    sqlite3_bind_null(stmt, 6);
  sqlite3_bind_int(stmt, 7, static_cast<int>(row.status));
  if (row.error_code)
    sqlite3_bind_text(stmt, 8, row.error_code->c_str(), -1, SQLITE_TRANSIENT);
  else
    sqlite3_bind_null(stmt, 8);
  sqlite3_bind_int64(stmt, 9, ts);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_finalize(stmt);
}


std::optional<Database::LocatorRow> Database::find_locator(std::string_view uri) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT uri, content_id, outer_path, member_path, size, mtime_ns "
      "FROM locators WHERE uri = ?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, uri.data(), static_cast<int>(uri.size()),
                    SQLITE_STATIC);
  std::optional<LocatorRow> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    LocatorRow r;
    r.uri = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (sqlite3_column_type(stmt, 1) != SQLITE_NULL)
      r.content_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    if (sqlite3_column_type(stmt, 2) != SQLITE_NULL)
      r.outer_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
      r.member_path =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
      r.size = sqlite3_column_int64(stmt, 4);
    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
      r.mtime_ns = sqlite3_column_int64(stmt, 5);
    out = std::move(r);
  }
  sqlite3_finalize(stmt);
  return out;
}

std::optional<Database::ContentRow> Database::find_content(
    std::string_view content_id) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT content_id, width, height, format, duration_ms, still_count, "
      "status, error_code FROM content WHERE content_id = ?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(),
                    static_cast<int>(content_id.size()), SQLITE_STATIC);
  std::optional<ContentRow> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    ContentRow r;
    r.content_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (sqlite3_column_type(stmt, 1) != SQLITE_NULL)
      r.width = sqlite3_column_int(stmt, 1);
    if (sqlite3_column_type(stmt, 2) != SQLITE_NULL)
      r.height = sqlite3_column_int(stmt, 2);
    if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
      r.format = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
      r.duration_ms = sqlite3_column_int64(stmt, 4);
    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
      r.still_count = sqlite3_column_int(stmt, 5);
    r.status = static_cast<ContentStatus>(sqlite3_column_int(stmt, 6));
    if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
      r.error_code =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    out = std::move(r);
  }
  sqlite3_finalize(stmt);
  return out;
}

std::optional<ContentMeta> Database::meta_for_content_id(
    std::string_view content_id) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  auto c = find_content(content_id);
  if (!c) return std::nullopt;
  ContentMeta m;
  m.content_id = c->content_id;
  if (c->width && c->height) m.size = Size{*c->width, *c->height};
  m.duration_ms = c->duration_ms;
  m.still_count = c->still_count;
  m.status = c->status;
  m.error_code = c->error_code;
  m.format = c->format;
  return m;
}

std::optional<ContentMeta> Database::meta_for_uri(std::string_view uri) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  // Content-addressed id used as URI (sha256:… / sha1:…).
  if (uri.starts_with("sha256:") || uri.starts_with("sha1:")) {
    return meta_for_content_id(uri);
  }
  auto loc = find_locator(uri);
  if (!loc || !loc->content_id) return std::nullopt;
  return meta_for_content_id(*loc->content_id);
}


void Database::upsert_level(const LevelRow& row) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO levels(content_id, max_edge, frame_idx, pts_ms, width, height, "
      "codec, quality, path, source) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10) "
      "ON CONFLICT(content_id, max_edge, frame_idx) DO UPDATE SET "
      "pts_ms=excluded.pts_ms, width=excluded.width, height=excluded.height, "
      "codec=excluded.codec, quality=excluded.quality, path=excluded.path, "
      "source=excluded.source;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, row.content_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, row.max_edge);
  sqlite3_bind_int(stmt, 3, row.frame_idx);
  if (row.pts_ms)
    sqlite3_bind_int64(stmt, 4, *row.pts_ms);
  else
    sqlite3_bind_null(stmt, 4);
  if (row.width)
    sqlite3_bind_int(stmt, 5, *row.width);
  else
    sqlite3_bind_null(stmt, 5);
  if (row.height)
    sqlite3_bind_int(stmt, 6, *row.height);
  else
    sqlite3_bind_null(stmt, 6);
  if (row.codec)
    sqlite3_bind_text(stmt, 7, row.codec->c_str(), -1, SQLITE_TRANSIENT);
  else
    sqlite3_bind_null(stmt, 7);
  if (row.quality)
    sqlite3_bind_int(stmt, 8, *row.quality);
  else
    sqlite3_bind_null(stmt, 8);
  if (row.path)
    sqlite3_bind_text(stmt, 9, row.path->c_str(), -1, SQLITE_TRANSIENT);
  else
    sqlite3_bind_null(stmt, 9);
  sqlite3_bind_int(stmt, 10, row.source);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_finalize(stmt);
}

void Database::update_locator_content_id(std::string_view uri,
                                         std::string_view content_id) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "UPDATE locators SET content_id = ?1 WHERE uri = ?2;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(), static_cast<int>(content_id.size()),
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, uri.data(), static_cast<int>(uri.size()),
                    SQLITE_STATIC);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_finalize(stmt);
}

void Database::delete_content(std::string_view content_id) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "DELETE FROM content WHERE content_id = ?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(),
                    static_cast<int>(content_id.size()), SQLITE_STATIC);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_finalize(stmt);
}

void Database::delete_level(std::string_view content_id, int max_edge,
                            int frame_idx) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "DELETE FROM levels WHERE content_id = ?1 AND max_edge = ?2 AND "
      "frame_idx = ?3;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(),
                    static_cast<int>(content_id.size()), SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, max_edge);
  sqlite3_bind_int(stmt, 3, frame_idx);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_finalize(stmt);
}


namespace {

std::optional<Database::LevelRow> step_level_row(sqlite3_stmt* stmt) {
  if (sqlite3_step(stmt) != SQLITE_ROW) return std::nullopt;
  Database::LevelRow r;
  r.content_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  r.max_edge = sqlite3_column_int(stmt, 1);
  r.frame_idx = sqlite3_column_int(stmt, 2);
  if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
    r.pts_ms = sqlite3_column_int64(stmt, 3);
  if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
    r.width = sqlite3_column_int(stmt, 4);
  if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
    r.height = sqlite3_column_int(stmt, 5);
  if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
    r.codec = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
  if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
    r.quality = sqlite3_column_int(stmt, 7);
  if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
    r.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
  if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
    r.source = sqlite3_column_int(stmt, 9);
  else
    r.source = 0;
  return r;
}

}  // namespace

std::optional<Database::LevelRow> Database::find_best_level(
    std::string_view content_id, int max_edge, int frame_idx) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT content_id, max_edge, frame_idx, pts_ms, width, height, codec, "
      "quality, path, source FROM levels WHERE content_id = ?1 AND frame_idx = ?2 "
      "AND max_edge <= ?3 ORDER BY max_edge DESC LIMIT 1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(),
                    static_cast<int>(content_id.size()), SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, frame_idx);
  sqlite3_bind_int(stmt, 3, max_edge);
  auto out = step_level_row(stmt);
  sqlite3_finalize(stmt);
  return out;
}

std::optional<Database::LevelRow> Database::find_smallest_level_ge(
    std::string_view content_id, int min_edge, int frame_idx) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT content_id, max_edge, frame_idx, pts_ms, width, height, codec, "
      "quality, path, source FROM levels WHERE content_id = ?1 AND frame_idx = ?2 "
      "AND max_edge >= ?3 ORDER BY max_edge ASC LIMIT 1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(),
                    static_cast<int>(content_id.size()), SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, frame_idx);
  sqlite3_bind_int(stmt, 3, min_edge);
  auto out = step_level_row(stmt);
  sqlite3_finalize(stmt);
  return out;
}

std::vector<Database::LevelRow> Database::list_levels(
    std::string_view content_id, int limit) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT content_id, max_edge, frame_idx, pts_ms, width, height, codec, "
      "quality, path, source FROM levels WHERE content_id = ?1 "
      "ORDER BY frame_idx, max_edge LIMIT ?2;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(),
                    static_cast<int>(content_id.size()), SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, limit);
  std::vector<LevelRow> rows;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    LevelRow r;
    r.content_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    r.max_edge = sqlite3_column_int(stmt, 1);
    r.frame_idx = sqlite3_column_int(stmt, 2);
    if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
      r.pts_ms = sqlite3_column_int64(stmt, 3);
    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
      r.width = sqlite3_column_int(stmt, 4);
    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
      r.height = sqlite3_column_int(stmt, 5);
    if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
      r.codec = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
      r.quality = sqlite3_column_int(stmt, 7);
    if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
      r.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
    if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
      r.source = sqlite3_column_int(stmt, 9);
    else
      r.source = 0;
    rows.push_back(std::move(r));
  }
  sqlite3_finalize(stmt);
  return rows;
}


void Database::replace_archive_entries(
    std::string_view archive_uri, const std::vector<ArchiveEntryRow>& entries) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* del = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "DELETE FROM archive_entries WHERE archive_uri = ?1;",
                         -1, &del, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(del, 1, archive_uri.data(),
                    static_cast<int>(archive_uri.size()), SQLITE_STATIC);
  if (sqlite3_step(del) != SQLITE_DONE) {
    sqlite3_finalize(del);
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_finalize(del);

  sqlite3_stmt* ins = nullptr;
  const char* sql =
      "INSERT INTO archive_entries(archive_uri, member_path, uncompressed_size) "
      "VALUES(?1,?2,?3);";
  if (sqlite3_prepare_v2(db_, sql, -1, &ins, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  for (const auto& e : entries) {
    sqlite3_bind_text(ins, 1, e.archive_uri.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 2, e.member_path.c_str(), -1, SQLITE_TRANSIENT);
    if (e.uncompressed_size)
      sqlite3_bind_int64(ins, 3, *e.uncompressed_size);
    else
      sqlite3_bind_null(ins, 3);
    if (sqlite3_step(ins) != SQLITE_DONE) {
      sqlite3_finalize(ins);
      throw std::runtime_error(sqlite3_errmsg(db_));
    }
    sqlite3_reset(ins);
  }
  sqlite3_finalize(ins);
}

std::vector<Database::ArchiveEntryRow> Database::list_archive_entries(
    std::string_view archive_uri, int limit) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT archive_uri, member_path, uncompressed_size FROM archive_entries "
      "WHERE archive_uri = ?1 ORDER BY member_path LIMIT ?2;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, archive_uri.data(),
                    static_cast<int>(archive_uri.size()), SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, limit);
  std::vector<ArchiveEntryRow> rows;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    ArchiveEntryRow r;
    r.archive_uri = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    r.member_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    if (sqlite3_column_type(stmt, 2) != SQLITE_NULL)
      r.uncompressed_size = sqlite3_column_int64(stmt, 2);
    rows.push_back(std::move(r));
  }
  sqlite3_finalize(stmt);
  return rows;
}


void Database::add_tag(std::string_view content_id, std::string_view tag,
                       std::string_view source) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  if (content_id.empty() || tag.empty()) return;
  // trim-ish: reject all-whitespace later via empty after simple skip
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT OR IGNORE INTO tags(content_id, tag, source, created_at) "
      "VALUES(?1,?2,?3,strftime('%s','now'));";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(),
                    static_cast<int>(content_id.size()), SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, tag.data(), static_cast<int>(tag.size()),
                    SQLITE_STATIC);
  if (source.empty())
    sqlite3_bind_null(stmt, 3);
  else
    sqlite3_bind_text(stmt, 3, source.data(), static_cast<int>(source.size()),
                      SQLITE_STATIC);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_finalize(stmt);
}

bool Database::remove_tag(std::string_view content_id, std::string_view tag) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  if (content_id.empty() || tag.empty()) return false;
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "DELETE FROM tags WHERE content_id = ?1 AND tag = ?2;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(),
                    static_cast<int>(content_id.size()), SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, tag.data(), static_cast<int>(tag.size()),
                    SQLITE_STATIC);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  const bool changed = sqlite3_changes(db_) > 0;
  sqlite3_finalize(stmt);
  return changed;
}

std::vector<std::string> Database::tags_for_content(
    std::string_view content_id) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  std::vector<std::string> out;
  if (content_id.empty()) return out;
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT tag FROM tags WHERE content_id = ?1 ORDER BY tag;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(),
                    static_cast<int>(content_id.size()), SQLITE_STATIC);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    out.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
  }
  sqlite3_finalize(stmt);
  return out;
}

std::vector<std::string> Database::content_ids_for_tag(std::string_view tag,
                                                      int limit) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  std::vector<std::string> out;
  if (tag.empty()) return out;
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT content_id FROM tags WHERE tag = ?1 ORDER BY content_id LIMIT ?2;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, tag.data(), static_cast<int>(tag.size()),
                    SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, limit);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    out.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
  }
  sqlite3_finalize(stmt);
  return out;
}


void Database::upsert_tile(const TileRow& row) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO tiles(content_id, scale, x, y, width, height, codec, quality, "
      "source) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9) "
      "ON CONFLICT(content_id, scale, x, y) DO UPDATE SET "
      "width=excluded.width, height=excluded.height, codec=excluded.codec, "
      "quality=excluded.quality, source=excluded.source;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, row.content_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, row.scale);
  sqlite3_bind_int(stmt, 3, row.x);
  sqlite3_bind_int(stmt, 4, row.y);
  if (row.width) sqlite3_bind_int(stmt, 5, *row.width);
  else sqlite3_bind_null(stmt, 5);
  if (row.height) sqlite3_bind_int(stmt, 6, *row.height);
  else sqlite3_bind_null(stmt, 6);
  if (row.codec)
    sqlite3_bind_text(stmt, 7, row.codec->c_str(), -1, SQLITE_TRANSIENT);
  else
    sqlite3_bind_null(stmt, 7);
  if (row.quality) sqlite3_bind_int(stmt, 8, *row.quality);
  else sqlite3_bind_null(stmt, 8);
  sqlite3_bind_int(stmt, 9, row.source);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_finalize(stmt);
}

std::optional<Database::TileRow> Database::find_tile(std::string_view content_id,
                                                     int scale, int x,
                                                     int y) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT content_id, scale, x, y, width, height, codec, quality, source FROM tiles "
      "WHERE content_id = ?1 AND scale = ?2 AND x = ?3 AND y = ?4;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(),
                    static_cast<int>(content_id.size()), SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, scale);
  sqlite3_bind_int(stmt, 3, x);
  sqlite3_bind_int(stmt, 4, y);
  std::optional<TileRow> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    TileRow r;
    r.content_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    r.scale = sqlite3_column_int(stmt, 1);
    r.x = sqlite3_column_int(stmt, 2);
    r.y = sqlite3_column_int(stmt, 3);
    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
      r.width = sqlite3_column_int(stmt, 4);
    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
      r.height = sqlite3_column_int(stmt, 5);
    if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
      r.codec = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
      r.quality = sqlite3_column_int(stmt, 7);
    if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
      r.source = sqlite3_column_int(stmt, 8);
    out = std::move(r);
  }
  sqlite3_finalize(stmt);
  return out;
}

void Database::delete_tile(std::string_view content_id, int scale, int x, int y) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "DELETE FROM tiles WHERE content_id = ?1 AND scale = ?2 AND x = ?3 AND y = ?4;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
  sqlite3_bind_text(stmt, 1, content_id.data(), static_cast<int>(content_id.size()),
                    SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, scale);
  sqlite3_bind_int(stmt, 3, x);
  sqlite3_bind_int(stmt, 4, y);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

bool Database::tile_min_max_scale(std::string_view content_id, int& min_scale_out,
                                  int& max_scale_out) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT MIN(scale), MAX(scale) FROM tiles WHERE content_id = ?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(),
                    static_cast<int>(content_id.size()), SQLITE_STATIC);
  bool ok = false;
  if (sqlite3_step(stmt) == SQLITE_ROW &&
      sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
    min_scale_out = sqlite3_column_int(stmt, 0);
    max_scale_out = sqlite3_column_int(stmt, 1);
    ok = true;
  }
  sqlite3_finalize(stmt);
  return ok;
}

std::vector<Database::TileRow> Database::list_tiles(std::string_view content_id,
                                                    int limit) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  std::vector<TileRow> rows;
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT content_id, scale, x, y, width, height, codec, quality, source FROM tiles "
      "WHERE content_id = ?1 ORDER BY scale, y, x LIMIT ?2;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(),
                    static_cast<int>(content_id.size()), SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, limit);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    TileRow r;
    r.content_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    r.scale = sqlite3_column_int(stmt, 1);
    r.x = sqlite3_column_int(stmt, 2);
    r.y = sqlite3_column_int(stmt, 3);
    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
      r.width = sqlite3_column_int(stmt, 4);
    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
      r.height = sqlite3_column_int(stmt, 5);
    if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
      r.codec = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
      r.quality = sqlite3_column_int(stmt, 7);
    if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
      r.source = sqlite3_column_int(stmt, 8);
    rows.push_back(std::move(r));
  }
  sqlite3_finalize(stmt);
  return rows;
}


std::optional<std::vector<std::uint8_t>> Database::get_lqip(
    std::string_view content_id) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "SELECT lqip FROM content WHERE content_id = ?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }
  sqlite3_bind_text(stmt, 1, content_id.data(), static_cast<int>(content_id.size()),
                    SQLITE_STATIC);
  std::optional<std::vector<std::uint8_t>> out;
  if (sqlite3_step(stmt) == SQLITE_ROW &&
      sqlite3_column_type(stmt, 0) == SQLITE_BLOB) {
    const auto* p = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt, 0));
    const int n = sqlite3_column_bytes(stmt, 0);
    if (p && n > 0) {
      out = std::vector<std::uint8_t>(p, p + n);
    }
  }
  sqlite3_finalize(stmt);
  return out;
}

void Database::set_lqip(std::string_view content_id, int kind,
                       std::span<const std::uint8_t> bytes) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  if (content_id.empty() || bytes.empty()) return;
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "UPDATE content SET lqip = ?1, lqip_kind = ?2, updated_at = ?3 "
      "WHERE content_id = ?4;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_blob(stmt, 1, bytes.data(), static_cast<int>(bytes.size()),
                    SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, kind);
  sqlite3_bind_int64(stmt, 3, now_unix_s());
  sqlite3_bind_text(stmt, 4, content_id.data(), static_cast<int>(content_id.size()),
                    SQLITE_STATIC);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_finalize(stmt);
}

std::int64_t Database::delete_tiles_below_scale(int min_scale_keep) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "DELETE FROM tiles WHERE scale < ?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int(stmt, 1, min_scale_keep);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  const std::int64_t n = sqlite3_changes(db_);
  sqlite3_finalize(stmt);
  return n;
}

std::vector<std::string> Database::list_orphan_content_ids(int limit) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT c.content_id FROM content c "
      "WHERE NOT EXISTS (SELECT 1 FROM locators l WHERE l.content_id = c.content_id) "
      "LIMIT ?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_int(stmt, 1, limit);
  std::vector<std::string> out;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (id) out.emplace_back(id);
  }
  sqlite3_finalize(stmt);
  return out;
}

std::vector<Database::LocatorRow> Database::list_dead_path_locators(
    int limit) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  // Load candidates; filter by filesystem outside SQL.
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT uri, content_id, outer_path, member_path, size, mtime_ns "
      "FROM locators WHERE outer_path IS NOT NULL AND outer_path != '' "
      "LIMIT ?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  // Over-fetch then filter; caller limit is dead count target.
  sqlite3_bind_int(stmt, 1, std::max(limit * 4, limit));
  std::vector<LocatorRow> out;
  while (sqlite3_step(stmt) == SQLITE_ROW &&
         static_cast<int>(out.size()) < limit) {
    LocatorRow r = locator_from_stmt(stmt);
    if (!r.outer_path) continue;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(*r.outer_path, ec)) {
      out.push_back(std::move(r));
    }
  }
  sqlite3_finalize(stmt);
  return out;
}

void Database::delete_locator(std::string_view uri) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "DELETE FROM locators WHERE uri = ?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
  sqlite3_bind_text(stmt, 1, uri.data(), static_cast<int>(uri.size()),
                    SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void Database::purge_content_metadata(std::string_view content_id) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  auto run = [&](const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, content_id.data(),
                      static_cast<int>(content_id.size()), SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  };
  run("DELETE FROM tiles WHERE content_id = ?1;");
  run("DELETE FROM levels WHERE content_id = ?1;");
  run("DELETE FROM text_layers WHERE content_id = ?1;");
  run("DELETE FROM document_outlines WHERE content_id = ?1;");
  run("DELETE FROM tags WHERE content_id = ?1;");
  run("DELETE FROM content WHERE content_id = ?1;");
}


void Database::upsert_text_layer(std::string_view content_id, int page_1based,
                                 std::string_view layout_key, double page_x0,
                                 double page_y0, double page_x1, double page_y1,
                                 const std::vector<std::uint8_t>& payload) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO text_layers(content_id, page_1based, layout_key, "
      "page_x0, page_y0, page_x1, page_y1, payload, updated_at) "
      "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,strftime('%s','now')) "
      "ON CONFLICT(content_id, page_1based, layout_key) DO UPDATE SET "
      "page_x0=excluded.page_x0, page_y0=excluded.page_y0, "
      "page_x1=excluded.page_x1, page_y1=excluded.page_y1, "
      "payload=excluded.payload, updated_at=excluded.updated_at;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(), static_cast<int>(content_id.size()),
                    SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, page_1based);
  sqlite3_bind_text(stmt, 3, layout_key.data(), static_cast<int>(layout_key.size()),
                    SQLITE_STATIC);
  sqlite3_bind_double(stmt, 4, page_x0);
  sqlite3_bind_double(stmt, 5, page_y0);
  sqlite3_bind_double(stmt, 6, page_x1);
  sqlite3_bind_double(stmt, 7, page_y1);
  sqlite3_bind_blob(stmt, 8, payload.data(), static_cast<int>(payload.size()),
                    SQLITE_STATIC);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_finalize(stmt);
}

std::optional<std::vector<std::uint8_t>> Database::find_text_layer(
    std::string_view content_id, int page_1based,
    std::string_view layout_key) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT payload FROM text_layers WHERE content_id=?1 AND page_1based=?2 "
      "AND layout_key=?3;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }
  sqlite3_bind_text(stmt, 1, content_id.data(), static_cast<int>(content_id.size()),
                    SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, page_1based);
  sqlite3_bind_text(stmt, 3, layout_key.data(), static_cast<int>(layout_key.size()),
                    SQLITE_STATIC);
  std::optional<std::vector<std::uint8_t>> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const void* blob = sqlite3_column_blob(stmt, 0);
    const int n = sqlite3_column_bytes(stmt, 0);
    if (blob && n > 0) {
      const auto* b = static_cast<const std::uint8_t*>(blob);
      out = std::vector<std::uint8_t>(b, b + n);
    }
  }
  sqlite3_finalize(stmt);
  return out;
}

void Database::delete_text_layers(std::string_view content_id) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "DELETE FROM text_layers WHERE content_id=?1;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(), static_cast<int>(content_id.size()),
                    SQLITE_STATIC);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  const char* sql2 = "DELETE FROM document_outlines WHERE content_id=?1;";
  if (sqlite3_prepare_v2(db_, sql2, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(), static_cast<int>(content_id.size()),
                    SQLITE_STATIC);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void Database::upsert_document_outline(std::string_view content_id,
                                       std::string_view layout_key,
                                       const std::vector<std::uint8_t>& payload) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO document_outlines(content_id, layout_key, payload, updated_at) "
      "VALUES(?1,?2,?3,strftime('%s','now')) "
      "ON CONFLICT(content_id, layout_key) DO UPDATE SET "
      "payload=excluded.payload, updated_at=excluded.updated_at;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, content_id.data(), static_cast<int>(content_id.size()),
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, layout_key.data(), static_cast<int>(layout_key.size()),
                    SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 3, payload.data(), static_cast<int>(payload.size()),
                    SQLITE_STATIC);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3_finalize(stmt);
}

std::optional<std::vector<std::uint8_t>> Database::find_document_outline(
    std::string_view content_id, std::string_view layout_key) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT payload FROM document_outlines WHERE content_id=?1 AND layout_key=?2;";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }
  sqlite3_bind_text(stmt, 1, content_id.data(), static_cast<int>(content_id.size()),
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, layout_key.data(), static_cast<int>(layout_key.size()),
                    SQLITE_STATIC);
  std::optional<std::vector<std::uint8_t>> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const void* blob = sqlite3_column_blob(stmt, 0);
    const int n = sqlite3_column_bytes(stmt, 0);
    if (blob && n > 0) {
      const auto* b = static_cast<const std::uint8_t*>(blob);
      out = std::vector<std::uint8_t>(b, b + n);
    }
  }
  sqlite3_finalize(stmt);
  return out;
}


}  // namespace thumtoo
