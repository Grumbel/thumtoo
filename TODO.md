# TODO / agent handoff

## Status (2026-09-25)

**Tip: thumtoo-340.5-pdf-level-floor-half** (base `75b1f60`).

### 340.5 — PDF level size = successive floor-half
`pdf_page_size_at_scale` used `lround(layout*2^-s)`, which disagreed with
`dim_at_tile_scale` on some sizes → wrong right/bottom edge tile crops vs host
grid (missing edge lines). Now uses `dim_at_tile_scale` only.

### 340.4 — kTileOverlap=0

### Apply
```bash
git pull --ff-only …/thumtoo-340.5-pdf-level-floor-half-75b1f60.bundle HEAD
```
