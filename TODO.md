## chore: drop unused encode_cell_from_level (2026-09-08) — tip **thumtoo-038**

Interactive path uses `extract_rgb_cell_from_level` only; JPEG durable encode is `encode_tile_cell_rgb` in Client.

---

## Interactive tiles: rgb888 delivery, no store→get round-trip (2026-09-08) — tip **thumtoo-037**

### Problem
Interactive `build_tile_cell*` JPEG-encoded every cell; Client then
`store_tiles` + `reply_one(get_tile(...))` re-read the blob. Galapix JPEG-decoded
again before GL upload — encode/decode on the hot path.

### Fix
* Ladder cut returns **rgb888** (crop + colourspace + write_to_memory)
* Client stores durable JPEG via `encode_tile_cell_rgb`, replies with the
  in-memory cell (no get_tile)
* Galapix already accepts `codec=rgb888` (PDF live path)

### Status
- [x] extract_rgb_cell_from_level
- [x] Client reply path
- [ ] Galapix pending-upload drain (separate)

---

## Interactive tile decode ladder cache (2026-09-08) — tip **thumtoo-036**

### Problem
Galapix requests one (scale,x,y) cell at a time. `build_tile_cell*` reloaded the
source and re-ran the shrink chain **per cell** → extreme CPU when many
high-res tiles are missing.

### Fix
In-process **shrink-ladder cache** (up to 4 sources):

* Key: file path+mtime, or caller-supplied key for buffers (`a:archive\nmember`, `h:uri`)
* Level 0 = full decode once; coarser levels via successive `vips_shrink` ×2
* Concurrent cells for the same source share the ladder (mutex per entry)
* Single-cell encode is crop + JPEG only after the level exists

`build_tile_cell_buffer(..., decode_cache_key)` optional key; Client passes
keys for archive members and HTTP bodies.

### Status
- [x] Ladder cache in image.cpp
- [x] Client cache keys for archive / HTTP
- [ ] Bundle / land for Galapix flake bump

---

## request_tile: no sync cache hit on caller thread (2026-09-07) — tip **thumtoo-035**

### Symptom (Galapix)
Frame drops when zooming: `Image::draw` → `request_tile` → Client did
`get_tile` (SQLite + blob read) on the GUI thread; default Executor then ran
the completion (JPEG/rgb888 decode) **inline** on that same thread.

### Fix
`Client::request_tile` always enqueues `EnsureTiles`. Cache hits are resolved
inside `handle_ensure_tiles` on a worker, then `executor_.post` delivers the
callback (inline on worker with default Executor; GUI-marshaled if the host
installs a queue).

### Note
`get_tile` remains available for tools/CLI that intentionally want a sync read.
`request_size` still fast-paths meta in memory on the caller (no blob I/O).

---

## Galapix ResourceDatabase → thumtoo gaps (2026-09-07) — tip **thumtoo-034**

Galapix removed its `ResourceDatabase` / `cache4.sqlite3` resource index
(galapix-068). View path already used thumtoo only. The following features
existed in Galapix schema or pipeline stubs but were **unfinished or unused
in the viewer**; implement in thumtoo when needed rather than resurrecting
cache4.

### Coverage already in thumtoo

| Need | thumtoo |
|------|---------|
| Path + mtime + size | `locators` |
| Content identity | `content_id` (sha256 preferred) |
| Image WxH | `content.width/height`, `get_size` |
| Archive member list | `archive_entries`, `//archive:` URIs |
| HTTP(S) sources | locators + optional curl fetch |
| Tags | `tags` table |
| Grid tiles + ladder | `tiles` / `levels` + blob store |
| PDF pages | `//page:N`, live rgb888 + durable JPEG |

### Still to implement in thumtoo (from Galapix ResourceDatabase)

1. **Archive passwords**  
   Galapix `archive.password` column — never wired through the view path, but
   needed for encrypted ZIP/RAR/7z. Design: store password material only in a
   host-controlled secret store or session; optional `locator` / archive meta
   flag that a password is required, not the password itself in the shared
   cache if multi-user.

2. **Video / still metadata beyond duration**  
   Galapix `video` table: width, height, duration, **aspect_ratio**. thumtoo
   has `duration_ms`, `still_count`; add aspect (or derive from WxH) and
   multi-frame still policy if video browsing becomes a product goal.

3. **Remote URL content-type / HTTP validators**  
   Galapix `url.content_type`, `url.mtime`. thumtoo HTTP path should persist
   Content-Type and validators (ETag / Last-Modified) on the locator or a
   small `http_meta` table for revalidation.

4. **Explicit resource “handler” / pipeline status**  
   Galapix `resource.type`, `handler`, `arguments`, `status` modeled a
   multi-stage job graph. Prefer thumtoo `content.status` + `error_code` +
   format; avoid a parallel handler table unless job orchestration returns.

5. **Directory / collection UX index** (optional product)  
   thumtoo already has `directory_snapshots` / `directory_entries`. Galapix
   never finished a full library UI on cache4; grow thumtoo listing APIs
   instead of a Galapix SQL index.

### Explicit non-goals

* Galapix tile SQLite (`cache4_tiles`) — already replaced by thumtoo tiles.
* Galapix SHA-1 blob ids as primary keys — keep sha256 content_id.
* Resurrecting `DatabaseThread` / `FileEntryGenerationJob` in Galapix.

### References

* Galapix `docs/CACHE4_VS_THUMTOO.md` (removal history + matrix)
* Removed Galapix tables: `file`, `blob`, `image`, `archive`, `archive_file`,
  `url`, `video`, `resource`

---

## Live PDF tiles + negative scale investigation (2026-09-07) — tip **thumtoo-031**

### Symptom
Cached PDF tiles (JPEG from durable store) render in Galapix. Live /
interactive `request_tile` (especially negative scale, or any PDF cell that
bypasses cache and returns **uncompressed** pixels) does not.

### thumtoo-side findings (verified with `thumtoo-test-pdf-tiles` + `thumtoo-tile`)

| Path | Behaviour |
|------|-----------|
| Interactive `request_tile` for `//page:N` | Always builds via `pdf_render_tile_cell` → replies **`codec=rgb888`** raw RGB888 in the callback `TileBlob`. |
| Durable store | Only if `scale >= kPdfMinDurableTileScale` (−2). Stored blob is **JPEG** (`kPdfTileQuality`). |
| `get_tile` after store | Returns the **JPEG** row (not rgb888). |
| Scale &lt; −2 | Live-only; never written to `tiles` / `tile_blobs`. |
| Negative scale geometry | `full = layout * 2^{-scale}`, `dpi = 144 * 2^{-scale}`, crop `(x*T, y*T, tw, th)` on that grid. Matches TILES.md; Poppler region + soft-crop fallback both produce correct 256² (or edge) cells. |
| `get_tile_coverage` | Reflects **stored** min/max only. Theoretical fallback starts at min_scale=0. Apps must request negative scales explicitly; coverage does not advertise them. |

Library path is consistent. Manual checks with Ghostscript test PDF:

* scale 0 / −1 / −2 / −3 → non-empty rgb888 256×256 (or edge size)
* scale −3 → `has_tile` stays false
* `pdftoppm` full-page vs region crop align on the same pixel grid

### Root cause (most likely Galapix)

`INTEGRATION_GALAPIX.md` documents the adapter as:

> misses use `request_tile` (JPEG → `surf::jpeg::load_from_mem`)

The callback payload for **every** interactive PDF tile is now **rgb888**, not
JPEG. JPEG-decoding raw RGB bytes fails (or yields garbage). Cache hits still
go through `get_tile` → JPEG → works. That matches “cached works, live does
not”.

Negative scale is a special case of the same path: those cells are often
live-only (or first miss), so they always hit the rgb888 reply.

### What Galapix must do

In `ThumtooTileProvider` (or equivalent):

1. Inspect `TileBlob::codec`.
2. If `codec == "rgb888"` (or `kTileCodecRgb888`): treat `bytes` as tightly
   packed RGB888, width×height from the blob meta; upload to GL / software
   surface **without** JPEG decode.
3. If `codec == "jpeg"` (or empty/default): keep existing `surf::jpeg::load_from_mem`.
4. To zoom past 1:1 on PDF pages, request `scale < 0` (layout is 144 dpi;
   scale −1 = 288 dpi, −2 = 576 dpi). Do not rely on `get_tile_coverage` for
   negative min_scale.

### Tests added this tip

* `tests/test_pdf_tiles.cpp` (`thumtoo-test-pdf-tiles`): end-to-end Client path
  for scales 0, −1, −2, −3; edge tiles; out-of-range; durable vs live-only;
  codec contract; geometry helpers. Embeds a minimal PDF-1.4 blob (no
  Ghostscript) so CI/nix only need Poppler.
* Existing `test_pdf_scale` remains the pure math check.

### Still open (not a thumtoo bug)

* Galapix adapter rgb888 branch (required for live PDF).
* Galapix requesting negative scales when zoomed past layout 1:1.
* Optional: expose theoretical min_scale for PDF in coverage (e.g. always allow
  down to `kPdfMinDurableTileScale` or a configurable live floor). Discuss
  before changing API semantics.

### Key files

* `src/client.cpp` — PDF branch of `handle_ensure_tiles` (live rgb888 reply)
* `src/pdf.cpp` — `pdf_render_tile_cell` / region raster
* `include/thumtoo/constants.hpp` — `kTileCodecRgb888`, `kPdfMinDurableTileScale`
* `tests/test_pdf_tiles.cpp`, `tools/thumtoo_tile.cpp` (`--raw-pdf`, codec-aware PNG out)

---

## PDF region tiles + negative scale (2026-09-07) — tip **thumtoo-030**

Interactive PDF tiles: `pdf_build_tile_cell` region-rasterizes a single cell at
`dpi = kPdfLayoutDpi * 2^{-scale}`. Negative scale is sharper than layout;
peak memory stays O(tile), not O(full page × dpi).

* `pdf_page_size_at_scale`, `pdf_dpi_for_scale`, `pdf_rasterize_page_region`
* `encode_tile_cell_rgb` for any scale
* `Client` single-cell PDF path uses region builder (not full-page + crop)

Pyramid prewarm still full-page at layout for scales ≥ 0 (unchanged).

Galapix follow-up: request tilescale &lt; 0 when zoomed past 1:1 on PDF pages;
**and** handle `codec=rgb888` on the live callback (see tip 031).

## Session handoff (2026-09-07) — tip **thumtoo-027**

Apply tip **`thumtoo-029.bundle`** (or stack 016…023). Author: Ingo Ruhnke
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
2. ~~Higher-DPI / per-tile PDF crop render (true “mandelbrot-style” region)~~ (030)
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
Apply in order or take tip **`thumtoo-029.bundle`**:
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
