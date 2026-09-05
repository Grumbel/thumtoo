<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# thumtoo design

## 1. Problem

Apps such as [biltoo](https://github.com/Grumbel/biltoo) currently keep image
sizes and soft previews in **process memory**. Opening a large session or an
archive repeatedly:

- re-probes dimensions (or falls back to neutral placeholders and wrong layout);
- re-decodes the same preview edges;
- re-lists archive members.

[dirtoo](https://github.com/Grumbel/dirtoo) already has pieces of the puzzle
(checksum SQLite, media meta cache, archive index, desktop thumbnailer client)
but no owned multi-edge **pixel ladder**. [galapix](https://github.com/Galapix/galapix)
implemented a full **tile pyramid** in SQLite, tightly coupled to that viewer.

**thumtoo** extracts a shared, library-shaped answer: durable index + display
pixels, consumable by biltoo first and optionally dirtoo later.

## 2. Lessons from sister projects

### galapix

- SQLite `files`: url, size, mtime, width, height.
- SQLite `tiles`: `(fileid, scale, x, y)` → JPEG/PNG blob.
- Archive members as URLs (`file://….rar//rar:inner.jpg`).
- Background DB thread; CLI `thumbgen` / `prepare` to prewarm.
- Strength: multi-resolution zoom. Weakness: app-coupled, many tile rows, blobs
  in DB.

### dirtoo

- **ChecksumStore**: path → digests; valid if size+mtime match.
- **MediaMetaCache**: async width/height (and media fields); GUI never probes on
  the UI thread.
- **dirtoo-archive**: read-only TOC + extract-on-demand (libarchive).
- **dirtoo-thumbnail**: D-Bus **Thumbnailer1 client**, not a pyramid owner.
- Strength: modular libraries, XDG paths, fingerprint invalidation.

### biltoo (consumer requirements)

- Replace provisional layout sizes with **known native size** as soon as probed
  once.
- Filmstrip / Gallery / Image soft preview from a **shared ladder**.
- Archive open without re-walking TOC every time.
- Keep **SessionImageId** for edits; thumtoo keys are **source URIs** only.

## 3. Goals

1. Permanent **native size** (and cheap format metadata).
2. **Archive TOC** cache + member identity.
3. **Display pixel ladder** at fixed long edges (e.g. 128, 256, 512, 1024, 2048).
4. **In-process C++ API** first; optional **D-Bus** service later for sharing and
   crash isolation.
5. GPL-3.0-or-later, REUSE headers, testable without a GUI.

## 4. Non-goals (initial)

- Full image editor or session/project file format.
- Replacing Freedesktop thumbnailers for every desktop icon.
- Mandatory JPEG-XL.
- Gigapixel region tiles (galapix-style) in phase 1.

## 5. JPEG-XL vs ladder vs tiles

| Approach | Role in thumtoo |
|----------|-----------------|
| **Fixed long-edge ladder** (WebP default; JXL optional) | **Phase 1** product cache |
| **Progressive JPEG-XL** single blob | Optional **codec** for a level, not a full index replacement |
| **Grid tiles** `(level, x, y)` | **Phase 4** if deep zoom is required |

## 6. Architecture

```
Apps: biltoo · dirtoo · thumtoo-prepare
                    │
                    ▼
         thumtoo::Client  (get / request + callback)
     ┌──────────┬──────────┬──────────┬──────────┐
     │ Identity │  Meta    │  Pixels  │ Archive  │
     │ SourceId │  SQLite  │  ladder  │ TOC+read │
     └──────────┴──────────┴──────────┴──────────┘
                    │
              worker pool + single SQLite writer
```

### Identity

Canonical **source URI** string, for example:

- File: `file:/absolute/path/to/image.jpg`
- Archive member: `archive:/absolute/path/to/book.zip!member/path.jpg`

Fingerprint of the **outer** file: `(size, mtime_ns)`. Mismatch → stale meta and
levels. Optional later: content hash (dirtoo-style) for rename-stable keys.

Biltoo **SessionImageId** remains session/edit identity and must **not** key
durable pixels.

### Schema (sketch)

```text
sources (
  id INTEGER PRIMARY KEY,
  uri TEXT UNIQUE NOT NULL,
  outer_path TEXT,
  member_path TEXT,
  size INTEGER,
  mtime_ns INTEGER,
  width INTEGER,
  height INTEGER,
  format TEXT,
  status INTEGER,
  updated_at INTEGER
)

archive_entries (
  archive_uri TEXT,
  member_path TEXT,
  uncompressed_size INTEGER,
  PRIMARY KEY (archive_uri, member_path)
)

levels (
  source_id INTEGER,
  max_edge INTEGER,
  width INTEGER,
  height INTEGER,
  codec TEXT,
  path TEXT,              -- relative under blob root (preferred over BLOB)
  PRIMARY KEY (source_id, max_edge)
)
```

Default locations (XDG):

- Index: `$XDG_CACHE_HOME/thumtoo/index.sqlite`
- Blobs: `$XDG_CACHE_HOME/thumtoo/blobs/…`

### API sketch

```text
open(cache_root)
get_size(uri) -> optional<Size>       // SQLite only
request_size(uri, callback)           // probe if missing
get_pixels(uri, max_edge) -> Image    // hit store or empty
request_pixels(uri, max_edge, cb)     // generate level if needed
list_archive(archive_path) -> TOC
read_member(archive, member) -> bytes
invalidate(uri | outer_path)
prepare(paths, edges[])               // CLI / idle prewarm
```

**GUI thread** may only call non-blocking get/try and schedule request_*.  
Workers decode/encode; one writer queue for SQLite.

### D-Bus (phase 3)

Same operations on a session service (e.g. `local.Thumtoo1`). Client library
selects backend via config/env (`THUMTOO_MODE=local|dbus`). Not required for
biltoo MVP.

## 7. Phases

| Phase | Deliverable |
|-------|-------------|
| **0** | This design; URI + fingerprint rules; spike size-only SQLite wired conceptually to biltoo |
| **1** | `libthumtoo`: sources + levels, async probe/decode, `thumtoo-prepare` CLI |
| **2** | Archive TOC + member pipeline; biltoo archive open uses cache |
| **3** | Optional `thumtood` + D-Bus |
| **4** | Optional grid tiles / deep zoom |

## 8. biltoo integration (target)

| Pain | thumtoo |
|------|---------|
| Provisional 1000×1000 layout | Durable `get_size` after first probe |
| Gallery / filmstrip soft tiles | `get_pixels(uri, 256\|512)` |
| Image-mode soft preview | `get_pixels(uri, 512\|1024)` |
| Archive re-list | Cached TOC |
| Slideshow warm | `prepare(session_paths, {512,1024})` |

## 9. What not to do

- Store multi-megapixel full frames in SQLite BLOBs by default.
- Key durable pixels by session edit id.
- Block the GUI on archive listing or encode.
- Require JPEG-XL for MVP.
- Vendor galapix/dirtoo sources into biltoo; keep thumtoo as its own repo.

## 10. License

GPL-3.0-or-later, REUSE.
