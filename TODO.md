# TODO / agent handoff

## Status (2026-09-25)

**Tip: thumtoo-340.3-encode-overlap** (base `75b1f60`).

### 340.3 — kTileOverlap=1 again (encode only)
Exclusive PDF region clips were dropping H/V hairlines on the 256 grid.
Restore +1 right/bottom in the **encoded** payload. Hosts paint exclusive
subrect only (no 257→256 dest scale).

### Also
- 340.2 tests data_root=cache
- 340.1 had set overlap to 0 — superseded for encode

### Apply
```bash
git pull --ff-only …/thumtoo-340.3-encode-overlap-75b1f60.bundle HEAD
```
