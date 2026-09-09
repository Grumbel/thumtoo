## EPUB via MuPDF — **thumtoo-103**

- [x] Design [docs/EPUB.md](docs/EPUB.md)
- [x] URI `//epub:w,h,em` + `//page:N`; format classify `.epub` before zip
- [x] `epub.cpp`: layout, page count, layout size, region/tile raster (MuPDF)
- [x] `expand_media_uris` default profile pages
- [x] Client size probe + live tiles + LQIP
- [ ] Galapix open/expand integration
- [ ] User CSS / presets beyond defaults

---

## PDF dual backend (MuPDF + Poppler) — **thumtoo-094+**

- [x] Design [docs/PDF_BACKENDS.md](docs/PDF_BACKENDS.md)
- [x] URI: `//page:`, `//poppler-page:`, `//mupdf-page:` + `PdfBackend`
- [x] MuPDF module + cmake/flake (`THUMTOO_HAVE_MUPDF`)
- [x] Dispatch render/count/stats by backend
- [x] Default `//page:` → MuPDF when available (`pdf_resolve_backend`)
- [x] Galapix min_scale + URI backend (galapix-165)
- [x] MuPDF image coverage via fz_stext image blocks
- [x] Implement `with_pdf_page_poppler` / `with_pdf_page_mupdf` (link fix for test_uri)
- [x] Silence `-Wclobbered` in `pdf_mupdf.cpp` via MuPDF `fz_var` (not C++ `volatile` on structs)
- [x] `is_pdf_page_uri` recognizes `//poppler-page:` / `//mupdf-page:`
- [x] test_pdf_tiles: denser fixture text (avoid sparse-text image_heavy gate)
- [x] MuPDF region scissor is device-space (fix white bottom tiles)

Tip: **thumtoo-103**.

---

## PDF image-heavy gate for live tiles — **thumtoo-093**

- [x] `PdfPageContentStats` + `pdf_page_allows_live_tiles`
- [x] Optional poppler-glib image coverage; text heuristic fallback
- [x] Refuse scale < 0 render for image-heavy pages
- [x] Full-page cache only at scale ≥ 0
- [x] Bundle thumtoo-093

---

## PDF page/raster TLS cache — **thumtoo-092**

- [x] TLS `poppler::page` cache (avoid create_page per cell)
- [x] Full-page RGB cache ≤4096 long edge for scanned multi-tile pages
- [x] Document: no PDF “scanned” metadata; region re-decode cost
- [x] Bundle thumtoo-092

---

## Archive batch parallel encode — **thumtoo-090**

`thumtoo-bench --tiles foo.rar` looked "stuck" with low CPU: coalesce ran
one sequential RAR extract then every pyramid on a single worker.

- [x] Hit extract_cache before opening the archive again
- [x] Parallelize post-extract probe/tile/pixel work across pool threads
- [x] Bench help notes solid RAR / --tiles cost
- [x] Bundle thumtoo-090

---

## Fix: LQIP must not starve tile workers — **thumtoo-088**

Inline Handsum after the first tile blocked the same worker from encoding
more cells → black/empty until the queue drained. LQIP is now a separate
low-priority `EnsureLqip` job after durable tile store.

- [x] `request_lqip` / `JobKind::EnsureLqip`
- [x] Remove inline post-tile Handsum from `handle_ensure_tiles`
- [x] Bundle thumtoo-088

---

## LQIP after first thumbnail (2026-09-09) — **thumtoo-086**

- [x] No LQIP on size probe (archive path cleaned)
- [x] Generate LQIP after first interactive durable tile
- [x] EnsurePixels still stores LQIP from in-memory RGB after ladder
- [x] Bundle thumtoo-086

---

## Code: Option A + LQIP + LRU + tile source (2026-09-09) — **thumtoo-083**

- [x] `build_tile_cell` / `_buffer`: JPEG `scale > 0` → `vips_jpegload(shrink=N)`
- [x] Size probe no longer generates LQIP (ensure_lqip / EnsurePixels only)
- [x] Extract cache size-based LRU (no clear-all)
- [x] `tiles.source` + `TileSource` enum; migrate via PRAGMA table_info
- [x] Bundle thumtoo-083

---

## DjVu blank pages → white tiles (2026-09-09) — **thumtoo-075**

Some intentional blank pages fail `ddjvu_page_render` (returns 0). That became
nullopt → Galapix purple missing-tile placeholder. Emit solid white RGB instead.

- [x] Full-page + region render: white on render failure
- [x] Bundle thumtoo-075

---

## DjVu tile Y-direction (2026-09-09) — **thumtoo-074**

Pages shifted upward when zooming in: ddjvu default y-direction is bottom-up
(PostScript). Tile crops used top-down (image) coordinates without setting
`ddjvu_format_set_y_direction(fmt, 1)`.

- [x] Set y_direction + row_order top-to-bottom on all page renders
- [x] Bundle thumtoo-074

---

## BUG: never open PDF/DjVu via Vips/Magick (2026-09-09) — **thumtoo-073**

Root cause of 60GB+: `vips_image_new_from_file` / `vips_thumbnail` on a
`.djvu` goes through ImageMagick, which decodes multipage DjVu at full
resolution (see prior stack: ReadDJVUImage → ddjvu_page_render).

- [x] Refuse PDF/DjVu in probe_image_file, build_ladder, lqip_from_file, build_tile_pyramid
- [x] ensure_pixels / size probe: fail bare container URIs (page_uri_required)
- [x] Pyramid: cell-by-cell, no full native RGB
- [x] Bundle thumtoo-073

---

## DjVu: one shared document + Vips concurrency 1 (2026-09-08) — **thumtoo-071**

TLS per-worker document cache opened the same multipage book N times (RAM thrash).
Vips concurrency × worker pool compounded threads/memory on --ladder.

- [x] Process-wide DjVu document cache, all API under one mutex
- [x] vips_concurrency_set(1)
- [x] Bundle thumtoo-071

---

## Fast size probe for multipage docs (2026-09-08) — **thumtoo-070**

Opening a 250-page DjVu was still extremely slow at "Probing image sizes":
each page re-hashed the whole file and rasterized for LQIP.

- [x] Cache sha256_file_hex by path+mtime
- [x] Size probe: dimensions only for PDF/DjVu (no per-page LQIP raster)
- [x] Bundle thumtoo-070

---

## LQIP: no Magick on PDF/DjVu containers (2026-09-08) — **thumtoo-069**

Size probe / ensure_lqip used path_from_file_uri which strips //page: and fed
the .djvu/.pdf path to Vips→Magick→ddjvu_page_render (whole doc, GUI stall).

- [x] ensure_lqip: per-page small raster for PDF/DjVu
- [x] size probe LQIP: same; skip Magick for page URIs
- [x] Bundle thumtoo-069

---

## CMake feature summary (2026-09-08) — tip **thumtoo-067** / bundle **thumtoo-067**

- [x] Configure-time feature summary (PDF / DjVu / curl / archive / vips / sqlite)
- [x] Bundle thumtoo-067

---

## expand_media_uris + PRIVATE decoder link (2026-09-08) — tip **thumtoo-066** / bundle **thumtoo-066**

- [x] `thumtoo::expand_media_uris` / `is_openable_media_path` (PDF, DjVu, archive, image)
- [x] Link Poppler / DjVuLibre / libcurl **PRIVATE** (feature macros stay PUBLIC)
- [ ] Galapix uses expand API; drops format-specific expand
- [x] Bundle thumtoo-066

---

## DjVu multipage: require DjVuLibre discovery (2026-09-08) — tip **thumtoo-065** / bundle **thumtoo-065**

Symptom: only the first page of a multipage .djvu appears. Galapix compile
flags showed THUMTOO_HAVE_POPPLER but **not** THUMTOO_HAVE_DJVU — thumtoo was
built without finding `ddjvuapi.pc`, so `djvu_page_count` always returns null
and page expansion never runs (single URL → first page via other paths).

- [x] CMake: clearer WARNING when ddjvuapi missing; try `djvulibre` pc name
- [x] flake: `djvulibre.dev` on PKG_CONFIG_PATH
- [x] page_count: extra message pump after decode
- [x] Bundle thumtoo-065

---

## DjVu pages via DjVuLibre (2026-09-08) — tip **thumtoo-064** / bundle **thumtoo-064**

Mirror PDF page support for `.djvu` / `.djv` using **ddjvuapi** (DjVuLibre).

- `is_djvu_path` / PathKind::Djvu / MIME `image/vnd.djvu`
- `//page:N` URIs (same pipe as PDF); `parse_pdf_uri` only matches `.pdf`
- Size probe, live RGB888 tiles, durable JPEG ≥ `kPdfMinDurableTileScale`
- Thread-local document cache (same worker model as Poppler)
- Optional: `pkg-config ddjvuapi` → `THUMTOO_HAVE_DJVU`

- [x] Code
- [x] Bundle thumtoo-064

---

## Interactive tiles: FIFO queue (2026-09-08) — **thumtoo-063**

LIFO (`enqueue(..., front=true)`) starved older EnsureTiles under continuous
pan/zoom — Galapix saw permanent REQUESTED with no fail/abort.

### Fix
- `request_tile` / `request_tiles` enqueue **FIFO**
- Same-cell supersede still drops obsolete single-cell pending jobs (nullopt)
- Rebased onto origin (PDF document cache already on master)

- [x] Code
- [x] Bundle thumtoo-063

---

## PDF: thread-local document cache (2026-09-08) — **thumtoo-061**

Interactive PDF tiles called `poppler::document::load_from_file` on **every**
cell (layout size + region render + optional full-page fallback). A viewport
of N tiles reopened the same PDF N–3N times.

### Fix
- `thread_local` open-document cache keyed by path + mtime (Poppler is not
  cross-thread safe; matches Client worker model)
- Cache media-box size at 72 dpi per path+page for layout queries

- [x] Code
- [x] Bundle thumtoo-061

---

## Index-based tile batch completion (2026-09-08) — **thumtoo-060**

Fresh-generate tiles could stay REQUESTED forever: batch used one shared
TileCallback that re-matched scale/x/y; missed matches never completed
Galapix JobHandles. `request_tiles` now calls `on_cell(index, tile)`.

- [x] Code
- [x] Bundle thumtoo-060

---

## Unstick interactive tile batch (2026-09-08) — **thumtoo-059**

Pending requests could sit at ~100 after batch path:

1. Every cell re-ran `handle_probe_size` (LQIP backfill could re-encode full image)
2. Exception on one cell aborted the rest with JobHandles left REQUESTED

### Fix
- Probe **once** per `request_tiles` batch; children set `skip_probe`
- try/catch per cell + nullopt reply so every JobHandle completes

- [x] Code
- [x] Bundle thumtoo-059

---

## Interactive tiles: reply before durable JPEG (2026-09-08) — **thumtoo-058**

### Root cause
Interactive `EnsureTiles` JPEG-encoded and wrote SQLite **before** `reply_one`.
After the shrink ladder held the image, each cell still paid encode+store
before the next cell could run — ~1s trickle for a zoomed grid.

### Fix
- Reply RGB (or cache hit) **first**
- `request_tiles` batch sets `skip_durable` and flushes JPEG/SQLite **after**
  every cell has been replied

- [x] Code
- [x] Bundle thumtoo-058

---

## Interactive multi-cell request_tiles batch (2026-09-08) — **thumtoo-057**

`Client::request_tiles(uri, coords, cb)` enqueues **one** EnsureTiles job for
many cells of the same image. The worker shares size-probe / shrink-ladder
work instead of N competing queue entries. Galapix deep-zoom uses this so
visible tiles arrive together instead of trickling over ~1s.

- [x] API + handle_ensure_tiles batch loop
- [x] Bundle thumtoo-057

---

## Interactive tile queue LIFO + coalesce (2026-09-08) — **thumtoo-056**

`request_tile` (single-cell interactive) enqueues at the **front** of the
worker queue so the latest view is processed before a backlog of intermediate
pan/zoom cells. Older pending jobs for the same (uri, scale, x, y) are dropped
and complete with empty. `request_tile_pyramid` / size / pixels stay FIFO
(`push_back`).

- [x] deque + enqueue(front)
- [x] Bundle thumtoo-056

---

## Optional unrar/unzip extract backends (2026-09-08) — backlog

libarchive covers most zip/rar/7z/tar. Gaps worth a future thumtoo path:

1. **unrar** for RAR subformats libarchive cannot open
2. **External unrar/unzip** for single-member extract (often faster than
   iterating the whole archive with libarchive)

Keep behind capability detection; default remains libarchive.
Galapix no longer ships arxpcpp — any such backends belong here.

- [ ] Design capability probe + fallback order
- [ ] unrar single-member extract
- [ ] unzip single-member extract (optional)

---

## Fix Database GC methods outside namespace (2026-09-08) — **thumtoo-054**

Same class of bug as thumtoo-053: GC helpers from thumtoo-052 were appended
after `} // namespace thumtoo` in `database.cpp`.

- [x] Move methods inside `namespace thumtoo`
- [x] Bundle thumtoo-054

---

## Fix BlobStore GC methods outside namespace (2026-09-08) — **thumtoo-053**

`delete_tiles_below_scale` / `delete_tiles_for_content` /
`delete_levels_for_content` were appended after `} // namespace thumtoo` in
`blob_store.cpp` (thumtoo-052), so the compiler saw free functions and
`BlobStore` was undeclared.

- [x] Move methods inside `namespace thumtoo`
- [x] Bundle thumtoo-053

---

## thumtoo-gc (2026-09-08) — **thumtoo-052**

Manual cache cleanup CLI (no automatic eviction):

```
thumtoo-gc --dry-run --min-scale 3 --orphans --dead-paths
```

- `--min-scale N` — drop tiles with scale < N (keep coarser overview)
- `--orphans` — content with no locators + blob purge
- `--dead-paths` — locators whose outer file is gone

- [x] Database/BlobStore GC helpers
- [x] tools/thumtoo_gc.cpp + CMake/flake

---

## LQIP backfill when size already known (2026-09-08) — tip **thumtoo-045**

Probe early-return skipped LQIP for content that already had width/height
(typical warm cache from before ThumbHash). Backfill on that path +
`Client::ensure_lqip()` for explicit fill.

### Status
- [x] Bundle thumtoo-045

---

## HTTP probe LQIP + DESIGN note (2026-09-08) — tip **thumtoo-044**

Encode ThumbHash during HTTP size probe (cached body). Document LQIP stack in
DESIGN.md.

### Status
- [x] Bundle thumtoo-044

---

## Archive-member LQIP on probe (2026-09-08) — tip **thumtoo-043**

`lqip_thumbhash_from_buffer` + encode ThumbHash during archive member size
probe (bytes already in memory).

### Status
- [x] Bundle thumtoo-043

---

## LQIP on size probe + unit test (2026-09-08) — tip **thumtoo-042**

Encode ThumbHash during `handle_probe_size` for local files so cold gallery
open gets inline LQIP without waiting for `request_pixels` / ladder.

Add `tests/test_lqip.cpp` encode/decode round-trip.

### Status
- [x] Bundle thumtoo-042

---

## Inline LQIP (ThumbHash) on content rows (2026-09-08) — tip **thumtoo-040**

### Goal
Extremely small gallery placeholders stored **on the content row** (~25–37 B
ThumbHash) so first paint for ~1000 images does not touch blob storage.

### Done
- Schema v2: `content.lqip` BLOB + `content.lqip_kind`
- ThumbHash encode/decode (`lqip.hpp` / `lqip.cpp`)
- `Database::get_lqip` / `set_lqip`
- `Client::get_lqip(uri)` cache-only
- Encode after successful ladder build (file path + PDF rgb)

### Next (Galapix)
- Paint LQIP under overview when present
- Optional: request_pixels only after LQIP shown / larger on-screen size

### Status
- [x] thumtoo-040 bundle

---

## tests: request_tile callback expects rgb888 (2026-09-08) — tip **thumtoo-039**

Interactive `request_tile` returns `codec=rgb888`; durable `get_tile` remains JPEG.

---

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

---

## Thumbnail generation audit + microbenchmarks (2026-09-09) — **in progress**

Goal: exhaustive audit of thumbnail / ladder / tile generation in thumtoo
(and galapix consumption), with measured numbers — not guesses.

### Deliverables
- [ ] `docs/THUMBNAIL_AUDIT.md` — file-by-file, slow vs fast ops, bugs, gaps
- [ ] Extended `thumtoo-bench` (or new `tools/microbench_*`) covering:
  - JPEG full decode vs shrink=2/4/8 vs header-only size
  - Quality comparison (PSNR/SSIM or visual samples) of shrink path
  - EXIF embedded thumb vs full `vips_thumbnail`
  - Archive extract: libarchive random member vs `unzip`/`unrar` CLI
  - Cold vs warm cache time-to-first-pixel paths
  - LQIP generation cost vs size-only probe
  - PNG/JXL/WebP vs JPEG decode costs
- [ ] Document every place that still does full-resolution work when a
  cheaper path exists
- [ ] Propose schema flag for "tile is high-quality vs fast-path" if useful

### Fast vs slow taxonomy (working)

| Operation | Expected class | Current implementation |
|-----------|----------------|------------------------|
| Image dimensions only | Fast (header) | `probe_image_*` via VIPS_ACCESS_SEQUENTIAL |
| EXIF embedded JPEG thumb | Fast | Used in `build_ladder` for JPEG when large enough |
| `vips_jpegload(..., shrink=N)` | Medium | Used in `build_tile_cell_buffer` for scale>0 JPEG |
| `vips_thumbnail` / full decode | Slow | Ladder fallback; pyramid path loads full |
| Archive TOC | Medium | libarchive sequential |
| Archive member extract | Slow | libarchive; in-process extract cache 512 MiB |
| LQIP/ThumbHash encode | Medium | Needs small RGBA raster |
| Tile JPEG encode | Medium | Parallel per scale in prepare |
| PDF/DjVu page raster | Slow | Poppler / ddjvu at requested DPI/region |

### Known design issues to verify in audit
- LQIP generated alongside size probe historically (partially fixed thumtoo-070)
- `build_tile_cell` for file path (non-buffer) may not use jpeg shrink
- Pyramid still full-loads non-JPEG
- No durable flag distinguishing fast-path vs HQ tiles
- Embedded thumbs used for ladder but not systematically for tiles/LQIP
- Galapix overview / size probe interaction with LQIP

### Progress
- [x] Read DESIGN.md, TILES.md, image.cpp probe/ladder/tile paths
- [x] Deep pass: image.cpp
- [x] Deep pass: client.cpp request_size / ensure_lqip / request_tile
- [x] Deep pass: archive.cpp extract paths (TOC + sequential extract)
- [x] Deep pass: lqip/handsum obtain path (vips_thumbnail 32)
- [x] Deep pass: pdf.cpp / djvu.cpp raster costs
- [x] Deep pass: galapix ImageOverview + ThumtooTileProvider
- [x] Microbench harness (Pillow results + C++ vips tool wired)
- [x] Initial numbers in docs/MICROBENCH_RESULTS.md
- [x] Propose JPEG shrink / LQIP / schema fixes (audit §8)
- [ ] Vips/libjpeg numbers under nix develop
- [ ] RAR / solid archive extract comparison
- [ ] Implement interactive JPEG shrink (after agreement on Option A/B)
- [ ] Decouple LQIP from size probe (after agreement)

- [x] ImageTileCache / SizeProbeSession / FIFO notes (§5.5–5.7)
- [x] Policy constants table (§7b)

- [x] Archive coalesce / warm extract skip documented
- [x] Schema + BlobStore tile path documented
- [x] ZIP stored/deflate extract numbers in MICROBENCH_RESULTS

- [x] prepare CLI / BuildStats / expand documented
- [x] Executive summary in THUMBNAIL_AUDIT.md

- [x] gc / status / format / hashing / non-thumtoo DCT contrast
- [x] Coverage checklist; audit marked complete for handoff

### Audit complete (docs)
Canonical: [docs/THUMBNAIL_AUDIT.md](docs/THUMBNAIL_AUDIT.md),
[docs/MICROBENCH_RESULTS.md](docs/MICROBENCH_RESULTS.md).

**Code follow-ups:**
- [x] Interactive JPEG shrink Option A (§8.1) — thumtoo-083
- [x] Decouple LQIP from size probe (§8.2) — thumtoo-083
- [x] Extract-cache LRU (§8.4) — thumtoo-083
- [x] Optional tile `source` column (§8.3) — thumtoo-083

**Numbers follow-ups:**
- [ ] `thumtoo-microbench-decode` under nix+vips
- [ ] Solid RAR extract comparison

Tip: **thumtoo-082**.
