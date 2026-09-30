# TODO / agent handoff

## Status (2026-09-30)

**Tip:** thumtoo-021.1-no-batch-tile-strip (on `b825e8d` + agent stack).

### 021.1
- `cancel_tile_cells` no longer compacts multi-cell `tile_batch` jobs. Stripping
  renumbered completion indices vs the host's original key list → **same-image
  wrong-spot tiles**. Single-cell scroll cancel unchanged.

### Prior
- 020.1 OCR unused ifdef
- 019.1 fz_style_document
- 018.1 silent tile cancel

### Bundle policy
Work-line base: `b825e8d`. Full stack in each tip bundle.
