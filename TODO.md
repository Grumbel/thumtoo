# TODO / agent handoff

## Status (2026-09-29)

**Tip:** thumtoo-009.2-tile-size-stretch (on 008.1 stack).

### 009.2
- `get_tile` / `has_tile`: do **not** reject rows whose w/h ≠ `dim_at_tile_scale`
  grid (JPEG DCT vs floor-half drift). Hosts stretch the bitmap into the cell.
- Removes miss storms that left interactive cells Failed while LQIP stayed.

### 009.1
- Interactive JPEG DCT path: rgb888 after remain shrinks; ladder fallthrough

### 008.1
- Serialize MuPDF Markdown opens (cmark not reentrant)

### 007
- materialize_tile_cell / batch request_tiles / worker rgb888
