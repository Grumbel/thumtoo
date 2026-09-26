# TODO / agent handoff

## Status (2026-09-26)

**Tip: thumtoo-341.7-stale-tile-size-reject** (base `241d2d3`).

### 341.7 — Stale PDF tiles vs current layout size
Export assembled scale-0 on native 822×1292 while Store tiles matched an
older ~794×1248 grid (edge cells 26×224, missing y=5). White right/bottom
strip = uncovered canvas.

Fix:
- `Client::get_tile`: if stored cell w/h ≠ `tile_cell_pixel_rect` for current
  media size → miss (forces re-encode)
- `thumtoo-export`: on decode size mismatch, invalidate + re-request once

### Prior
341.5–341.6 layout round, kTileOverlap kill, full-page crop default.

### Apply
```bash
git pull --ff-only …/thumtoo-341.7-stale-tile-size-reject-241d2d3.bundle HEAD
```
