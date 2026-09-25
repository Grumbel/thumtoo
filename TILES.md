<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Display pixels: grid tiles (durable) vs whole-image helpers

thumtoo exposes **one durable multi-res pixel path** (grid tiles) plus
ephemeral whole-image helpers. Normative policy:
[docs/PIXEL_AND_ARCHIVE_POLICY.md](docs/PIXEL_AND_ARCHIVE_POLICY.md).

| Path | API | Durable? | Typical use |
|------|-----|----------|-------------|
| **Grid tiles** | `get_tile` / `request_tile` | **Yes** | Deep zoom, Gallery/Image display, Galapix |
| **LQIP** | `get_lqip` / `request_lqip` | **Yes** | Tiny placeholder |
| **Whole-image soft / overview** | `get_pixels` / `request_pixels` | **No** (ephemeral or TileSynth) | Filmstrip cold path; PreferCache plateau |

## Soft / whole-image rules (ephemeral)

1. **Not stored.** Store schema ≥ 100 does not persist soft ladder levels.
   `get_pixels` returns TileSynth when a complete tile scale exists, else miss.
2. **`request_pixels`** may still generate a ≤ `kMaxSoftLadderEdge` (512) raster
   in the worker and return it once (RAM / host ImageCache only).
3. **Prefer tiles.** Hosts that can paint cells (biltoo) should schedule
   `request_tile` rather than relying on soft replies.
4. Consumers that need more than a soft edge on screen should call
   **`request_tile`** or a full source decode — not larger soft levels.


## Grid tiles (Phase 4)

Optional **galapix-compatible** tile pyramid.
Tiles are the durable multi-res path; soft/overview is ephemeral or TileSynth.

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

- **index.sqlite** `tiles(content_id, scale, x, y, width, height, codec, quality, source)`
  (`source`: 0=full, 1=jpeg_shrink, 3=pdf_region, 4=djvu_region — see `TileSource`)
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

On miss, interactive `request_tile(s)` encode **one cell** (not the full pyramid).
JPEG `scale>0` uses DCT `jpegload` shrink; concurrent cells of the same file share
one shrink decode (`jpeg_shrink_acquire`, tip 305). Pyramid prepare still builds
all scales offline.

Historical note: an older path built the requested scale and coarser scales in one
pass on miss; interactive path is single-cell. Finer scales than already stored are generated only when
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

**Intended:** For JPEG sources, `build_tile_cell_buffer` can use
`vips_jpegload(..., shrink=2|4|8)` so coarse scales avoid a full-resolution
decode, then apply remaining factor-of-two shrinks.

**Current (2026-09-09, Option A):** Interactive `build_tile_cell` /
`build_tile_cell_buffer` use `vips_jpegload(..., shrink=2|4|8)` for
**scale > 0** JPEG sources (file path and buffer), independent of
`decode_cache_key`. Stored tiles record `source = JpegShrink`. Scale 0 and
non-JPEG still full-load; multi-cell scale 0 can use the in-process shrink
ladder after a full decode.

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

`full = pdf_page_size_at_scale(L, s)`:
- `s >= 0`: successive floor-half (`dim_at_tile_scale`) — matches image pyramid
- `s < 0`: exact `L * 2^{-s}` (integer)

`dpi = kPdfLayoutDpi * 2^{-s}`

Interactive `request_tile` for `//page:N` builds exclusive cells from a
**full-page raster** at that scale’s dpi (thread-local page level cache), then
crops — same model as image tiles. Per-cell region draws are only a fallback
when the page level would exceed `kTileMaxSourcePixels`.

**Layout pixels:** one `lround(page_pt * dpi/72)` from the continuous page
bound (`fz_bound_page`). Do not round to integer 72dpi points and scale again
(that drifts by up to 1 device pixel vs the region ctm).

Each cell is ≤ `T²` pixels. Durable cache keys `(content_id, scale, x, y)`.
PDF cells finer than `kPdfMinDurableTileScale` (−2, 576 dpi) are generated
live and **not** stored. Interactive PDF `request_tile` replies with
**`codec=rgb888` raw pixels** (no JPEG). Durable store (scale ≥
`kPdfMinDurableTileScale`) still writes JPEG at `kPdfTileQuality` for the
next cache hit.

Raster images never use negative scale.

## Full-frame construct from tiles

When a host needs a **whole-image** raster at an arbitrary long edge (not a
single deep-zoom cell), and a grid pyramid is already warm:

```text
get_pixels_from_tiles(uri, max_edge)  // cache-only
```

1. Pick pyramid scale whose long edge still covers `max_edge` (else coarsest).
2. Require **every** tile at that scale (`get_tile`); incomplete → miss.
3. Composite to RGB, shrink to `max_edge`, encode JXL.
4. `PixelSource::TileSynth`.

`get_pixels` prefers this path when `allow_tile_synth` is true and a
complete scale exists. Soft is not durable; mid-edge UI (≤ `kBatchMaxEdge`
1024) should use TileSynth or tiles rather than a soft ladder.

See biltoo `docs/PIXEL_PIPELINE_REDESIGN.md` for batch vs focus lanes that
*build* the pyramid; this API only **reads** it.

## Archive backends (RAR / solid)

| Backend | Formats | Solid RAR | RAR5 |
|---------|---------|-----------|------|
| **libarchive** (default) | zip, 7z, rar, … | Often **unsupported** (`RAR solid archive support unavailable`) | Partial |
| **libunarr** (optional) | **RAR / CBR** focus | **Yes** (sequential walk) | **No** (upstream WIP) |

When built with `THUMTOO_HAVE_UNARR`, `.rar` / `.cbr` use unarr for TOC + extract
(`//archive:` URIs unchanged). Extract does **not** fall back to libarchive for
RAR — libarchive cannot extract solid RAR and only confuses RAR5 failures.
**RAR5:** libunarr cannot open it (spam on stderr if called). thumtoo probes the
magic and routes RAR5 to **libarchive** (TOC + extract for non-solid). Solid RAR5
remains limited. RAR4 solid stays on unarr. ZIP/CBZ stay on libarchive.

**Seek:** unarr can `ar_seek` the **file stream** and `ar_parse_entry_at` **headers**.
Solid **payloads** are not randomly seekable — extract walks members in order and
must uncompress intermediate solid members to keep the dictionary (see
`extract_archive_members_unarr`). Prefer sequential batch windows for CBR.
