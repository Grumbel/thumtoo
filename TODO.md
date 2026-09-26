# TODO / agent handoff

## Status (2026-09-26)

**Tip: thumtoo-341.8-stale-tile-no-loop** (base `241d2d3`).

### 341.8 — Stale tile reject without request storms
- `has_tile` uses the same w/h grid check as `get_tile` (stale row ≠ hit)
- `invalidate_tile` remains a Store no-op — do not rely on it
- export: at most one encode per cell; second size mismatch is fatal (no loop)
- `put_tile` ON CONFLICT replaces the row so one encode converges

### 341.7 — reject wrong-dimension Store tiles

### Apply
```bash
git pull --ff-only …/thumtoo-341.8-stale-tile-no-loop-241d2d3.bundle HEAD
```
