# TODO / agent handoff

## Status (2026-09-23)

**Tip: thumtoo-329-extract-staging** (base `f71d183`, includes 324–328).

### 329 — Disk extract staging for sequential archives
Solid RAR decompress is too expensive to repeat. Members are written under
`cache_root/extract_staging/` (or `$TMPDIR/thumtoo-extract-$PID` for
`--no-cache`). `member_bytes` checks staging before opening the archive again.
Size stream path stages each member as it probes.

Header-only size without decompress is not available for solid RAR (stream
depends on prior members). Unpacked dirs stay ~0.5s; RAR pays extract once.

### Apply
```bash
git pull --ff-only …/thumtoo-329.1-extract-staging-f71d183.bundle HEAD
```

Next: **330** — optional: probe from staged path without re-reading full bytes;
GC for extract_staging; prepare --tiles reuse staging.

---

**Prior tips:** 324 sizes-only, 325 one-pass, 326 :memory:, 327 bulk enqueue,
328 stream size (no N-image RAM).
