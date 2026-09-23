# TODO / agent handoff

## Status (2026-09-23)

**Tip: thumtoo-328-stream-sequential-size** (base `f71d183`, includes 324–327).

### 328 — Stream sequential size probes (no N-image RAM)
Holding all solid-RAR members in RAM for size probe swapped/stalled and got
slower deeper in the archive. `visit_archive_members`: one open, probe each
member, discard bytes immediately.

### Apply
```bash
git pull --ff-only …/thumtoo-328.1-stream-sequential-size-f71d183.bundle HEAD
```

Next: **329**.

---

**Prior: thumtoo-323-try-exif-external-linkage** (`f71d183`).
