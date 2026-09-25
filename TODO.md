# TODO / agent handoff

## Status (2026-09-25)

**Tip: thumtoo-340.4-no-tile-overlap** (base `75b1f60`).

### 340.4 — remove 257 overlap
`kTileOverlap = 0`. Exclusive tile crops only. Re-prepare caches to drop any
257 payloads.

### Apply
```bash
git pull --ff-only …/thumtoo-340.4-no-tile-overlap-75b1f60.bundle HEAD
```
