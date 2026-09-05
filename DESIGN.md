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

## 3a. Hard product rules (normative)

### Cache-first browse

The UI may browse **entirely from local cache** with **no I/O to source
volumes** until detail level or an explicit/idle refresh requires it. Waiting
for a USB HDD to spin up or a NAS to wake must not gate scrolling, folder
open (when a snapshot exists), or painting known previews.

- `get_*` / `list_cached_*` — local SQLite + blob files only.
- `request_*` / `prepare` / `refresh` — only place that touches sources.

Stale cache is acceptable; mark incomplete/unverified rather than block.

### Storage location

All durable state lives under **`$XDG_CACHE_HOME/thumtoo/`** (or an explicit
cache root for tests). **Never** write into the user’s directory trees:

- no sidecar files next to images
- no extended attributes on source files
- no `.DS_Store`, `Thumbs.db`, or other pollutants in source trees

The cache is **strictly read-only with respect to source data**.

### Content identity (hashes)

Couple durable media rows to **content** when possible, not only to path:

- Path + `(size, mtime)` remains a fast invalidation fingerprint.
- **Checksum (e.g. SHA-256)** is the stable id when directory layout changes
  (rename, reorganize, copy). Same bytes → same size/ladder/tags.
- Path indexes are convenience aliases that may point at a content id.

dirtoo’s checksum store and **tags-on-checksum** model are the reference;
thumtoo should align so apps can share identity.

### Tags (planned)

Optional **tags attached to content hash** (not path), same idea as dirtoo:

- Survive renames and moves.
- Library API for list/add/remove; UI stays in apps (dirtoo Tag Manager, etc.).
- Phase may trail the pixel ladder; schema should not paint us into a corner.

### Network URLs (later)

**http(s)** source URIs are a future extension (download/cache policy, TTL).
**Plain local files first**, then archives, then remote URLs.

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

Two layers:

1. **Locator URI** (how to find bytes today) — follow dirtoo / Galapix Location form:

   - File: `file:///absolute/path/to/image.jpg`
   - Archive member: `file:///absolute/path/to/book.zip//archive:member/path.jpg`
   - Nested archive: `file:///outer.zip//archive:inner.rar//archive:path/to/image.jpg`
   - Archive root (TOC only): `file:///absolute/path/to/book.zip//archive`
   - Later: `https://example.com/…` (not phase 1)

   The `//` acts as a pipe into the blob; `//archive:` (or historically `//rar:`, `//zip:`) selects the archive handler and optional member path. Prefer this over JAR-style `archive:…!…` (still accepted only for import compatibility).

2. **Content id** (what the bytes are): `sha256:<hex>` when known.

Path/mtime fingerprint of the **outer** file is a **fast** staleness check.
When the checksum is known, **levels, size, and tags** key primarily by content
id so renames do not orphan the ladder. Locators are many-to-one aliases onto
content rows.

Biltoo **SessionImageId** remains session/edit identity and must **not** key
durable pixels or tags.

### Schema (sketch)

```text
content (
  content_id TEXT PRIMARY KEY,   -- sha256:hex when known; provisional ids allowed
  width INTEGER,
  height INTEGER,
  format TEXT,
  status INTEGER,
  updated_at INTEGER
)

locators (
  uri TEXT PRIMARY KEY,          -- file:///… or file:///…//archive:member (later https:)
  content_id TEXT,               -- nullable until hashed
  outer_path TEXT,
  member_path TEXT,
  size INTEGER,
  mtime_ns INTEGER,              -- fingerprint of outer file
  updated_at INTEGER
)

archive_entries (
  archive_uri TEXT,
  member_path TEXT,
  uncompressed_size INTEGER,
  PRIMARY KEY (archive_uri, member_path)
)

-- Optional: directory listing snapshots (cache-first folder browse)
directory_snapshots (
  dir_uri TEXT PRIMARY KEY,
  size INTEGER,
  mtime_ns INTEGER,
  listed_at INTEGER,
  incomplete INTEGER
)

directory_entries (
  dir_uri TEXT,
  name TEXT,
  child_uri TEXT,
  is_dir INTEGER,
  size INTEGER,
  mtime_ns INTEGER,
  PRIMARY KEY (dir_uri, name)
)

levels (
  content_id TEXT,
  max_edge INTEGER,
  width INTEGER,
  height INTEGER,
  codec TEXT,
  path TEXT,              -- relative under blob root (never next to sources)
  PRIMARY KEY (content_id, max_edge)
)

tags (
  content_id TEXT,
  tag TEXT,
  PRIMARY KEY (content_id, tag)
)
```

Default locations (**only** under XDG cache, never in source trees):

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


### Video (still frames + optional animated)

Videos share the same content-id + locator model. The `content` row gains
`duration_ms` (and optionally codec / bitrate). Display proxies:

1. **Poster / representative still** — normal ladder entry (one frame, usually
   after a short offset or a middle keyframe).
2. **Temporal still set** — N frames sampled evenly across the timeline,
   stored as individual levels or (preferred for gallery) one **storyboard /
   contact-sheet** image that tiles them.
3. **Animated preview** (must-have, deferred) — short muted low-res WebM/MP4
   of a few snippets; neither biltoo nor dirtoo consume it yet, so keep at
   the bottom of the TODO.

#### Adaptive temporal resolution

Fixed counts waste resolution on long videos and over-sample short ones.
Default policy (tunable):

```text
min_count = 8
max_count = 64
target_interval_s = 8.0   # aim for roughly one frame every 8 s

count = clamp(round(duration_s / target_interval_s), min_count, max_count)
```

Examples:
- 30 s clip  → 8 frames (floor)
- 2 min     → 15 frames
- 10 min    → 64 frames (ceiling)
- 2 h movie → 64 frames (still useful overview; denser sampling is a
  future “high-res temporal” request, not the default ladder)

Storyboard layout can be derived from `count` (e.g. nearest rectangular grid).
Prefer keyframes when the container exposes them (faster, more stable frames).

Generation still follows the archive rule: on-demand extracts only the needed
timestamps; `prepare` / batch walks the file once and emits the whole set.

#### Worker isolation

libav/ffmpeg in-process is attractive (no process overhead, shared decoder
state). Broken or adversarial video files, however, frequently hang or crash
the decoder. For robustness the default worker path should be **subprocess**
(ffmpeg CLI or a tiny helper) so a bad file can be killed without taking down
the library or the host app. In-process libav remains an optional fast path
once we have a proven sandbox / timeout story.

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
- Key durable pixels or tags by path alone when a content hash is known.
- Key durable pixels by biltoo session edit id.
- Block the GUI on archive listing, network readdir, or encode.
- Touch source trees (xattrs, sidecars, AppleDouble, …).
- Require JPEG-XL or http(s) for MVP.
- Vendor galapix/dirtoo sources into biltoo; keep thumtoo as its own repo.

## 10. License

GPL-3.0-or-later, REUSE.
