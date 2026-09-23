# TODO / agent handoff

## Status (2026-09-23)

**Tip: thumtoo-332-mirror-opt-in** (base `f71d183`, includes 324–331).

### Sizes-only performance (resolved for current RAR class)
| Case | Wall |
|------|------|
| Local | ~4.2s |
| Network cold | ~17s |
| Network warm | ~5s |

Root fix was 331: `preextracted` was discarded; every size probe re-fetched members.

### 332 — Archive mirror opt-in only
`ensure_local_archive` is a no-op unless `THUMTOO_MIRROR_ARCHIVES=1`.
No automatic 700MB copies.

### Apply
```bash
git pull --ff-only …/thumtoo-332.1-mirror-opt-in-f71d183.bundle HEAD
```

### Deferred
- Network-drive detect + mirror + GC
- THUMTOO_DEBUG_ARCHIVE traces
- Pathological solid-archive microbench

### ECS?
thumtoo is not a GUI session: Store is durable ground truth; Client is the
job/scheduler + process caches. No biltoo-style ECS refactor needed. Remaining
cleanup is pipeline (extract → probe → tiles), not entity components.
