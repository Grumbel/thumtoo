# TODO / agent handoff

## Status (2026-09-23)

**Tip: thumtoo-331-use-preextracted-size** (base `f71d183`, includes 324–330).

### Bug
`handle_probe_size` discarded `preextracted` (`(void)preextracted`) and always
called `member_bytes` again. Sequential size visit extracted each member, then
re-read staging (or worse) for every probe. Local RAR sizes-only ~46s wall /
~44s user for 164 members.

### Fix
Wire preextracted into `handle_probe_size_store` archive path. Size visit puts
extract_cache only (no full-album staging writes during sizes-only).

### Goal
~15s sizes-only on local 716MB solid RAR (extract ~1–2s + probe/hash).

### Apply
```bash
git pull --ff-only …/thumtoo-331.1-use-preextracted-size-f71d183.bundle HEAD
```

Next: **332** — network-drive detect for mirror; staging GC; THUMTOO_DEBUG_ARCHIVE.
Defer until sizes-only is near the 15s goal.
