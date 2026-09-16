// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/store.hpp"
#include "thumtoo/constants.hpp"

#include "sqlite3.h"

#include <chrono>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace thumtoo {
namespace {

std::int64_t now_unix_s() {
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

[[noreturn]] void throw_sqlite(sqlite3* db, const char* what) {
  const char* msg = db ? sqlite3_errmsg(db) : "null db";
  throw std::runtime_error(std::string(what) + ": " + (msg ? msg : ""));
}

void exec_sql(sqlite3* db, const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string msg = err ? err : "sqlite3_exec failed";
    sqlite3_free(err);
    throw std::runtime_error(msg);
  }
}

std::optional<std::string> meta_get_db(sqlite3* db, std::string_view key) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT value FROM schema_meta WHERE key = ?1;", -1,
                         &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(db, "prepare meta_get");
  }
  sqlite3_bind_text(stmt, 1, key.data(), static_cast<int>(key.size()),
                    SQLITE_STATIC);
  std::optional<std::string> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* v =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (v) out = v;
  }
  sqlite3_finalize(stmt);
  return out;
}

void meta_set_db(sqlite3* db, std::string_view key, std::string_view value) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db,
                         "INSERT INTO schema_meta(key, value) VALUES(?1, ?2) "
                         "ON CONFLICT(key) DO UPDATE SET value = excluded.value;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(db, "prepare meta_set");
  }
  sqlite3_bind_text(stmt, 1, key.data(), static_cast<int>(key.size()),
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, value.data(), static_cast<int>(value.size()),
                    SQLITE_STATIC);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw_sqlite(db, "step meta_set");
  }
  sqlite3_finalize(stmt);
}

int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

constexpr char kIndexSchemaSql[] = R"SQL(
CREATE TABLE IF NOT EXISTS schema_meta (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS blob (
  id         INTEGER PRIMARY KEY,
  size       INTEGER,
  status     INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS hash_algo (
  id   INTEGER PRIMARY KEY,
  name TEXT NOT NULL UNIQUE
);
CREATE TABLE IF NOT EXISTS blob_hash (
  blob_id INTEGER NOT NULL REFERENCES blob(id) ON DELETE CASCADE,
  algo_id INTEGER NOT NULL REFERENCES hash_algo(id),
  digest  BLOB NOT NULL,
  PRIMARY KEY (blob_id, algo_id),
  UNIQUE (algo_id, digest)
);
CREATE INDEX IF NOT EXISTS idx_blob_hash_digest ON blob_hash(algo_id, digest);
CREATE TABLE IF NOT EXISTS locator (
  id         INTEGER PRIMARY KEY,
  uri        TEXT NOT NULL UNIQUE,
  blob_id    INTEGER REFERENCES blob(id) ON DELETE SET NULL,
  size       INTEGER,
  mtime_ns   INTEGER,
  updated_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_locator_blob ON locator(blob_id);
CREATE TABLE IF NOT EXISTS codec (
  id   INTEGER PRIMARY KEY,
  name TEXT NOT NULL UNIQUE
);
CREATE TABLE IF NOT EXISTS media (
  id           INTEGER PRIMARY KEY,
  blob_id      INTEGER NOT NULL REFERENCES blob(id) ON DELETE CASCADE,
  kind         INTEGER NOT NULL DEFAULT 0,
  width        INTEGER,
  height       INTEGER,
  duration_ms  INTEGER,
  page_count   INTEGER,
  still_count  INTEGER,
  status       INTEGER NOT NULL DEFAULT 0,
  error_code   TEXT,
  updated_at   INTEGER NOT NULL,
  UNIQUE (blob_id, kind)
);
CREATE INDEX IF NOT EXISTS idx_media_blob ON media(blob_id);
CREATE TABLE IF NOT EXISTS region (
  id         INTEGER PRIMARY KEY,
  media_id   INTEGER NOT NULL REFERENCES media(id) ON DELETE CASCADE,
  kind       INTEGER NOT NULL,
  key        TEXT NOT NULL,
  ordinal    INTEGER,
  UNIQUE (media_id, kind, key)
);
CREATE INDEX IF NOT EXISTS idx_region_media ON region(media_id);
CREATE TABLE IF NOT EXISTS region_key (
  region_id INTEGER NOT NULL REFERENCES region(id) ON DELETE CASCADE,
  key_kind  INTEGER NOT NULL,
  key       TEXT NOT NULL,
  PRIMARY KEY (region_id, key_kind)
);
CREATE TABLE IF NOT EXISTS tile (
  media_id   INTEGER NOT NULL REFERENCES media(id) ON DELETE CASCADE,
  region_id  INTEGER NOT NULL REFERENCES region(id) ON DELETE CASCADE,
  scale      INTEGER NOT NULL,
  x          INTEGER NOT NULL,
  y          INTEGER NOT NULL,
  width      INTEGER NOT NULL,
  height     INTEGER NOT NULL,
  codec_id   INTEGER NOT NULL REFERENCES codec(id),
  quality    INTEGER,
  PRIMARY KEY (media_id, region_id, scale, x, y)
);
)SQL";

constexpr char kBulkSchemaSql[] = R"SQL(
CREATE TABLE IF NOT EXISTS schema_meta (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS tile_blob (
  media_id   INTEGER NOT NULL,
  region_id  INTEGER NOT NULL,
  scale      INTEGER NOT NULL,
  x          INTEGER NOT NULL,
  y          INTEGER NOT NULL,
  codec_id   INTEGER NOT NULL,
  data       BLOB NOT NULL,
  PRIMARY KEY (media_id, region_id, scale, x, y)
);
CREATE TABLE IF NOT EXISTS http_body (
  url         TEXT PRIMARY KEY,
  fetched_at  INTEGER NOT NULL,
  blob_id     INTEGER,
  data        BLOB NOT NULL
);
)SQL";

constexpr char kUserSchemaSql[] = R"SQL(
CREATE TABLE IF NOT EXISTS schema_meta (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS tag_def (
  id         INTEGER PRIMARY KEY,
  name       TEXT NOT NULL UNIQUE,
  label      TEXT,
  color      TEXT,
  badge      TEXT,
  created_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS blob_tag (
  blob_ref   TEXT NOT NULL,
  tag_id     INTEGER NOT NULL REFERENCES tag_def(id) ON DELETE CASCADE,
  tagged_at  INTEGER NOT NULL,
  source     TEXT,
  PRIMARY KEY (blob_ref, tag_id)
);
CREATE INDEX IF NOT EXISTS idx_blob_tag_tag ON blob_tag(tag_id);
CREATE TABLE IF NOT EXISTS collection (
  id         INTEGER PRIMARY KEY,
  label      TEXT,
  color      TEXT,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS collection_member (
  collection_id INTEGER NOT NULL REFERENCES collection(id) ON DELETE CASCADE,
  blob_ref      TEXT NOT NULL,
  ordinal       INTEGER,
  path_key      TEXT,
  PRIMARY KEY (collection_id, blob_ref)
);
CREATE TABLE IF NOT EXISTS bookmark (
  id         INTEGER PRIMARY KEY,
  target_ref TEXT NOT NULL,
  title      TEXT,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_bookmark_target ON bookmark(target_ref);
CREATE TABLE IF NOT EXISTS link_edge (
  id         INTEGER PRIMARY KEY,
  from_ref   TEXT NOT NULL,
  to_ref     TEXT NOT NULL,
  rel        TEXT,
  source     INTEGER NOT NULL DEFAULT 2,
  created_at INTEGER NOT NULL,
  UNIQUE (from_ref, to_ref, rel, source)
);
CREATE INDEX IF NOT EXISTS idx_user_link_from ON link_edge(from_ref);
CREATE INDEX IF NOT EXISTS idx_user_link_to ON link_edge(to_ref);
CREATE TABLE IF NOT EXISTS annotation (
  id         INTEGER PRIMARY KEY,
  target_ref TEXT NOT NULL,
  kind       INTEGER NOT NULL,
  body       TEXT,
  geom       BLOB,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_annotation_target ON annotation(target_ref);
)SQL";

}  // namespace

Store::Store(sqlite3* index, sqlite3* bulk, sqlite3* user,
             std::filesystem::path cache_root, std::filesystem::path data_root,
             int index_schema_version)
    : index_(index),
      bulk_(bulk),
      user_(user),
      cache_root_(std::move(cache_root)),
      data_root_(std::move(data_root)),
      index_schema_version_(index_schema_version) {}

Store::Store(Store&& other) noexcept
    : index_(std::exchange(other.index_, nullptr)),
      bulk_(std::exchange(other.bulk_, nullptr)),
      user_(std::exchange(other.user_, nullptr)),
      cache_root_(std::move(other.cache_root_)),
      data_root_(std::move(other.data_root_)),
      index_schema_version_(other.index_schema_version_) {}

Store& Store::operator=(Store&& other) noexcept {
  if (this != &other) {
    close_sqlite(index_);
    close_sqlite(bulk_);
    close_sqlite(user_);
    index_ = std::exchange(other.index_, nullptr);
    bulk_ = std::exchange(other.bulk_, nullptr);
    user_ = std::exchange(other.user_, nullptr);
    cache_root_ = std::move(other.cache_root_);
    data_root_ = std::move(other.data_root_);
    index_schema_version_ = other.index_schema_version_;
  }
  return *this;
}

Store::~Store() {
  close_sqlite(index_);
  close_sqlite(bulk_);
  close_sqlite(user_);
}

void Store::close_sqlite(sqlite3*& db) {
  if (db) {
    sqlite3_close(db);
    db = nullptr;
  }
}

sqlite3* Store::open_sqlite(const std::filesystem::path& path) {
  sqlite3* db = nullptr;
  if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK) {
    const char* msg = db ? sqlite3_errmsg(db) : "open failed";
    std::string err = path.string() + ": " + (msg ? msg : "");
    if (db) sqlite3_close(db);
    throw std::runtime_error(err);
  }
  exec_sql(db, "PRAGMA journal_mode=WAL;");
  exec_sql(db, "PRAGMA busy_timeout=5000;");
  exec_sql(db, "PRAGMA foreign_keys=ON;");
  return db;
}

void Store::exec_index(const char* sql) const { exec_sql(index_, sql); }
void Store::exec_bulk(const char* sql) const { exec_sql(bulk_, sql); }
void Store::exec_user(const char* sql) const { exec_sql(user_, sql); }

void Store::seed_lookups() {
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT OR IGNORE INTO hash_algo(id, name) VALUES(?1, ?2);";
  if (sqlite3_prepare_v2(index_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare hash_algo seed");
  }
  auto bind_algo = [&](int id, const char* name) {
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      throw_sqlite(index_, "seed hash_algo");
    }
  };
  bind_algo(static_cast<int>(HashAlgoId::Sha256), "sha256");
  bind_algo(static_cast<int>(HashAlgoId::Sha1), "sha1");
  bind_algo(static_cast<int>(HashAlgoId::Md5), "md5");
  bind_algo(static_cast<int>(HashAlgoId::Crc32), "crc32");
  sqlite3_finalize(stmt);

  if (sqlite3_prepare_v2(index_,
                         "INSERT OR IGNORE INTO codec(id, name) VALUES(?1, ?2);",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare codec seed");
  }
  auto bind_codec = [&](int id, const char* name) {
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      throw_sqlite(index_, "seed codec");
    }
  };
  bind_codec(static_cast<int>(CodecId::Jpeg), "jpeg");
  bind_codec(static_cast<int>(CodecId::Jxl), "jxl");
  bind_codec(static_cast<int>(CodecId::Png), "png");
  sqlite3_finalize(stmt);
}

void Store::migrate_or_init_index() {
  exec_index(kIndexSchemaSql);
  auto ver = meta_get_db(index_, kSchemaMetaVersionKey);
  if (!ver) {
    meta_set_db(index_, kSchemaMetaVersionKey,
                std::to_string(kStoreIndexSchemaVersion));
    index_schema_version_ = kStoreIndexSchemaVersion;
    seed_lookups();
    return;
  }
  index_schema_version_ = std::stoi(*ver);
  if (index_schema_version_ > kStoreIndexSchemaVersion) {
    throw std::runtime_error(
        "index schema_version is newer than this build");
  }
  if (index_schema_version_ < kStoreIndexSchemaVersion) {
    // Redesign has no in-place migration from legacy ladders or older epochs.
    throw std::runtime_error(
        "index schema_version is outdated; delete cache index.sqlite and retry");
  }
  seed_lookups();
}

void Store::migrate_or_init_bulk() {
  exec_bulk(kBulkSchemaSql);
  auto ver = meta_get_db(bulk_, kSchemaMetaVersionKey);
  if (!ver) {
    meta_set_db(bulk_, kSchemaMetaVersionKey,
                std::to_string(kStoreBulkSchemaVersion));
    return;
  }
  const int v = std::stoi(*ver);
  if (v > kStoreBulkSchemaVersion) {
    throw std::runtime_error("bulk schema_version is newer than this build");
  }
  if (v < kStoreBulkSchemaVersion) {
    throw std::runtime_error(
        "bulk schema_version is outdated; delete cache bulk.sqlite and retry");
  }
}

void Store::migrate_or_init_user() {
  exec_user(kUserSchemaSql);
  auto ver = meta_get_db(user_, kSchemaMetaVersionKey);
  if (!ver) {
    meta_set_db(user_, kSchemaMetaVersionKey,
                std::to_string(kStoreUserSchemaVersion));
    return;
  }
  const int v = std::stoi(*ver);
  if (v > kStoreUserSchemaVersion) {
    throw std::runtime_error("user schema_version is newer than this build");
  }
  if (v < kStoreUserSchemaVersion) {
    // User data: refuse silent wipe; operator must migrate or reset explicitly.
    throw std::runtime_error(
        "user schema_version is outdated; migrate or replace user.sqlite");
  }
}

Store Store::open(const std::filesystem::path& cache_root) {
  Paths p;
  p.cache_root = cache_root;
  p.data_root = cache_root;
  return open(p);
}

Store Store::open(const Paths& paths) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(paths.cache_root, ec);
  if (ec) {
    throw std::runtime_error("create cache_root: " + ec.message());
  }
  fs::create_directories(paths.data_root, ec);
  if (ec) {
    throw std::runtime_error("create data_root: " + ec.message());
  }

  const fs::path index_path = paths.cache_root / "index.sqlite";
  const fs::path bulk_path = paths.cache_root / "bulk.sqlite";
  const fs::path user_path = paths.data_root / "user.sqlite";

  // Legacy thumtoo index (schema 1–4, content_id TEXT): replace index+bulk only.
  if (fs::exists(index_path)) {
    sqlite3* probe = nullptr;
    if (sqlite3_open(index_path.string().c_str(), &probe) == SQLITE_OK) {
      auto ver = meta_get_db(probe, kSchemaMetaVersionKey);
      sqlite3_close(probe);
      if (ver) {
        const int v = std::stoi(*ver);
        // Redesign epoch starts at kStoreIndexSchemaVersion (100+).
        // Legacy used 1–4. Anything in between is also treated as obsolete cache.
        if (v < kStoreIndexSchemaVersion) {
          fs::remove(index_path, ec);
          fs::remove(bulk_path, ec);
          // WAL sidecars
          fs::remove(fs::path(index_path.string() + "-wal"), ec);
          fs::remove(fs::path(index_path.string() + "-shm"), ec);
          fs::remove(fs::path(bulk_path.string() + "-wal"), ec);
          fs::remove(fs::path(bulk_path.string() + "-shm"), ec);
        }
      }
    } else if (probe) {
      sqlite3_close(probe);
    }
  }

  sqlite3* index = open_sqlite(index_path);
  sqlite3* bulk = open_sqlite(bulk_path);
  sqlite3* user = open_sqlite(user_path);

  Store store(index, bulk, user, paths.cache_root, paths.data_root, 0);
  try {
    store.migrate_or_init_index();
    store.migrate_or_init_bulk();
    store.migrate_or_init_user();
  } catch (...) {
    // Store destructor will close.
    throw;
  }
  return store;
}

std::optional<std::string> Store::meta_get(std::string_view key) const {
  return meta_get_db(index_, key);
}

void Store::meta_set(std::string_view key, std::string_view value) {
  meta_set_db(index_, key, value);
}

std::int64_t Store::insert_blob(std::optional<std::int64_t> size,
                                BlobStatus status) {
  const std::int64_t now = now_unix_s();
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "INSERT INTO blob(size, status, created_at, updated_at) "
                         "VALUES(?1, ?2, ?3, ?4);",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare insert_blob");
  }
  if (size) {
    sqlite3_bind_int64(stmt, 1, *size);
  } else {
    sqlite3_bind_null(stmt, 1);
  }
  sqlite3_bind_int(stmt, 2, static_cast<int>(status));
  sqlite3_bind_int64(stmt, 3, now);
  sqlite3_bind_int64(stmt, 4, now);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw_sqlite(index_, "step insert_blob");
  }
  sqlite3_finalize(stmt);
  return static_cast<std::int64_t>(sqlite3_last_insert_rowid(index_));
}

std::optional<Store::BlobRow> Store::find_blob(std::int64_t id) const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "SELECT id, size, status, created_at, updated_at "
                         "FROM blob WHERE id = ?1;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare find_blob");
  }
  sqlite3_bind_int64(stmt, 1, id);
  std::optional<BlobRow> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    BlobRow r;
    r.id = sqlite3_column_int64(stmt, 0);
    if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
      r.size = sqlite3_column_int64(stmt, 1);
    }
    r.status = static_cast<BlobStatus>(sqlite3_column_int(stmt, 2));
    r.created_at = sqlite3_column_int64(stmt, 3);
    r.updated_at = sqlite3_column_int64(stmt, 4);
    out = std::move(r);
  }
  sqlite3_finalize(stmt);
  return out;
}

void Store::set_blob_size(std::int64_t id, std::int64_t size) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "UPDATE blob SET size = ?1, updated_at = ?2 WHERE id = ?3;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare set_blob_size");
  }
  sqlite3_bind_int64(stmt, 1, size);
  sqlite3_bind_int64(stmt, 2, now_unix_s());
  sqlite3_bind_int64(stmt, 3, id);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw_sqlite(index_, "step set_blob_size");
  }
  sqlite3_finalize(stmt);
}

void Store::set_blob_status(std::int64_t id, BlobStatus status) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(
          index_,
          "UPDATE blob SET status = ?1, updated_at = ?2 WHERE id = ?3;", -1,
          &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare set_blob_status");
  }
  sqlite3_bind_int(stmt, 1, static_cast<int>(status));
  sqlite3_bind_int64(stmt, 2, now_unix_s());
  sqlite3_bind_int64(stmt, 3, id);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw_sqlite(index_, "step set_blob_status");
  }
  sqlite3_finalize(stmt);
}

void Store::put_hash(std::int64_t blob_id, HashAlgoId algo,
                     std::span<const std::uint8_t> digest) {
  if (digest.empty()) {
    throw std::invalid_argument("put_hash: empty digest");
  }
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "INSERT INTO blob_hash(blob_id, algo_id, digest) "
                         "VALUES(?1, ?2, ?3) "
                         "ON CONFLICT(blob_id, algo_id) DO UPDATE SET "
                         "digest = excluded.digest;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare put_hash");
  }
  sqlite3_bind_int64(stmt, 1, blob_id);
  sqlite3_bind_int(stmt, 2, static_cast<int>(algo));
  sqlite3_bind_blob(stmt, 3, digest.data(), static_cast<int>(digest.size()),
                    SQLITE_STATIC);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw_sqlite(index_, "step put_hash");
  }
  sqlite3_finalize(stmt);
}

std::optional<std::int64_t> Store::find_blob_by_hash(
    HashAlgoId algo, std::span<const std::uint8_t> digest) const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "SELECT blob_id FROM blob_hash "
                         "WHERE algo_id = ?1 AND digest = ?2;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare find_blob_by_hash");
  }
  sqlite3_bind_int(stmt, 1, static_cast<int>(algo));
  sqlite3_bind_blob(stmt, 2, digest.data(), static_cast<int>(digest.size()),
                    SQLITE_STATIC);
  std::optional<std::int64_t> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    out = sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return out;
}

std::optional<std::vector<std::uint8_t>> Store::get_hash(
    std::int64_t blob_id, HashAlgoId algo) const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "SELECT digest FROM blob_hash "
                         "WHERE blob_id = ?1 AND algo_id = ?2;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare get_hash");
  }
  sqlite3_bind_int64(stmt, 1, blob_id);
  sqlite3_bind_int(stmt, 2, static_cast<int>(algo));
  std::optional<std::vector<std::uint8_t>> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const void* p = sqlite3_column_blob(stmt, 0);
    const int n = sqlite3_column_bytes(stmt, 0);
    if (p && n > 0) {
      const auto* bytes = static_cast<const std::uint8_t*>(p);
      out = std::vector<std::uint8_t>(bytes, bytes + n);
    }
  }
  sqlite3_finalize(stmt);
  return out;
}

std::optional<std::string> Store::blob_ref_sha256(std::int64_t blob_id) const {
  auto d = get_hash(blob_id, HashAlgoId::Sha256);
  if (!d || d->size() != 32) return std::nullopt;
  return format_blob_ref_sha256(*d);
}

std::optional<std::vector<std::uint8_t>> Store::parse_sha256_digest(
    std::string_view ref_or_hex) {
  std::string_view hex = ref_or_hex;
  constexpr std::string_view kPref = "blob:sha256:";
  constexpr std::string_view kPrefLegacy = "sha256:";
  if (hex.size() >= kPref.size() &&
      hex.substr(0, kPref.size()) == kPref) {
    hex = hex.substr(kPref.size());
  } else if (hex.size() >= kPrefLegacy.size() &&
             hex.substr(0, kPrefLegacy.size()) == kPrefLegacy) {
    hex = hex.substr(kPrefLegacy.size());
  }
  if (hex.size() != 64) return std::nullopt;
  std::vector<std::uint8_t> out(32);
  for (std::size_t i = 0; i < 32; ++i) {
    const int hi = hex_nibble(hex[i * 2]);
    const int lo = hex_nibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return std::nullopt;
    out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
  }
  return out;
}

std::string Store::format_blob_ref_sha256(
    std::span<const std::uint8_t> digest32) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out = "blob:sha256:";
  out.reserve(out.size() + digest32.size() * 2);
  for (std::uint8_t b : digest32) {
    out.push_back(kHex[(b >> 4) & 0xf]);
    out.push_back(kHex[b & 0xf]);
  }
  return out;
}

std::int64_t Store::upsert_locator(std::string_view uri,
                                   std::optional<std::int64_t> blob_id,
                                   std::optional<std::int64_t> size,
                                   std::optional<std::int64_t> mtime_ns) {
  const std::int64_t now = now_unix_s();
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "INSERT INTO locator(uri, blob_id, size, mtime_ns, "
                         "updated_at) VALUES(?1, ?2, ?3, ?4, ?5) "
                         "ON CONFLICT(uri) DO UPDATE SET "
                         "blob_id = COALESCE(excluded.blob_id, locator.blob_id), "
                         "size = COALESCE(excluded.size, locator.size), "
                         "mtime_ns = COALESCE(excluded.mtime_ns, locator.mtime_ns), "
                         "updated_at = excluded.updated_at;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare upsert_locator");
  }
  sqlite3_bind_text(stmt, 1, uri.data(), static_cast<int>(uri.size()),
                    SQLITE_STATIC);
  if (blob_id) {
    sqlite3_bind_int64(stmt, 2, *blob_id);
  } else {
    sqlite3_bind_null(stmt, 2);
  }
  if (size) {
    sqlite3_bind_int64(stmt, 3, *size);
  } else {
    sqlite3_bind_null(stmt, 3);
  }
  if (mtime_ns) {
    sqlite3_bind_int64(stmt, 4, *mtime_ns);
  } else {
    sqlite3_bind_null(stmt, 4);
  }
  sqlite3_bind_int64(stmt, 5, now);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw_sqlite(index_, "step upsert_locator");
  }
  sqlite3_finalize(stmt);

  auto row = find_locator(uri);
  if (!row) {
    throw std::runtime_error("upsert_locator: row missing after write");
  }
  return row->id;
}

void Store::bind_locator_blob(std::string_view uri, std::int64_t blob_id) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "UPDATE locator SET blob_id = ?1, updated_at = ?2 "
                         "WHERE uri = ?3;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare bind_locator_blob");
  }
  sqlite3_bind_int64(stmt, 1, blob_id);
  sqlite3_bind_int64(stmt, 2, now_unix_s());
  sqlite3_bind_text(stmt, 3, uri.data(), static_cast<int>(uri.size()),
                    SQLITE_STATIC);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw_sqlite(index_, "step bind_locator_blob");
  }
  sqlite3_finalize(stmt);
}

std::optional<Store::LocatorRow> Store::find_locator(
    std::string_view uri) const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "SELECT id, uri, blob_id, size, mtime_ns, updated_at "
                         "FROM locator WHERE uri = ?1;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare find_locator");
  }
  sqlite3_bind_text(stmt, 1, uri.data(), static_cast<int>(uri.size()),
                    SQLITE_STATIC);
  std::optional<LocatorRow> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    LocatorRow r;
    r.id = sqlite3_column_int64(stmt, 0);
    r.uri = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
      r.blob_id = sqlite3_column_int64(stmt, 2);
    }
    if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
      r.size = sqlite3_column_int64(stmt, 3);
    }
    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
      r.mtime_ns = sqlite3_column_int64(stmt, 4);
    }
    r.updated_at = sqlite3_column_int64(stmt, 5);
    out = std::move(r);
  }
  sqlite3_finalize(stmt);
  return out;
}

std::vector<Store::LocatorRow> Store::list_locators_for_blob(
    std::int64_t blob_id, int limit) const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "SELECT id, uri, blob_id, size, mtime_ns, updated_at "
                         "FROM locator WHERE blob_id = ?1 LIMIT ?2;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare list_locators_for_blob");
  }
  sqlite3_bind_int64(stmt, 1, blob_id);
  sqlite3_bind_int(stmt, 2, limit);
  std::vector<LocatorRow> out;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    LocatorRow r;
    r.id = sqlite3_column_int64(stmt, 0);
    r.uri = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
      r.blob_id = sqlite3_column_int64(stmt, 2);
    }
    if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
      r.size = sqlite3_column_int64(stmt, 3);
    }
    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
      r.mtime_ns = sqlite3_column_int64(stmt, 4);
    }
    r.updated_at = sqlite3_column_int64(stmt, 5);
    out.push_back(std::move(r));
  }
  sqlite3_finalize(stmt);
  return out;
}

std::int64_t Store::count_blobs() const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_, "SELECT COUNT(*) FROM blob;", -1, &stmt,
                         nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare count_blobs");
  }
  std::int64_t n = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return n;
}

std::int64_t Store::count_locators() const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_, "SELECT COUNT(*) FROM locator;", -1, &stmt,
                         nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare count_locators");
  }
  std::int64_t n = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return n;
}

std::int64_t Store::insert_media(std::int64_t blob_id, MediaKind kind,
                                 std::optional<int> width,
                                 std::optional<int> height,
                                 MediaStatus status) {
  const std::int64_t now = now_unix_s();
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "INSERT INTO media(blob_id, kind, width, height, "
                         "status, updated_at) VALUES(?1, ?2, ?3, ?4, ?5, ?6);",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare insert_media");
  }
  sqlite3_bind_int64(stmt, 1, blob_id);
  sqlite3_bind_int(stmt, 2, static_cast<int>(kind));
  if (width) {
    sqlite3_bind_int(stmt, 3, *width);
  } else {
    sqlite3_bind_null(stmt, 3);
  }
  if (height) {
    sqlite3_bind_int(stmt, 4, *height);
  } else {
    sqlite3_bind_null(stmt, 4);
  }
  sqlite3_bind_int(stmt, 5, static_cast<int>(status));
  sqlite3_bind_int64(stmt, 6, now);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw_sqlite(index_, "step insert_media");
  }
  sqlite3_finalize(stmt);
  return static_cast<std::int64_t>(sqlite3_last_insert_rowid(index_));
}

std::int64_t Store::ensure_image_media(std::int64_t blob_id,
                                       std::optional<int> width,
                                       std::optional<int> height) {
  if (auto existing = find_media_for_blob(blob_id, MediaKind::Image)) {
    if (width && height) {
      set_media_size(existing->id, *width, *height);
    }
    ensure_region(existing->id, RegionKind::Full, "", std::nullopt);
    return existing->id;
  }
  const std::int64_t media_id =
      insert_media(blob_id, MediaKind::Image, width, height, MediaStatus::Ready);
  ensure_region(media_id, RegionKind::Full, "", std::nullopt);
  return media_id;
}

std::optional<Store::MediaRow> Store::find_media(std::int64_t media_id) const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "SELECT id, blob_id, kind, width, height, duration_ms, "
                         "page_count, still_count, status, error_code, "
                         "updated_at FROM media WHERE id = ?1;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare find_media");
  }
  sqlite3_bind_int64(stmt, 1, media_id);
  std::optional<MediaRow> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    MediaRow r;
    r.id = sqlite3_column_int64(stmt, 0);
    r.blob_id = sqlite3_column_int64(stmt, 1);
    r.kind = static_cast<MediaKind>(sqlite3_column_int(stmt, 2));
    if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
      r.width = sqlite3_column_int(stmt, 3);
    }
    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
      r.height = sqlite3_column_int(stmt, 4);
    }
    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
      r.duration_ms = sqlite3_column_int64(stmt, 5);
    }
    if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) {
      r.page_count = sqlite3_column_int(stmt, 6);
    }
    if (sqlite3_column_type(stmt, 7) != SQLITE_NULL) {
      r.still_count = sqlite3_column_int(stmt, 7);
    }
    r.status = static_cast<MediaStatus>(sqlite3_column_int(stmt, 8));
    if (sqlite3_column_type(stmt, 9) != SQLITE_NULL) {
      r.error_code =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
    }
    r.updated_at = sqlite3_column_int64(stmt, 10);
    out = std::move(r);
  }
  sqlite3_finalize(stmt);
  return out;
}

std::optional<Store::MediaRow> Store::find_media_for_blob(
    std::int64_t blob_id, MediaKind kind) const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "SELECT id FROM media WHERE blob_id = ?1 AND kind = ?2;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare find_media_for_blob");
  }
  sqlite3_bind_int64(stmt, 1, blob_id);
  sqlite3_bind_int(stmt, 2, static_cast<int>(kind));
  std::optional<std::int64_t> id;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    id = sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  if (!id) return std::nullopt;
  return find_media(*id);
}

void Store::set_media_size(std::int64_t media_id, int width, int height) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "UPDATE media SET width = ?1, height = ?2, "
                         "updated_at = ?3 WHERE id = ?4;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare set_media_size");
  }
  sqlite3_bind_int(stmt, 1, width);
  sqlite3_bind_int(stmt, 2, height);
  sqlite3_bind_int64(stmt, 3, now_unix_s());
  sqlite3_bind_int64(stmt, 4, media_id);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw_sqlite(index_, "step set_media_size");
  }
  sqlite3_finalize(stmt);
}

void Store::set_media_page_count(std::int64_t media_id, int page_count) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "UPDATE media SET page_count = ?1, updated_at = ?2 "
                         "WHERE id = ?3;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare set_media_page_count");
  }
  sqlite3_bind_int(stmt, 1, page_count);
  sqlite3_bind_int64(stmt, 2, now_unix_s());
  sqlite3_bind_int64(stmt, 3, media_id);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw_sqlite(index_, "step set_media_page_count");
  }
  sqlite3_finalize(stmt);
}

void Store::set_media_status(std::int64_t media_id, MediaStatus status,
                             std::optional<std::string_view> error_code) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "UPDATE media SET status = ?1, error_code = ?2, "
                         "updated_at = ?3 WHERE id = ?4;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare set_media_status");
  }
  sqlite3_bind_int(stmt, 1, static_cast<int>(status));
  if (error_code) {
    sqlite3_bind_text(stmt, 2, error_code->data(),
                      static_cast<int>(error_code->size()), SQLITE_STATIC);
  } else {
    sqlite3_bind_null(stmt, 2);
  }
  sqlite3_bind_int64(stmt, 3, now_unix_s());
  sqlite3_bind_int64(stmt, 4, media_id);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw_sqlite(index_, "step set_media_status");
  }
  sqlite3_finalize(stmt);
}

std::int64_t Store::insert_region(std::int64_t media_id, RegionKind kind,
                                  std::string_view key,
                                  std::optional<int> ordinal) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "INSERT INTO region(media_id, kind, key, ordinal) "
                         "VALUES(?1, ?2, ?3, ?4);",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare insert_region");
  }
  sqlite3_bind_int64(stmt, 1, media_id);
  sqlite3_bind_int(stmt, 2, static_cast<int>(kind));
  sqlite3_bind_text(stmt, 3, key.data(), static_cast<int>(key.size()),
                    SQLITE_STATIC);
  if (ordinal) {
    sqlite3_bind_int(stmt, 4, *ordinal);
  } else {
    sqlite3_bind_null(stmt, 4);
  }
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw_sqlite(index_, "step insert_region");
  }
  sqlite3_finalize(stmt);
  return static_cast<std::int64_t>(sqlite3_last_insert_rowid(index_));
}

std::int64_t Store::ensure_region(std::int64_t media_id, RegionKind kind,
                                  std::string_view key,
                                  std::optional<int> ordinal) {
  if (auto existing = find_region_by_key(media_id, kind, key)) {
    return existing->id;
  }
  return insert_region(media_id, kind, key, ordinal);
}

std::optional<Store::RegionRow> Store::find_region(
    std::int64_t region_id) const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "SELECT id, media_id, kind, key, ordinal FROM region "
                         "WHERE id = ?1;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare find_region");
  }
  sqlite3_bind_int64(stmt, 1, region_id);
  std::optional<RegionRow> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    RegionRow r;
    r.id = sqlite3_column_int64(stmt, 0);
    r.media_id = sqlite3_column_int64(stmt, 1);
    r.kind = static_cast<RegionKind>(sqlite3_column_int(stmt, 2));
    r.key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
      r.ordinal = sqlite3_column_int(stmt, 4);
    }
    out = std::move(r);
  }
  sqlite3_finalize(stmt);
  return out;
}

std::optional<Store::RegionRow> Store::find_region_by_key(
    std::int64_t media_id, RegionKind kind, std::string_view key) const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "SELECT id FROM region WHERE media_id = ?1 AND "
                         "kind = ?2 AND key = ?3;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare find_region_by_key");
  }
  sqlite3_bind_int64(stmt, 1, media_id);
  sqlite3_bind_int(stmt, 2, static_cast<int>(kind));
  sqlite3_bind_text(stmt, 3, key.data(), static_cast<int>(key.size()),
                    SQLITE_STATIC);
  std::optional<std::int64_t> id;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    id = sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  if (!id) return std::nullopt;
  return find_region(*id);
}

std::optional<Store::RegionRow> Store::find_full_region(
    std::int64_t media_id) const {
  return find_region_by_key(media_id, RegionKind::Full, "");
}

void Store::put_tile(const TileRow& meta, std::span<const std::uint8_t> data) {
  if (data.empty()) {
    throw std::invalid_argument("put_tile: empty data");
  }
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "INSERT INTO tile(media_id, region_id, scale, x, y, "
                         "width, height, codec_id, quality) "
                         "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9) "
                         "ON CONFLICT(media_id, region_id, scale, x, y) DO UPDATE SET "
                         "width = excluded.width, height = excluded.height, "
                         "codec_id = excluded.codec_id, quality = excluded.quality;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare put_tile index");
  }
  sqlite3_bind_int64(stmt, 1, meta.media_id);
  sqlite3_bind_int64(stmt, 2, meta.region_id);
  sqlite3_bind_int(stmt, 3, meta.scale);
  sqlite3_bind_int(stmt, 4, meta.x);
  sqlite3_bind_int(stmt, 5, meta.y);
  sqlite3_bind_int(stmt, 6, meta.width);
  sqlite3_bind_int(stmt, 7, meta.height);
  sqlite3_bind_int(stmt, 8, static_cast<int>(meta.codec_id));
  if (meta.quality) {
    sqlite3_bind_int(stmt, 9, *meta.quality);
  } else {
    sqlite3_bind_null(stmt, 9);
  }
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw_sqlite(index_, "step put_tile index");
  }
  sqlite3_finalize(stmt);

  if (sqlite3_prepare_v2(bulk_,
                         "INSERT INTO tile_blob(media_id, region_id, scale, x, "
                         "y, codec_id, data) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7) "
                         "ON CONFLICT(media_id, region_id, scale, x, y) DO UPDATE SET "
                         "codec_id = excluded.codec_id, data = excluded.data;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(bulk_, "prepare put_tile bulk");
  }
  sqlite3_bind_int64(stmt, 1, meta.media_id);
  sqlite3_bind_int64(stmt, 2, meta.region_id);
  sqlite3_bind_int(stmt, 3, meta.scale);
  sqlite3_bind_int(stmt, 4, meta.x);
  sqlite3_bind_int(stmt, 5, meta.y);
  sqlite3_bind_int(stmt, 6, static_cast<int>(meta.codec_id));
  sqlite3_bind_blob(stmt, 7, data.data(), static_cast<int>(data.size()),
                    SQLITE_STATIC);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw_sqlite(bulk_, "step put_tile bulk");
  }
  sqlite3_finalize(stmt);
}

bool Store::has_tile(std::int64_t media_id, std::int64_t region_id, int scale,
                     int x, int y) const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "SELECT 1 FROM tile WHERE media_id = ?1 AND "
                         "region_id = ?2 AND scale = ?3 AND x = ?4 AND y = ?5;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare has_tile");
  }
  sqlite3_bind_int64(stmt, 1, media_id);
  sqlite3_bind_int64(stmt, 2, region_id);
  sqlite3_bind_int(stmt, 3, scale);
  sqlite3_bind_int(stmt, 4, x);
  sqlite3_bind_int(stmt, 5, y);
  const bool ok = sqlite3_step(stmt) == SQLITE_ROW;
  sqlite3_finalize(stmt);
  return ok;
}

std::optional<Store::TileRow> Store::find_tile_meta(std::int64_t media_id,
                                                    std::int64_t region_id,
                                                    int scale, int x,
                                                    int y) const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "SELECT media_id, region_id, scale, x, y, width, "
                         "height, codec_id, quality FROM tile WHERE "
                         "media_id = ?1 AND region_id = ?2 AND scale = ?3 AND "
                         "x = ?4 AND y = ?5;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare find_tile_meta");
  }
  sqlite3_bind_int64(stmt, 1, media_id);
  sqlite3_bind_int64(stmt, 2, region_id);
  sqlite3_bind_int(stmt, 3, scale);
  sqlite3_bind_int(stmt, 4, x);
  sqlite3_bind_int(stmt, 5, y);
  std::optional<TileRow> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    TileRow r;
    r.media_id = sqlite3_column_int64(stmt, 0);
    r.region_id = sqlite3_column_int64(stmt, 1);
    r.scale = sqlite3_column_int(stmt, 2);
    r.x = sqlite3_column_int(stmt, 3);
    r.y = sqlite3_column_int(stmt, 4);
    r.width = sqlite3_column_int(stmt, 5);
    r.height = sqlite3_column_int(stmt, 6);
    r.codec_id = static_cast<CodecId>(sqlite3_column_int(stmt, 7));
    if (sqlite3_column_type(stmt, 8) != SQLITE_NULL) {
      r.quality = sqlite3_column_int(stmt, 8);
    }
    out = r;
  }
  sqlite3_finalize(stmt);
  return out;
}

std::optional<std::vector<std::uint8_t>> Store::get_tile_data(
    std::int64_t media_id, std::int64_t region_id, int scale, int x,
    int y) const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(bulk_,
                         "SELECT data FROM tile_blob WHERE media_id = ?1 AND "
                         "region_id = ?2 AND scale = ?3 AND x = ?4 AND y = ?5;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(bulk_, "prepare get_tile_data");
  }
  sqlite3_bind_int64(stmt, 1, media_id);
  sqlite3_bind_int64(stmt, 2, region_id);
  sqlite3_bind_int(stmt, 3, scale);
  sqlite3_bind_int(stmt, 4, x);
  sqlite3_bind_int(stmt, 5, y);
  std::optional<std::vector<std::uint8_t>> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const void* p = sqlite3_column_blob(stmt, 0);
    const int n = sqlite3_column_bytes(stmt, 0);
    if (p && n > 0) {
      const auto* bytes = static_cast<const std::uint8_t*>(p);
      out = std::vector<std::uint8_t>(bytes, bytes + n);
    }
  }
  sqlite3_finalize(stmt);
  return out;
}

void Store::delete_tiles_for_region(std::int64_t media_id,
                                    std::int64_t region_id) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_,
                         "DELETE FROM tile WHERE media_id = ?1 AND "
                         "region_id = ?2;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare delete_tiles index");
  }
  sqlite3_bind_int64(stmt, 1, media_id);
  sqlite3_bind_int64(stmt, 2, region_id);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw_sqlite(index_, "step delete_tiles index");
  }
  sqlite3_finalize(stmt);

  if (sqlite3_prepare_v2(bulk_,
                         "DELETE FROM tile_blob WHERE media_id = ?1 AND "
                         "region_id = ?2;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    throw_sqlite(bulk_, "prepare delete_tiles bulk");
  }
  sqlite3_bind_int64(stmt, 1, media_id);
  sqlite3_bind_int64(stmt, 2, region_id);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    throw_sqlite(bulk_, "step delete_tiles bulk");
  }
  sqlite3_finalize(stmt);
}

std::int64_t Store::count_tiles() const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_, "SELECT COUNT(*) FROM tile;", -1, &stmt,
                         nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare count_tiles");
  }
  std::int64_t n = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return n;
}

std::int64_t Store::count_media() const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_, "SELECT COUNT(*) FROM media;", -1, &stmt,
                         nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare count_media");
  }
  std::int64_t n = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return n;
}

std::int64_t Store::count_regions() const {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(index_, "SELECT COUNT(*) FROM region;", -1, &stmt,
                         nullptr) != SQLITE_OK) {
    throw_sqlite(index_, "prepare count_regions");
  }
  std::int64_t n = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return n;
}

}  // namespace thumtoo
