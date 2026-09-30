# TODO / agent handoff

## Status (2026-09-30)

**Tip:** thumtoo-017.1-interactive-tile-lifo (on `b825e8d` + agent stack).

### 017.1
- Interactive `request_tile` / `request_tiles` enqueue **LIFO** (front) so the
  live viewport outruns tiles already scrolled past.
- `cancel_tile_cells(uri, cells)` drops matching queued single-cell jobs and
  strips cells from batch jobs (scroll cancel without `cancel_uri` of the path).

### Prior
- 016.2 TTFP baseline note
- 016.1 size-ensure cleanup

### Bundle policy
Work-line base: `b825e8d`. Full stack in each tip bundle.
