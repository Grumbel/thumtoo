# TODO / agent handoff

## Status (2026-09-25)

**Tip: thumtoo-341.4-pdf-fullpage-then-cut** (base `75b1f60`).

### 341.4 — PDF tiles: full-page raster, exclusive crop
Root cause of missing lines on PDF pages: independent per-cell MuPDF region
clips cull vector strokes whose centre sits on a 256 grid line. Pad/overlap
hacks paper over that; they are not used.

`mupdf_render_tile_cell` now rasterizes the **whole page level** once (TLS
cache per path/page/scale), then crops exclusive cells — same model as image
tiles. Region fallback only when `width*height > kTileMaxSourcePixels`.
`kTileOverlap` stays 0.

Re-export / invalidate old durable JPEG tiles encoded with region clips.

### 341.2 — thumtoo-export bare path + //page:
Preserve pipe suffix when normalizing path-like URIs.

### 341.1 — pdf_page_size_at_scale negative scales
- `s >= 0`: successive floor-half
- `s < 0`: exact `L * 2^{-s}`

### Apply
```bash
git pull --ff-only …/thumtoo-341.4-pdf-fullpage-then-cut-75b1f60.bundle HEAD
```
