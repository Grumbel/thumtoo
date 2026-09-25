# TODO / agent handoff

## Status (2026-09-25)

**Tip: thumtoo-341.3-pdf-tile-edge-overscan** (base `75b1f60`).

### 341.3 — PDF tile exclusive cell 1px overscan
Missing ink on tile boundaries in thumtoo-export (and host) for PDF pages:
each exclusive cell was region-rasterized in isolation, so glyph AA that
straddles the edge was clipped.

`mupdf_render_tile_cell` now expands the MuPDF region by 1px (clamped to the
level), rasterizes, then crops back to the exclusive payload. `kTileOverlap`
stays 0. `--raw-pdf` export uses the same path.

**Note:** existing durable JPEG tiles were encoded without overscan — purge
or re-prepare affected PDF tile rows to pick up the fix.

### 341.2 — thumtoo-export bare path + //page:
Preserve `//page:` when normalizing path-like URIs (no `lexically_normal` collapse).

### 341.1 — pdf_page_size_at_scale negative scales
- `s >= 0`: successive floor-half
- `s < 0`: exact `L * 2^{-s}`

### Apply
```bash
git pull --ff-only …/thumtoo-341.3-pdf-tile-edge-overscan-75b1f60.bundle HEAD
```
