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

### 4.3 `src/client.cpp` — orchestration (partial)

- `get_size`: cache only.
- `request_size` / probe handlers: hash caching (thumtoo-070); PDF/DjVu
  size without LQIP raster on probe (fixed); LQIP backfill separate.
- `ensure_lqip`: page-aware for PDF/DjVu after thumtoo-069.
- `request_tile` / pyramid: routes to `build_tile_cell*` or PDF/DjVu
  builders; archive bytes via extract cache.
- Multi-worker queue; interactive FIFO (thumtoo-062 era).
- Same-archive coalesce for probe and tiles.

**Open questions for deeper pass**
- Exact condition when `decode_cache_key` is passed into
  `build_tile_cell_buffer` (does interactive single cell pass empty key?).
- Whether warm `get_tile` avoids all decode (expected yes).

### 4.4 `src/archive.cpp`

- Sequential libarchive walk for TOC.
- Member extract; Client holds process-RAM extract cache (512 MiB).
- No comparison yet vs `unzip -p` / `unrar p` random access (benchmark).

### 4.5 `src/lqip.cpp` / `src/handsum.cpp`

- Pure encode from RGBA; cost is dominated by obtaining the small raster
  upstream, not the hash math.
- Historical bug: LQIP tied to size probe → full decode; mitigated for
  multipage docs.

### 4.6 `src/pdf.cpp` / `src/djvu.cpp`

- Region/page raster at needed resolution for tiles; blank DjVu → white
  (thumtoo-075).
- Must never go through Vips/Magick (thumtoo-073).

### 4.7 Schema / blobs

- Tiles: `(content_id, scale, x, y, width, height, codec, quality)`.
- No column for `source_path` quality class (HQ full-decode vs shrink=8).
- Ladder levels in `blobs.sqlite` separately from tiles.

## 5. Galapix consumption (outline)

- `ThumtooTileProvider` + async size probe (`SizeProbeSession`).
- Overview LQIP path (`ImageOverview`) — must not block UI (galapix-082).
- Time to first pixel: warm cache should be SQLite get + decode JPEG tile
  + GL upload; cold = probe + extract + generate.
- Existing benches: `open_phase_bench`, `GALAPIX_OPEN_TIMING`, `thumtoo-bench`.

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

## 8. Progress log

- 2026-09-09: Initial taxonomy and image.cpp findings written.
- Next: finish client.cpp call graph for decode_cache_key; implement
  microbench tool; run numbers.

