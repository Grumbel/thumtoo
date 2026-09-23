# TODO / agent handoff

## Status (2026-09-23)

**Tip: thumtoo-324-prepare-sizes-only-timing** (base `f71d183`).

`thumtoo-prepare` size-probe diagnostics for cold-cache triage (no biltoo):
- `--sizes-only` (default when no encode flags): wall_ms, ok/fail, probes/s
- Note sequential archives (RAR/CBR/tar) — one extract cursor; jobs do not fan out
- Print real worker count

### Apply
```bash
git pull --ff-only …/thumtoo-324.1-prepare-sizes-only-timing-f71d183.bundle HEAD
```

Next: **325**.

---

**Prior: thumtoo-323-try-exif-external-linkage** (`f71d183`).
