# TODO / agent handoff

## Status (2026-09-26)

**Tip: thumtoo-344.3-page-size-from-tiles** (base `aeb5159`; on origin after 344.2).

### 344.3 — Per-page size for multipage docs (not shared media size)
`meta_from_store` for PDF/DjVu/EPUB used Document media width×height (last
probe wins). Mixed portrait/landscape → wrong tile grid → out-of-bounds cells.

Size from **page region tiles** when present, else page layout probe.

### 344.2 — SizeReply warm path includes EMB
`request_size` / `prepare_paths` cache hits include `reply.embedded`.

### Apply
```bash
git pull --ff-only …/thumtoo-344.3-page-size-from-tiles-aeb5159.bundle HEAD
```

## Prior
344.1 PreferCache kicks tile pyramid on TileSynth miss
