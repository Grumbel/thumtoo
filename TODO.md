## Session handoff (2026-09-07) — tip **thumtoo-027**

Apply tip **`thumtoo-028.bundle`** (or stack 016…023). Author: Ingo Ruhnke
`<grumbel@gmail.com>` + `Co-authored-by: Grok <grok@x.ai>`.

### What landed this session (API / retrieval)

| Bundle | Change |
|--------|--------|
| thumtoo-016 | **Location URI API**: `parse_location` / `format_location`, nested `//archive` + `//page`, http/content-id helpers, `with_archive_member` / `with_pdf_page`; `Client::list_locators` / `find_locator`; `tests/test_uri` |
| thumtoo-017 | **Content-id resolve**: `meta_for_content_id`, `list_locators_for_content_id`, `meta_for_uri` accepts `sha256:`/`sha1:`; `Client::resolve_content_id`, `list_uris_for_content_id`, `get_meta_for_content_id` |
| thumtoo-018 | **`read_source_bytes`**: file + archive member (+ via content-id); `read_file_bytes`; size-capped |
| thumtoo-019 | **PDF live tiles**: `kPdfLayoutDpi = 144`, `pdf_page_layout_size`; probe + `request_tile`/pyramid rasterize at layout edge |
| thumtoo-020 | Fix `test_client` `read_source_bytes` scope |
| thumtoo-021 | **HTTP(S) fetch**: optional libcurl (`THUMTOO_HAVE_CURL`), `network.hpp` `http_get_bytes`; probe / pixels / tiles / `read_source_bytes`; flake + CMake |
| thumtoo-022 | **Session HTTP body cache** (`fetch_http_cached`, 512 MiB in-process) |
| thumtoo-023 | **Fix**: declare `Client::fetch_http_cached` in `client.hpp` (022 build break) |

Earlier tips (001–015) remain in the history; tip is **024** (handoff docs; code tip still 023 features).

### Product direction (do not grow Galapix SQL for this)

* thumtoo = shared **media index + display pixels + growing data retrieval**
* Nested location URIs, content-id identity, network + archive + PDF
* Query/library listing for apps (Galapix `-p` transitional)
* Galapix/biltoo/dirtoo consume; durable HTTP disk cache / TTL still future

### Build notes

* `pkg-config libcurl` → `THUMTOO_HAVE_CURL=1`; without curl, http(s) parses but fetch fails
* Poppler optional → `THUMTOO_HAVE_POPPLER` for PDF
* Tests: `thumtoo-test-uri`, database, client

### Next (priority)

1. ~~Durable HTTP/download cache + TTL~~ (025: blobs.sqlite `http_bodies`, 7d TTL)
2. Higher-DPI / per-tile PDF crop render (true “mandelbrot-style” region)
3. ~~Query prefix/LIKE on locators~~ (026); tags/collections still open
4. Galapix flake: pin/update input to a tip that includes 021+ curl

### Key files

* `include/thumtoo/uri.hpp`, `network.hpp`, `client.hpp`, `pdf.hpp`, `constants.hpp`
* `src/uri.cpp`, `network.cpp`, `client.cpp`, `pdf.cpp`
* `DESIGN.md`, `TILES.md`, `AGENTS.md`, this TODO


<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO — thumtoo


## Session handoff (2026-09-07)

### Bundles
Apply in order or take tip **`thumtoo-028.bundle`**:
| Bundle | Change |
|--------|--------|
| thumtoo-001 | `request_tile` single-scale only |
| thumtoo-002 | Parallel JPEG encode per scale; prepare timings |
| thumtoo-003 | Clean `--help` |
| thumtoo-004 | **Multi-worker Client pool** (default HW concurrency); `--jobs N` |
| thumtoo-005 | **Extract cache** (512 MiB) + coalesce EnsureTiles/Pixels same archive |
| thumtoo-006 | Stats: **wall=** vs **cpu:** summed scopes + parallel~= |
| thumtoo-007 | **Single-edge preview** (not full ladder); thumb/jxl stats; Galapix-shaped cache |
| thumtoo-008 | DESIGN/INTEGRATION policy; downscale smaller preview from larger cache |
| thumtoo-009 | **Single-cell** interactive `request_tile` (not full scale grid) |
| thumtoo-010 | EXIF embedded thumb for preview; JPEG shrink-on-load for tile cells |
| thumtoo-011 | Pretty stats + `thumtoo-bench` phase benchmark tool |
| thumtoo-012 | flake apps + `nix develop` helpers (configure/build/test/run) |
| thumtoo-013 | `--min-scale` / `--max-scale` for tile prepare + bench |
| thumtoo-014 | `format.hpp`: central ext/MIME/classify for apps |
| thumtoo-015 | pdf.cpp: remove unused to_lower_ext warning |
| thumtoo-016 | **Location URI API** (parse/format nested archive+page, http/content-id); Client list_locators |

### Pixel cache policy (Galapix-first, biltoo API stable)
* **Durable:** size index + **one** JXL preview per `request_pixels(max_edge)` (largest
  `kLadderEdges` entry ≤ max_edge) + optional **tile** pyramid.
* **Not durable by default:** multi-edge ladder (128+256+512+…). Apps can
  downscale the stored preview or call `request_pixels` with another edge.
* **Original full-res:** not cached; viewers decode past max tile scale.
* Public API unchanged: `get_pixels` / `request_pixels(uri, max_edge, …)`.

### Measured (one ~26 MiB archive, 68 members, 4324 tiles)
* After cache/coalesce: **real ~3.6 s** with 12 workers; jpeg dominates **cpu-share**
* extract cpu-share dropped sharply vs double full-zip read
* Stats **cpu-sum ≫ wall** when parallel — expected
* Pre-007: `--ladder 256` still encoded full ladder (invisible to stats) → slow

### Done (parallelism / prepare)
* [x] `vips_concurrency_set(hardware_concurrency)`
* [x] Parallel JPEG encode of independent 256² cells per scale
* [x] Client worker pool (`open(..., worker_threads)`; 0 = auto, max 32)
* [x] Archive member extract cache across probe → tiles/ladder
* [x] Coalesce ProbeSize **and** EnsureTiles / EnsurePixels per archive
* [x] `thumtoo-prepare --stats --jobs --tiles` with clear wall vs cpu labels
* [x] Single-scale interactive `request_tile`
* [x] Single-edge `request_pixels` / `--ladder EDGE` (one `vips_thumbnail` + JXL)
* [x] Stats: `thumb=` / `jxl=` / `levels=`
* [x] DESIGN §5 + INTEGRATION_GALAPIX: preview + tiles, no double ladder
* [x] Downscale-from-larger-cached-preview when asking a smaller max_edge
* [x] Single-cell interactive `request_tile` (`build_tile_cell`)

### Next session — priority
1. [x] EXIF embedded thumb for even faster first preview
2. Optional: tune extract cache size / eviction (clear-all is crude)
3. [x] **Single-cell** tile cut (encode only requested (scale,x,y), not full scale grid)
4. Not worth yet: GPU JPEG (nvJPEG) — CPU jpeg still parallelizable; extract fixed
5. Not realistic: “cut tiles from JPEG without decode” (see below)

### JPEG region decode (design note)
Baseline JPEG is not randomly tiled. libjpeg can **DCT-scale** and limited
**skip/crop scanlines**; true per-tile extract without stream decode needs
RST markers or a different format. Prefer one decode → shrink → encode for
pyramids; use DCT-scale for overviews only.


## Done

- [x] Bootstrap documentation repo (README, DESIGN, ARCHITECTURE, AGENTS)
- [x] Link biltoo, dirtoo, galapix, dirtoo-py
- [x] Normative rules: cache-first browse, XDG-only, no source pollution, hash identity
- [x] Location URI form; video stills (`still_count` + `frame_idx`); review response
- [x] README scope row: directory snapshots vs dirtoo live listing
- [x] Phase 0 constants written down (ladder edges, JXL q=80, archive caps, WAL)

## Phase 0 remaining (thin)

- [x] Encode constants as implementable C++/header names (`constants.hpp`)
- [x] biltoo integration note (`INTEGRATION.md`)

## Phase 1

- [x] Fix prepare/drain race leaving locators pending
- [x] Ladder payloads in blobs.sqlite (not per-level files)

- [x] `include/thumtoo/` public headers + SQLite content/locator spike
- [x] CMake + flake.nix + `nix develop` shell
- [x] `thumtoo-status` CLI (inspect cache summary/locators/content)
- [x] Database open/migrate tests
- [x] `thumtoo-prepare` CLI skeleton (register paths + schedule probe)
- [x] Client API: get_size / get_meta / request_size + single worker queue
- [x] URI helpers (file:/// , //archive detection)
- [x] Image pipeline via libvips + JPEG-XL ladder (required; flake.nix)
- [x] SHA-256 content id promotion
- [x] get_pixels / request_pixels reading levels
- [x] `thumtoo-prepare` progress reporting (per-job lines + --quiet)
- [x] flake: add util-linux for gio `mount.pc` (silence pkg-config noise)
- [x] flake: add libselinux for gio `libselinux.pc` (silence pkg-config noise)
- [x] flake: add libsepol for libselinux `libsepol.pc` (silence pkg-config noise)
- [x] flake: add libthai, libdatrie, libxdmcp, libxml2 (silence pkg-config noise)
- [x] Tag API sketch reconciled with dirtoo checksum tags (`TAGS.md` + list/add/remove)

## Design open / discuss

- [x] Archive on-demand vs batch coalescing details (worker peeks same-archive jobs)
- [ ] Optional convenience Location `//frame:N` (view only)
- [ ] Whether archive caps need per-format overrides

## Phase 2 (started)

- [x] Archive TOC read (libarchive) + `archive_entries` cache
- [x] Extract member bytes + probe/ladder for `//archive:member` URIs
- [x] Size / ratio caps on extract (`kArchiveMaxMemberUncompressedBytes`)
- [x] Batch vs on-demand coalesce for archives (open once, many members)
- [x] Client shutdown clears job queue; skip re-probe when Ready
- [x] `thumtoo-prepare` archive expand (register all image members)

## Phase 4 — Grid tiles (galapix-style) — DONE (library)

Goal: provide optional **256×256 power-of-two tile pyramids** so Galapix
(develop) can consume thumtoo instead of its own SQLite `tiles` table.

Reference: galapix master `TileGenerator` + `tiles` schema
`(fileid, scale, x, y) → JPEG/PNG blob`. Scale 0 = full resolution;
each +1 halves linear size. Tile size fixed at 256.

### Design decisions (proposed)

1. **Tile size** `kTileSize = 256` (Galapix-compatible; not configurable for now).
2. **Scale convention** matches Galapix: `scale=0` full-res tiles, `scale=1`
   half linear, … up to the scale where the whole image fits in one tile.
3. **Codec** default **JPEG** (quality 80) for Galapix decode path simplicity;
   ladder stays JXL. (Revisit JXL tiles once Galapix has a JXL decoder path.)
4. **Storage**
   - Metadata rows in `index.sqlite` table `tiles`
     `(content_id, scale, x, y, width, height, codec, quality)` PRIMARY KEY
     `(content_id, scale, x, y)`.
   - Payload BLOBs in `blobs.sqlite` table `tile_blobs` with the same key.
   - Same pattern as ladder `levels` / `level_blobs` (no loose files).
5. **Schema** keep `kSchemaVersion = 1`; additive tables only (no bump) until
   a breaking change is required.
6. **API surface** (Client)
   - `get_tile(uri, scale, x, y) → optional<TileBlob>`
   - `request_tile(uri, scale, x, y, cb)` async; generates missing tiles for
     that scale (or full pyramid on prepare).
   - `get_tile_coverage(uri) → optional{min_scale, max_scale, image_size}`
   - `request_tiles(uri, min_scale, max_scale, cb)` / prepare flag.
7. **Generation** libvips: load once, successive `resize(0.5)` + crop 256²;
   partial edge tiles allowed (width/height < 256 stored).
8. **Prepare CLI** optional `--tiles` / `--tile-max-edge N` to prewarm pyramids
   (default off so biltoo path stays light).
9. **Non-goals this phase**
   - Replacing Galapix UI or OpenGL tile cache.
   - Video still tiles (images + archive + PDF page tiles done).
   - Eviction of tiles (shares future `thumtoo-gc`).

### Implementation order

- [x] `TILES.md` normative note + constants in `constants.hpp`
- [x] Schema: `tiles` in index + `tile_blobs` in BlobStore; status counters
- [x] `image.cpp`: `build_tile_pyramid(...)` → vector of tile blobs
- [x] Database / BlobStore put/get/list/min_max for tiles
- [x] Client: get/request tile + worker job type
- [x] `thumtoo-status` tile summary; tests with small fixture image
- [x] `thumtoo-prepare --tiles`
- [x] Galapix develop integration sketch (`INTEGRATION_GALAPIX.md`)

### Notes / resolved

- On-demand generates **requested scale + all coarser** in one source load.
- `kTileMaxSourcePixels` (100 MP): refuse encode above that; size probe still works.
- Galapix develop adapter remains outside this repo (see INTEGRATION_GALAPIX.md).

## Later

- [ ] Cache eviction / LRU / orphan sweep + `thumtoo-gc` / `thumtoo-status`
- [ ] Optional D-Bus daemon
- [ ] Adaptive video frame count (8–64 → still_count)
- [ ] Animated video preview level (must-have; deferred until consumers exist)


## PDF page tiles ([x] 2026-09-07)

Interactive `request_tile` and pyramid prewarm no longer stub PDF URIs.
Flow: `pdf_page_size_72dpi` → `pdf_rasterize_page` at that long edge →
`build_tile_cell_rgb` / `build_tile_pyramid_rgb` (new in image.hpp).

Known limit: layout size remains 72 dpi media box; higher-dpi native size
for readable scale-0 is a follow-up.
