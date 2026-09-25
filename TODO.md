# TODO / agent handoff

## Status (2026-09-25)

**Tip: thumtoo-340.2-test-data-root** (base `75b1f60`).

### 340.2 — tests pass data_root = cache
After user.sqlite moved to `default_data_root()` (XDG_STATE_HOME), tests that
called `Client::open(cache)` tried to create `~/.local/state/thumtoo` and
aborted with `create data_root: Permission denied` in restricted environments.
All unit tests now pass `data_root = cache` (4th arg).

### 340.1 — kTileOverlap = 0
Exclusive 256 tiles; biltoo assemble-then-smooth.

### Apply
```bash
git pull --ff-only …/thumtoo-340.2-test-data-root-75b1f60.bundle HEAD
```
