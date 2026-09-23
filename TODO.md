# TODO / agent handoff

## Status (2026-09-23)

**Tip: thumtoo-327-bulk-enqueue-no-sleep** (base `f71d183`, includes 324–326).

### 327 — Bulk size enqueue + drop worker sleep
prepare_paths enqueued one-by-one while workers raced → coalesce saw ~12 jobs,
solid RAR re-opened each wave. Fix: `enqueue_jobs` + notify_all; no 50ms
wait_for; drain on cv (not sleep 5ms).

### Apply
```bash
git pull --ff-only …/thumtoo-327.1-bulk-enqueue-no-sleep-f71d183.bundle HEAD
```

Next: **328**.

---

**Prior: thumtoo-323-try-exif-external-linkage** (`f71d183`).
