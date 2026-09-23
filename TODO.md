# TODO / agent handoff

## Status (2026-09-23)

**Tip: thumtoo-330-local-archive-mirror** (base `f71d183`, includes 324–329).

### Diagnosis
Solid extract locally ~1–2s; NFS `cp` of the same RAR ~11s. Multi-minute
`thumtoo-prepare --sizes-only` on `/net/...` was **repeated remote reads**, not
JPEG decode and not unarr itself.

### 330 — Local archive mirror
`ensure_local_archive()` copies the RAR once under
`cache_root/archive_mirror/` (or `$TMPDIR/.../archive_mirror` for `--no-cache`).
Sequential extract/visit uses the local copy.

### Apply
```bash
git pull --ff-only …/thumtoo-330.1-local-archive-mirror-f71d183.bundle HEAD
```

Expect cold: ~copy time + ~2s extract + probe. Warm mirror: extract+probe only.

Next: **331** — THUMTOO_DEBUG_ARCHIVE traces; sizes-only skip member staging;
optional skip mirror when already local FS.
