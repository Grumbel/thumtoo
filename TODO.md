# TODO / agent handoff

## Status (2026-09-29)

**Tip:** thumtoo-003 on origin via 002 (vips=1, workers≤8).

### 003
- vips_concurrency_set(1) again (avoid workers×hw thread storm)
- default worker_threads capped at 8 when 0 is passed

### Stack
551a360 → 001 PreferCache → 002 → 003

