# TODO / agent handoff

## Status (2026-09-26)

**Tip: thumtoo-343.4-ocr-uniform-scale** (base `66fc03e`).

### 343.4 — OCR bbox scale drift fix
- Map Tess pixels with **uniform** long-edge scale (not independent sx/sy)
- Plain-image OCR: `page_bounds` = full upright image size (probe), not OCR raster

Re-OCR existing pages (force) to refresh cached layers.

### Apply
```bash
git pull --ff-only …/thumtoo-343.4-ocr-uniform-scale-66fc03e.bundle HEAD
```
