# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `thumtoo-345.2-ocr-source-dpi` (base `aeb5159`).

### 345.2 — Tesseract source DPI
- `OcrOptions::dpi` (0 = auto)
- `SetSourceResolution` before Recognize (no more silent ~70 DPI)
- Auto: document page box → `72×pix/bounds`; pixel page box → 300
- See docs/PAGE_SPACE.md (OCR source DPI)

### 345.1 — page_y_up (TTL7)
Document Y-up for PDF/DjVu/EPUB native+OCR; plain images Y-down.

### Apply
```bash
git pull --ff-only …/thumtoo-345.2-ocr-source-dpi-aeb5159.bundle HEAD
```

## Prior
344.4 region page size
