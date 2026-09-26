# TODO / agent handoff

## Status (2026-09-26)

**Tip: thumtoo-344.2-sizereply-embedded** (base `aeb5159`).

### 344.2 — SizeReply warm path includes EMB
`request_size` / `prepare_paths` cache hits only set `reply.lqip = get_lqip()`,
which **skips EmbeddedJpeg**. PDF/EXIF thumbs never arrived with size on hot
cache. Now also `reply.embedded = get_embedded_preview(uri)` (same as
`handle_probe_size` `reply_size`).

### Apply
```bash
git pull --ff-only …/thumtoo-344.2-sizereply-embedded-aeb5159.bundle HEAD
```

## Prior
344.1 PreferCache kicks tile pyramid on TileSynth miss
