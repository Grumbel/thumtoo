# TODO / agent handoff

## Status (2026-09-26)

**Tip: thumtoo-344.1-prefercache-kick-tiles** (base `aeb5159`).

### 344.1 — PreferCache/Overview kick tile pyramid on TileSynth miss
Kill Soft Phase D: `request_raster(PreferCache|Overview)` tries TileSynth first;
on miss schedules `request_tile_pyramid` then one-shot ephemeral reply.
SoftOnly unchanged (no TileSynth, no tile kick).

### Apply
```bash
git pull --ff-only …/thumtoo-344.1-prefercache-kick-tiles-aeb5159.bundle HEAD
```

## Prior
### 343.6 — OCR rasterize for EPUB //page: URIs
