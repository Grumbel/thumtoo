<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Thumbnail / ladder / tile generation audit

Status: **in progress** (2026-09-09). Companion to TODO.md section
“Thumbnail generation audit + microbenchmarks”.

Audience: agents continuing the work; goal is zero open questions about
what is slow, what is fast, what is measured, and what is still missing.

## 1. Scope

| Repo | Role in this audit |
|------|--------------------|
| **thumtoo** | All durable generation: size probe, LQIP, JXL ladder, 256² JPEG tiles, archive extract, PDF/DjVu raster |
| **galapix** | Consumer: time-to-first-pixel (overview LQIP + coarse tiles), size probe session, TileProvider |

Not in scope for generation code: OpenGL upload, layout algorithms (except
where they force generation order).

## 2. Pipeline overview

```
Source URI
  → parse_location (file / archive member / page)
  → content identity (sha256 + path fingerprint)
  → size (width×height)          [fast path desired]
  → LQIP / Handsum / ThumbHash   [tiny raster]
  → ladder level (JXL, long-edge) [preview]
  → grid tiles (JPEG 256²)       [zoom]
```

Cache layout: `$XDG_CACHE_HOME/thumtoo/{index,blobs}.sqlite`.

## 3. Fast vs slow operations (taxonomy)

### 3.1 Fast (should not require full-resolution decode)

| Op | Mechanism | Code | Notes |
|----|-----------|------|-------|
| Dimensions | Header / sequential open | `probe_image_file`, `probe_image_buffer` | VIPS_ACCESS_SEQUENTIAL; for JPEG libvips reads SOF only when no pixel access |
| EXIF IFD1 JPEG thumb | Parse APP1, load small JPEG | `extract_exif_jpeg_thumbnail*` in `image.cpp` | Used by `build_ladder*` when emb_edge ≥ target |
| JPEG shrink load | libjpeg IDCT at 1/2, 1/4, 1/8 | `vips_jpegload(..., "shrink", js)` | **Only** in `build_tile_cell_buffer` when `scale > 0` and magic looks JPEG and (scale < 0 \|\| decode_cache_key empty) |
| Cached size / tile / level | SQLite + blob get | `Client::get_size`, `get_tile`, `get_pixels` | GUI-safe; no source I/O |
| Archive TOC (cached) | index rows | archive listing APIs | |

### 3.2 Medium

| Op | Mechanism | Code |
|----|-----------|------|
| `vips_thumbnail` | Shrink-on-load where loader supports it | ladder fallback after EXIF miss |
| Single-cell tile encode | Crop + JPEG q=80 | `cut_cell_from_vips` |
| LQIP encode | ThumbHash on ≤100² RGBA | `lqip.cpp` |
| Archive member extract (warm extract_cache) | RAM hit | `archive.cpp` + Client extract cache 512 MiB |

### 3.3 Slow (full resolution or equivalent)

| Op | Mechanism | Code |
|----|-----------|------|
| Full `vips_image_new_from_file` without shrink | Decode all samples | `build_tile_cell` **file path**; pyramid; ladder when no EXIF and format lacks shrink-on-load |
| `build_tile_pyramid*` | Load full → cut all scales | Always full load today |
| PDF page raster | Poppler at chosen DPI | `pdf.cpp` |
| DjVu page raster | ddjvu_page_render | `djvu.cpp` |
| Cold archive member extract | libarchive stream to member | Sequential walk cost for RAR especially |
| SHA-256 of whole file | CPU + I/O | size probe path (now path+mtime cached, thumtoo-070) |

## 4. File-by-file (thumtoo)

### 4.1 `src/image.cpp` — core pixel generation

**Probe (`probe_image_file` / `probe_image_buffer`)**
- Refuses PDF/DjVu (thumtoo-073) — correct.
- Uses `VIPS_ACCESS_SEQUENTIAL`. For JPEG this is **header-class** if no
  pixel operation follows; width/height come from SOF.
- Does **not** use pure libjpeg `jpeg_read_header` path; relies on Vips.
- Format string from extension / hint only.

**Ladder (`build_ladder` / `_buffer` / `_rgb`)**
- Header open for long_edge → `pick_preview_edge`.
- JPEG: try EXIF embedded thumb first; if large enough, JXL-encode that
  (or thumbnail_image down). Stats: `exif_thumb_hits`.
- Else `vips_thumbnail` (shrink-on-load for JPEG/PNG where supported).
- **Does not** emit multi-edge ladder in one call when max_edge_limit is
  set for prepare; prepare policy is one edge (see prepare CLI).
- PDF/DjVu refused.

**Tiles**
- `jpeg_shrink_factor_for_scale`: scale≥3 → 8, ≥2 → 4, ≥1 → 2.
- `build_tile_cell_buffer`: JPEG shrink **only** when
  `scale < 0 || decode_cache_key.empty()`. If a decode cache key is set
  (ladder_acquire_level path), loader is full `vips_image_new_from_buffer`
  with **no shrink**.
- `build_tile_cell` (filesystem path): **never** calls `vips_jpegload` with
  shrink. Always full file open via ladder_acquire_level or direct
  `vips_image_new_from_file`.
- `build_tile_pyramid*`: always full load then `cut_pyramid_from_vips`.
- Interactive path in Client prefers single-cell (`request_tile`); prepare
  uses pyramid.

**Gaps / bugs in image.cpp**
1. **Asymmetry file vs buffer**: file path tiles miss JPEG shrink fast-path.
2. **ladder_acquire_level defeats JPEG DCT shrink**: the in-process shrink
   ladder (`g_ladders`, max 4 entries) always `load_full()` once then
   `vips_shrink` ×2 per scale step. Good when many tiles of one image are
   requested; **bad for cold single coarse cell** (still full decode).
3. **Client always passes non-empty decode_cache_key** for archive (`a:…`)
   and HTTP (`h:…`) interactive cells → the `vips_jpegload` shrink=2/4/8
   branch in `build_tile_cell_buffer` is **dead code on the hot path**.
   Plain files use `build_tile_cell` which never shrinks either.
4. **No quality / fast-path metadata** on stored tiles (all look the same).
5. **Pyramid ignores shrink**: generating scale 3 still decodes scale 0
   pixels first.
6. Embedded EXIF thumb used for ladder, **not** for tile cells or LQIP
   source when only a tiny preview is needed.
7. `path_looks_jpeg` is extension-based; buffer path uses magic bytes — good
   for archives renamed members.

### 4.2 Client call graph (interactive tile) — verified

```
handle_ensure_tiles (non-pyramid):
  archive → member_bytes → build_tile_cell_buffer(..., dkey="a:…")  // NO jpeg shrink
  http    → fetch_http_cached → build_tile_cell_buffer(..., dkey="h:…")  // NO jpeg shrink
  file    → build_tile_cell(path)  // ladder_acquire_level full load
  pdf/djvu → dedicated raster (rgb888 live; durable JPEG only above threshold)
```

Reply-before-durable-store is already implemented (live paint first).

### 4.3 `src/client.cpp` — orchestration

**Cache-only reads (GUI-safe)**
- `get_size` / `get_meta` / `get_lqip` / `get_tile` / `get_pixels`: SQLite +
  blob only. Warm path has no source I/O.

**Size probe (`handle_probe_size`)**
- If width/height already Ready/Incomplete: return immediately; optional
  LQIP *backfill* if missing (`store_lqip_if_missing` on file path).
- PDF/DjVu: layout size only (72dpi media box / ddjvu); status Incomplete;
  **no** LQIP on probe (thumtoo-070).
- Archive member: `member_bytes` (extract cache) → `probe_image_buffer` →
  status Incomplete. Probe itself is sequential Vips open (header-class for
  JPEG). **Does not** call LQIP in the archive branch of the final
  size_out block (only plain file://).
- HTTP: fetch → probe buffer → **does** `store_lqip_if_missing` on bytes.
- Plain file://: probe → Incomplete → **`store_lqip_if_missing` on path**
  (LQIP still coupled to first size probe for local images).

**LQIP (`store_lqip_if_missing` / `ensure_lqip`)**
- Prefer Handsum (magic `FE D6`); replace legacy ThumbHash.
- Raster sources:
  - RGB888 → encode
  - path → `lqip_thumbhash_from_file` → `vips_thumbnail(..., 32)`
  - buffer → `vips_thumbnail_buffer(..., 32)`
- PDF/DjVu in `ensure_lqip`: dedicated page raster at edge 64 (not Vips).
- Archive in `ensure_lqip`: full member extract then thumbnail_buffer 32.
- **Bug/design smell:** cold size probe for plain files still pays
  thumbnail-32 + Handsum **synchronously** in `handle_probe_size` after
  size upsert, before `size_cb`. Size is visible in SQLite mid-job (so
  SizeProbeSession can attach providers), but that worker cannot start
  the next probe until LQIP finishes. Galapix UI only *reads* `get_lqip`.

**Tiles**
- Warm: `get_tile` then reply; no decode of source.
- Cold interactive: see §4.2 (no JPEG shrink on hot path).
- Reply-before-durable-JPEG-store for live paint (good for TTFB).
- Extract cache 512 MiB; on overflow **clears entire map** (not LRU).

**Workers**
- Shared SQLite WAL; recursive_mutex on DB/BlobStore (thumtoo-051).
- Interactive tile queue FIFO (starve fixes in galapix history).

### 4.4 `src/archive.cpp`

- `read_archive_toc`: sequential libarchive headers only (skip data).
- `extract_archive_members`: single sequential pass; collect wanted members.
- No random-access API; RAR/solid archives are inherently sequential.
- Client `member_bytes`: preextracted → RAM cache → extract one member
  (which still walks the archive until that header).
- Benchmark needed: N random members via libarchive vs `unzip -p` vs
  (if present) `unrar p`.

### 4.5 LQIP encode (`lqip.cpp` / `handsum.cpp` / `image.cpp` helpers)

- Encode cost is small vs obtaining RGBA.
- `vips_thumbnail` edge 32 is **medium** (JPEG shrink-on-load), not full-res
  for JPEG — but still non-trivial I/O + decode, and runs on size probe.
- Does **not** try EXIF embedded thumb first (ladder does; LQIP does not).

### 4.6 `src/pdf.cpp`

| Op | Cost class | Notes |
|----|------------|-------|
| `pdf_page_size_72dpi` / layout | Fast | Media box; TLS layout cache per path+page |
| `pdf_rasterize_page(max_edge)` | Slow | Full page at DPI scaled to long edge |
| `pdf_rasterize_page_region` | Medium–slow | Poppler crop at target DPI; O(tile) pixels |
| `pdf_render_tile_cell` | Medium | Prefer region; **fallback full-page** if region size mismatch >2px |
| Document open | Medium | TLS per-worker document cache (path+mtime) |

- Layout DPI constant (`kPdfLayoutDpi`, historically 144) defines scale 0.
- Live interactive: rgb888; durable JPEG only for scale ≥ `kPdfMinDurableTileScale`.
- Galapix allows live min_scale down to **−8** (~36k dpi) — each cell still
  region-renders, so memory is O(256²), but CPU grows with DPI.
- Fallback full-page path is a footgun for large pages when region API
  misbehaves (rare size mismatch).

### 4.7 `src/djvu.cpp`

| Op | Cost class | Notes |
|----|------------|-------|
| page layout size | Fast–medium | Needs decoded page dimensions |
| `djvu_rasterize_page` | Slow | Full page; shared process-wide doc cache + mutex |
| region render | Medium–slow | Crop rect; y_direction top-down (thumtoo-074) |
| blank page | Fast | solid white on render failure (thumtoo-075) |

- **Process-wide mutex** on all DjVu API (thumtoo-071): correct for ddjvu,
  serializes workers — high `--jobs` does not parallelize DjVu well.
- `ddjvu_cache_clear` after each page render — avoids unbounded RAM, may
  re-decode shared structure across pages.

### 4.8 Schema / blobs

- Tiles: `(content_id, scale, x, y, width, height, codec, quality)`.
- No column for decode path / quality class (HQ full vs shrink=N vs embedded).
- Ladder levels in `blobs.sqlite` separately from tiles.
- LQIP on content row (kind + blob).

## 5. Galapix consumption

### 5.1 Time-to-first-pixel paths

| Stage | Cold cache | Warm cache |
|-------|------------|------------|
| Size | `request_size` + worker probe (+ LQIP for plain files) | `get_size` ~instant |
| Soft underlay | `get_lqip` miss until probe fills; UI never `ensure_lqip` | `get_lqip` + Handsum decode on main |
| Coarse tile | extract + **full** decode + RAM shrink ladder + JPEG | `get_tile` + JPEG decode + GL |
| Overview levels | `request_pixels` / max_scale tile after budget | cache hit |

### 5.2 `ThumtooTileProvider` (`src/thumtoo/thumtoo_tile_provider.cpp`)

- `create_from_size`: pure local; max_scale matches thumtoo loop (fit in 256²).
- `create`: **blocking** `request_size` + `drain` — must not be used in a
  per-URI loop on open (ViewerCommand uses SizeProbeSession batch instead).
- `request_tile` / `request_tiles`: callback on Client worker; JPEG/rgb888
  → RGBA8 **on worker** (good — not GUI).
- `surface_from_tile_blob`:
  - rgb888: per-pixel expand to RGBA8 (PDF/DjVu live).
  - JPEG: `surf::jpeg::load_from_mem` then convert to RGBA8 if needed.
- Warm TTFB still pays: SQLite get + JPEG decode of ≤256² + RGBA convert +
  queue to main + GL upload. Should be low tens of ms for many tiles if
  workers keep up; stampede limited by galapix job budget.

### 5.3 `ImageOverview`

- thumtoo: **only** `get_lqip` on GUI; retry each frame (galapix-082).
- Non-thumtoo: `OverviewLoadJob` + libjpeg DCT scale.
- `ensure_levels` after grid budget; ≤128 long-edge stays LQIP-only.

### 5.4 Existing measurement tools

- `thumtoo-bench`, `open_phase_bench`, `GALAPIX_OPEN_TIMING`.
- New: `thumtoo-microbench-decode`, `docs/MICROBENCH_RESULTS.md`.

### 5.5 `SizeProbeSession` (galapix)

- Open path queues `request_size` for all URIs, constructs placeholder Images,
  launches viewer, then `start_drain` on a **background thread**.
- Main-thread `tick()` polls `get_size` and attaches `create_from_size` when
  ready — does **not** wait on size callbacks.
- **Interaction with LQIP:** `handle_probe_size` upserts size, then
  synchronously runs `store_lqip_if_missing` for plain files, then posts
  `size_cb`. Size is in SQLite before LQIP finishes, so `tick()` can attach
  providers while the same worker is still encoding Handsum — good for
  layout. Throughput of the worker pool still pays LQIP cost per plain
  file before that worker takes the next probe (multi-worker mitigates).

### 5.6 `ImageTileCache` request path

- Global per-frame budget: `begin_frame_request_budget(128)` in `Viewer`
  (raised from historical 48).
- `issue_requests`: sort **finer scale first**, then distance to focus.
  Coalesces into `request_tiles` batch for ThumtooTileProvider.
- Failed/aborted cells retried up to 3 attempts (important for live PDF).
- `find_smaller_tile`: **lookup only** (no request from draw path) — avoids
  O(max_scale) job storm.
- GL upload cap: 64 tiles per image per frame.
- `cleanup()` aborts in-flight only; retains all SUCCEEDED surfaces (memory
  grows with browsing).
- Interactive thumtoo queue: **FIFO** (`enqueue(..., front=false)`); same-cell
  supersede drops obsolete pending work. (Older LIFO starved archive grids.)

### 5.7 End-to-end cold open (many JPEGs in ZIP)

```
expand TOC → request_size×N → drain workers:
  per member: extract (sequential archive coalesce) → probe header
              → upsert size → LQIP thumb32+Handsum  [plain-like member]
UI: get_size → provider → overview get_lqip (may still miss)
    → issue_requests coarse cells → full JPEG decode path (§4.2)
    → reply RGB/JPEG → worker RGBA → upload ≤64/frame
```

Warm open skips extract/probe/LQIP/decode-source; still pays get_tile +
JPEG decode 256² + RGBA + GL.

## 6. Things we could do but do not (yet)

1. Systematic EXIF / embedded thumbnail for **any** first pixel (tiles + LQIP).
2. JPEG shrink on **file-path** `build_tile_cell`.
3. Shrink-aware pyramid (decode at max needed shrink, build finer from
   that only when requested — or decode once at shrink=1 for prepare HQ).
4. DB flag `tile_quality` / `decode_path` (full | jpeg_shrink_N | embedded).
5. Progressive JPEG / hierarchical scans for intermediate display (rare).
6. libjpeg-turbo direct API for header-only and scaled decode microbench
   baseline vs Vips.
7. Archive: memory-map ZIP central directory; compare libarchive vs CLI
   tools for random member extract latency.
8. Store original SOF dimensions without opening pixels for more formats
   (PNG IHDR, etc. — Vips sequential usually already cheap).

## 7. Benchmark plan (to implement)

Corpus: synthetic JPEGs at 4K, 8K, 16K; PNG; JXL; ZIP/RAR of many JPEGs;
single multipage PDF/DjVu if tools present.

| Bench | Metric |
|-------|--------|
| Header-only size | ns/file, JPEG/PNG |
| Full decode | ms, peak RSS |
| jpegload shrink 2/4/8 | ms + optional PSNR vs full+downsample |
| EXIF thumb extract + decode | ms |
| Ladder edge 256 | ms cold/warm |
| Tile cell scale 0 vs 3 | ms |
| Pyramid full | ms |
| Archive TOC | ms |
| Archive random member × N | ms libarchive vs unzip/unrar |
| Client get_tile warm | µs |
| End-to-end request_tile cold | ms |

Output: machine-readable JSON + markdown table in this doc.

## 7b. Policy constants (`include/thumtoo/constants.hpp`)

| Constant | Value | Role |
|----------|-------|------|
| `kLadderEdges` | 128…2048 | Display ladder long edges |
| `kDefaultJxlQuality` | 80 | Ladder encode |
| `kTileSize` | 256 | Grid cell |
| `kDefaultTileQuality` | (see constants) | JPEG tiles |
| `kPdfLayoutDpi` | 144 | Scale 0 / size layout |
| `kPdfMinDurableTileScale` | (see file) | Below this: live only |
| `kArchiveMaxMemberUncompressedBytes` | 512 MiB | Extract cap |
| `Client::kExtractCacheMaxBytes` | 512 MiB | Process RAM cache |
| `kLadderCacheMaxEntries` | 4 | In-process shrink ladder |
| Galapix frame tile budget | 128 | `begin_frame_request_budget` |
| Galapix GL uploads/image/frame | 64 | `kMaxUploadsPerFrame` |

## 8. Proposed fixes (not implemented — discuss before code)

### 8.1 Interactive JPEG shrink on the hot path

**Problem:** `build_tile_cell_buffer` only uses `vips_jpegload(shrink=N)` when
`decode_cache_key.empty()`. Client always passes `a:…` / `h:…` keys; file path
uses `build_tile_cell` → `ladder_acquire_level` full load.

**Option A — single-cell fast path (preferred for TTFB)**  
When interactive request is **one cell** and no other scale for that key is
cached yet:

1. If magic/path is JPEG and `scale > 0`, load with `shrink = jpeg_shrink_factor_for_scale(scale)`.
2. Crop cell; JPEG-encode; reply.
3. Optionally **do not** populate the full-res RAM ladder (or populate only
   the shrink level) so memory stays low.

When the same image later needs multiple scales/tiles, fall back to full load
+ ladder (current behaviour) or progressively fill ladder from finest
requested.

**Option B — shrink-aware ladder_acquire_level**  
Change `load_full` to `load_at_min_scale(scale)`: first open uses
`jpegload(shrink=min(8, 2^scale))`, store as level `applied_steps`, then
`vips_shrink` only remaining steps. Non-JPEG still full load.

**Option C — leave ladder; fix only file path**  
Mirror buffer JPEG branch into `build_tile_cell` when scale>0 and ladder miss.
Does not fix archive/HTTP unless decode_cache_key is empty for single-shot.

Recommend **A** for cold coarse tiles (gallery overview), **B** if multi-tile
same image is the common case after first paint.

### 8.2 Decouple LQIP from size probe

Size callback should not wait on Handsum. Enqueue LQIP as a separate low-priority
job after size is stored (or only on `ensure_lqip` / EnsurePixels). Galapix
already tolerates missing LQIP and retries `get_lqip`.

### 8.3 Optional `tile_source` / quality column

`tiles` table: add `source TINYINT` — `0=full`, `1=jpeg_shrink`, `2=embedded`,
`3=pdf_region`, … Re-generate if UI requests HQ and only fast-path exists.
Not required for correctness; helps cache policy and debugging.

### 8.4 Extract cache LRU

Replace clear-all-on-overflow with size-based LRU so large albums do not
thrash the entire 512 MiB map after one oversized member.

## 9. Progress log


- 2026-09-09: Initial taxonomy and image.cpp findings written.
- 2026-09-09: Client/LQIP/archive/galapix paths documented; JPEG shrink
  dead on interactive hot path confirmed.
- 2026-09-09: Pillow microbench corpus + results in
  `docs/MICROBENCH_RESULTS.md` (size_only ~0.03ms; draft scale only
  1.3–2.3× vs full on large JPEG; PIL thumb32 on 8K ~207ms).
- 2026-09-09: `tools/microbench_decode.cpp` + CMake target
  `thumtoo-microbench-decode` (needs vips build).
- 2026-09-09: PDF/DjVu cost tables; Galapix `ThumtooTileProvider` decode
  path; fix proposals §8 (JPEG shrink, LQIP decoupling, tile_source, LRU).
- 2026-09-09: SizeProbeSession, ImageTileCache budget (128), FIFO tiles,
  end-to-end cold ZIP open diagram (§5.5–5.7).
- Next: implement Option A under discussion; vips numbers under nix;
  RAR corpus; constants.hpp policy table.

