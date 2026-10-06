# TODO / agent handoff

## Status (2026-10-07)

**Tip:** `request_tile_cells` — exactly-once per-cell tile results (Claude Code,
direct commit on master).

### request_tile_cells
Interactive cells now answer exactly once each (Ok / Cancelled / Failed /
Unavailable + reason), streamed per cell, exempt from interest-epoch purges,
cancellable inside batches. Contract: TILES.md "Interactive cell contract";
test `tests/test_tile_cells_contract.cpp`. Legacy `request_tile(s)` unchanged.
biltoo's tile loader (docs/TILE_STATE_MACHINE.md) requires this API.

### Earlier: 040.2

### 040.2 — Smooth image scaling for denser PDF tiles
`set_smooth_image_scaling(bool)` / `smooth_image_scaling()` process-wide.
MuPDF `fz_tune_image_scale` uses Mitchell for upscales when smooth (default
on). Page-level TLS key includes S/N so toggles do not reuse nearest buffers.

Hosts (biltoo View → Smooth Scaling) should call `set_smooth_image_scaling`
and invalidate live denser tile RAM.

### 040.1 — Image-heavy denser via full-page crop
(see prior commit)

### Bundle policy
Cumulative `.bundle`; no parallel histories.
