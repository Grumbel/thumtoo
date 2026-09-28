# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `thumtoo-352.1-pdf-ocr-y-down` (base `fb6a408`).

### 352.1
- PDF OCR: page_y_up = false (MuPDF-aligned; was true and inverted boxes vs biltoo)
- Docs: PAGE_SPACE + text.hpp match native PDF extract

### Apply
```bash
git pull --ff-only …/thumtoo-352.1-pdf-ocr-y-down-fb6a408.bundle HEAD
```
