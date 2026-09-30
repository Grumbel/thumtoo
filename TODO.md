# TODO / agent handoff

## Status (2026-09-30)

**Tip:** thumtoo-018.1-silent-tile-cancel (on `b825e8d` + agent stack).

### 018.1
- Cancelled EnsureTiles jobs no longer deliver miss callbacks. Hosts treated
  nullopt as terminal Failed → stuck LQIP after scroll cancel / supersede
  despite Store tiles present.

### Prior
- 017.1 interactive tile LIFO + cancel_tile_cells

### Bundle policy
Work-line base: `b825e8d`. Full stack in each tip bundle.
