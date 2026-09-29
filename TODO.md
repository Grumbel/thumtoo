# TODO / agent handoff

## Status (2026-09-29)

**Tip:** thumtoo-007-materialize-tile-cell (on 006 stack).

### 007
- `materialize_tile_cell` is the sole interactive cell path (store hit or encode)
- `request_tiles` → one batch job; walks coords via materialize (probe once)
- `decode_tile_blob_to_rgb888` on the worker before host callback (no host JPEG decode)

### 006
- request_tiles one batch job (superseded structure in 007)

### 005
- Do not archive-coalesce interactive EnsureTiles
