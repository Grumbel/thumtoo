# TODO / agent handoff

## Status (2026-09-30)

**Tip:** thumtoo-010.1-tile-supersede-activity-finish (on 009.2 stack).

### 010.1
- Single-cell EnsureTiles supersede in `enqueue` now calls `reply_cancelled_job`
  (finishes ActivityLedger + miss callback). Previously only posted nullopt and
  left `tile_queued` stuck → biltoo Performance badge stayed **Working** with
  `tile=N/0` while thumtoo pending/inflight were 0.

### Prior: 009.2
- `get_tile` / `has_tile`: accept size-drift cells (host stretches at paint)

### Apply
```bash
git pull /path/to/thumtoo-010.1-tile-supersede-activity-finish-<base>.bundle HEAD
```
