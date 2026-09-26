# TODO / agent handoff

## Status (2026-09-26)

**Tip: thumtoo-344.4-region-page-size** (base `aeb5159`).

### 344.4 — One size: document page layout on region (no tile reconstruction)
**Wrong (344.3):** invent size from tile bounding boxes → arbitrary dims on
incomplete pyramids.

**Right:** the only size is the document page layout (PDF/DjVu/EPUB).
- Store it on `region.width` / `region.height` at ProbeSize
- `meta_from_store` reads region size; else layout probe
- Never shared Document media size; never tile-derived size

### Apply
```bash
git pull --ff-only …/thumtoo-344.4-region-page-size-aeb5159.bundle HEAD
```
Existing caches: re-probe (open book / size pass) fills region sizes once.

## Prior
344.3 tile-reconstruct (superseded); 344.2 SizeReply EMB; 344.1 PreferCache
