# TODO / agent handoff

## Status (2026-09-23)

**Tip: thumtoo-325-sequential-size-one-pass** (base `f71d183`, includes 324).

### 325 — Sequential archive size batch: one solid pass
**Bug:** Coalesced ProbeSize extracted only a 32-member window; parallel
`handle_probe_size` without pre restarted solid RAR decompress per missing
member → ~0.86 probes/s (164 members ≈ 3 min).

**Fix:** Sequential + ProbeSize/EnsurePixels/EnsureLqip: extract **all**
interest in one TOC-ordered `extract_archive_members`. Serialize sequential
`member_bytes` disk opens.

### 324 — prepare sizes-only timing
`--sizes-only` wall_ms / ok / fail / probes_per_s.

### Apply
```bash
git pull --ff-only …/thumtoo-325.1-sequential-size-one-pass-f71d183.bundle HEAD
```

Next: **326**.

---

**Prior: thumtoo-323-try-exif-external-linkage** (`f71d183`).
