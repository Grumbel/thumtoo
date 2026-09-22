# TODO / agent handoff

## Status (2026-09-22)

**Tip: thumtoo-320-activity-batch-warm-finish.**

Fix: batch cache-hit path for EnsureTiles/EnsurePixels now finishes
activity_id (was leaving soft/tile forever "queued").

Verified: `test_activity` ok.

### Apply
```bash
git pull --ff-only /path/to/thumtoo-320-activity-batch-warm-finish-8ea52ea.bundle HEAD
```

Next: **321**.
