-- SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
-- SPDX-License-Identifier: GPL-3.0-or-later
-- thumtoo schema_version 1

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
  updated_at INTEGER
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
  PRIMARY KEY (content_id, max_edge, frame_idx)
);

CREATE TABLE IF NOT EXISTS tags (
  content_id TEXT NOT NULL,
  tag TEXT NOT NULL,
  source TEXT,
  created_at INTEGER,
  PRIMARY KEY (content_id, tag)
);

CREATE INDEX IF NOT EXISTS idx_locators_content_id ON locators(content_id);
CREATE INDEX IF NOT EXISTS idx_levels_content_id ON levels(content_id);
