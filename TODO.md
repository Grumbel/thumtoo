# TODO / agent handoff

## Status (2026-09-29)

**Tip:** thumtoo-001-prefercache-no-focusfull (linear on origin 551a360).

### Stack (single bundle)
1. PreferCache miss must not request_tile_pyramid (e4db5b8 regression)
2. FocusFull cap: wait instead of busy-rotate; no archive pyramid coalesce

### Apply
```bash
git pull --ff-only …/thumtoo-001-prefercache-no-focusfull-551a360.bundle HEAD
```

