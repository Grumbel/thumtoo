# TODO / agent handoff

## Status (2026-09-30)

**Tip:** thumtoo-014.1-region-size-load (on `b825e8d` + agent stack).

### 014.1
- **Root cause:** `Store::find_region` SELECTed width/height but never assigned
  them → region sizes always looked missing → hosts re-probed / opened PDFs.
- `find_region` + `find_region_by_key` now load width/height (by-key is one query).
- 013.1 light `get_size` remains (no list_tile_scales / no source open).

### Prior
- 013.1 get_size light
- 012.1 activity orphan drop

### Bundle policy
Work-line base: `b825e8d`. Full stack in each tip bundle.
