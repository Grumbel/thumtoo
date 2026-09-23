# TODO / agent handoff

## Status (2026-09-23)

**Tip: thumtoo-336-tile-request-no-gui-sqlite** (base `f71d183`, includes 324–335).

### request_tile(s) no longer SQLite on the caller
Warm durable hits used `get_tile` on the **caller thread** then `executor_.post`.
Biltoo issues tiles from the GUI (`TileLoadCoordinator::tick`) → multi-second
GUI budgets after `prepare --tiles`.

**Fix:** Always enqueue EnsureTiles jobs; workers still short-circuit on
`get_tile` inside `handle_ensure_tiles`.

### Apply
```bash
git pull --ff-only …/thumtoo-336.1-tile-request-no-gui-sqlite-f71d183.bundle HEAD
```

Next: **337**.

## Prior — 335
prepare --tiles builds durable pyramids for PDF/DjVu/EPUB pages.

## Prior — 334
unarr close; avoid extra TOC opens on sizes-only RAR.
