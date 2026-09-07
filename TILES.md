<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Grid tiles (Phase 4)

Optional **galapix-compatible** tile pyramid on top of the fixed long-edge
ladder. Ladder remains the primary biltoo path; tiles serve deep zoom and the
Galapix collection viewer.

## Model (Galapix-aligned)

| Field | Meaning |
|-------|---------|
| `scale` | 0 = full resolution; each +1 halves width and height |
| `x`, `y` | Tile indices from the top-left of that scale’s image |
| Tile size | **256×256** (`kTileSize`); edge tiles may be smaller |
| Codec | Default **JPEG** q=80 (`kDefaultTileCodec` / `kDefaultTileQuality`) |
| Source cap | Encode refused when `width*height > kTileMaxSourcePixels` (100 MP) |

Whole-image coverage at scale `s` is:

```text
tw = ceil(width  / 2^s / 256)
th = ceil(height / 2^s / 256)
```

`max_scale` is the smallest `s` where `tw == 1 && th == 1` (or a configured cap).

## Storage

- **index.sqlite** `tiles(content_id, scale, x, y, width, height, codec, quality)`
- **blobs.sqlite** `tile_blobs(... same key ..., data BLOB)`
- Content identity is still `sha256:…` / provisional; locators unchanged.

Additive schema only; `schema_version` stays 1 until a breaking migration.

## API (Client)

```text
get_tile(uri, scale, x, y) -> optional<TileBlob>
request_tile(uri, scale, x, y, callback)
get_tile_coverage(uri) -> optional{min_scale, max_scale, Size}
request_tile_pyramid(uri, min_scale, max_scale, callback)  // optional batch
```

`TileBlob`: scale, x, y, width, height, codec, bytes.

On miss, the worker loads the source once (file or archive member), builds the
requested scale **and all coarser scales** in one pass, stores them, then
invokes the callback. Finer scales than already stored are generated only when
asked. Sources larger than `kTileMaxSourcePixels` yield no tiles (probe/size
still work).

## Prepare

`thumtoo-prepare --tiles [paths…]` prewarms full pyramids (subject to source
size policy). Default prepare path does **not** build tiles.

## Non-goals

- GUI / OpenGL cache (stays in Galapix)
- Video or PDF page tiles in the first cut
- Replacing the JXL ladder

## Integration

See [INTEGRATION.md](INTEGRATION.md) for biltoo. Galapix mapping will live in
`INTEGRATION_GALAPIX.md` once the Client tile API is stable: replace
`SQLiteTileDatabase` / `TileGenerator` with thumtoo `get_tile` / `request_tile`
keyed by URL → content_id.

## Performance notes (2026-09-07)

* Interactive `request_tile`: **one scale only** (`tile_max_scale = scale`). Use
  `request_tile_pyramid` / `thumtoo-prepare --tiles` for full pyramids.
* `prepare --tiles --stats --jobs N`: wall time vs CPU-share (jpeg usually
  dominates after extract cache).
* Archive members: extract once per batch when possible; bytes may live briefly
  in the in-process extract cache (max 512 MiB).
* Encoding: parallel JPEG for cells within a scale; Client runs multiple jobs
  across URIs concurrently.

## Interactive vs prepare

* `request_tile(uri, scale, x, y)` encodes **only that cell** (load → shrink to
  scale → crop → JPEG). Does not fill the rest of the scale grid.
* `request_tile_pyramid` / `thumtoo-prepare --tiles` still build full scale
  ranges for offline prewarm.

### JPEG load for interactive cells

For `.jpg` sources, `build_tile_cell` uses `vips_jpegload(..., shrink=2|4|8)` so
coarse scales avoid a full-resolution decode, then applies any remaining
factor-of-two shrinks to match Galapix scale. Non-JPEG formats still full-load.

## Scale range (detail cutoff)

Galapix scale **0 = full resolution**; higher = coarser.

* `request_tile_pyramid(uri, min_scale, max_scale)`  
  - `min_scale`: finest scale to store (0 = full res; `1` skips full-res tiles)  
  - `max_scale < 0`: generate until the image fits in one 256² tile  
* CLI: `thumtoo-prepare --tiles --min-scale N --max-scale M`  
  (`--min-scale` / `--max-scale` imply `--tiles`)  
* Interactive `request_tile` still encodes only the requested cell; the app
  chooses which scales to ask for.


## PDF layout DPI + region tiles (2026-09-07)

Page `get_size` uses **kPdfLayoutDpi (144)** (media box × 144/72) as the
**nominal** full-resolution size for layout. Do not change this constant
without migrating or discarding existing tile rows.

### Scale geometry (must stay consistent with Galapix)

Let `L` = layout size at 144 dpi. Tile size `T = 256`.

| scale `s` | full pixel grid | dpi | tile `(x,y)` covers layout px |
|----------:|-----------------|-----|-------------------------------|
| 0 | `L` | 144 | `[xT,(x+1)T) × [yT,(y+1)T)` of `L` |
| +1 | `L/2` | 72 | 2× coarser |
| −1 | `2L` | 288 | half the linear span of a scale-0 tile |

`full = pdf_page_size_at_scale(L, s)` → `round(L * 2^{-s})`  
`dpi = kPdfLayoutDpi * 2^{-s}`

Interactive `request_tile` for `//page:N` region-rasterizes one cell (Poppler
crop) or full-page + software crop fallback. Each cell is ≤ `T²` pixels.
Durable cache keys `(content_id, scale, x, y)`. PDF cells finer than
`kPdfMinDurableTileScale` (−2, 576 dpi) are generated live and **not** stored.
PDF cells encode at `kPdfTileQuality` (95), not `kDefaultTileQuality` (80).

Raster images never use negative scale.
