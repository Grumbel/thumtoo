# TODO / agent handoff

## Status (2026-09-25)

**Tip: thumtoo-341.5-kill-overlap-layout-round** (base `75b1f60`).

### 341.5 — kill kTileOverlap; fix layout double-round
- Removed `kTileOverlap` and `THUMTOO_DEBUG_TILE_OVERLAP` entirely.
- Layout size is one `lround(page_pt * kPdfLayoutDpi/72)` from continuous
  `fz_bound_page` bounds — not `lround(pt)` then `×2` (up to 1px drift vs ctm).
- PDF cells still full-page raster + exclusive crop (works for scanned pages).

### Apply
```bash
git pull --ff-only …/thumtoo-341.5-kill-overlap-layout-round-75b1f60.bundle HEAD
```
