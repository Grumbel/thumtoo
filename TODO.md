# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `thumtoo-345.1-page-y-up` (base `aeb5159`).

### 345.1 — Page space Y axis is explicit
- `PageTextLayer::page_y_up` (TTL7 wire format)
- OCR maps Tesseract boxes into Y-up page space for PDF/DjVu/EPUB (same as native)
- Plain-image OCR stays Y-down
- Crop-before-OCR respects Y-up when slicing the raster
- See [docs/PAGE_SPACE.md](docs/PAGE_SPACE.md)

### Apply
```bash
git pull --ff-only …/thumtoo-345.1-page-y-up-aeb5159.bundle HEAD
```
Existing OCR cache entries: re-OCR to get Y-up document layers; native extract is unchanged.

## Prior
344.4 region page size; 344.3 tile-reconstruct (superseded); 344.2 SizeReply EMB
