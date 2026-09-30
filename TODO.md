# TODO / agent handoff

## Status (2026-09-30)

**Tip:** thumtoo-010.2-tile-supersede-activity-tests (on 010.1 stack).

### 010.2
- Document ActivityLedger + supersede invariant in TILES.md
- Tests:
  - `test_activity`: finish Queued without Running clears tile_queued
  - `test_tile_supersede_activity`: Client flood same cell does not leave
    tile_queued == N (regression for host status=Working tile=N/0)

### 010.1
- Single-cell EnsureTiles supersede calls `reply_cancelled_job`

### Apply
```bash
git pull /path/to/thumtoo-010.2-tile-supersede-activity-tests-<base>.bundle HEAD
```
