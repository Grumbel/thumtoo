# TODO / agent handoff

## Status (2026-09-26)

**Tip: thumtoo-343.1-ocr-rasterize-ifdef** (base `66fc03e`).

### 343.1 — Gate OCR rasterize helper on Tesseract
- `rasterize_uri_for_ocr` only compiled when `THUMTOO_HAVE_TESSERACT` (fixes -Wunused-function without Tesseract)

### 342.2 — Region kinds (page number / header / footer)

### Apply
```bash
git pull --ff-only …/thumtoo-343.1-ocr-rasterize-ifdef-66fc03e.bundle HEAD
```
