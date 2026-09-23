# TODO / agent handoff

## Status (2026-09-23)

**Tip: thumtoo-333-debug-archive** (base `f71d183`, includes 324–332).

### THUMTOO_DEBUG_ARCHIVE=1
Stderr lines prefixed `thumtoo-archive:` for unarr/libarchive:
open, TOC, extract/visit per-member ms+bytes, solid-discard ≥50ms, totals.

Also on when `THUMTOO_DEBUG=1`. Header: `include/thumtoo/debug.hpp`.

```bash
THUMTOO_DEBUG_ARCHIVE=1 thumtoo-prepare --sizes-only --no-cache album.rar
```

### Apply
```bash
git pull --ff-only …/thumtoo-333.1-debug-archive-f71d183.bundle HEAD
```

Next: **334**.
