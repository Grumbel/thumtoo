# TODO / agent handoff

## Status (2026-09-30)

**Tip:** thumtoo-020.1-ocr-unused-ifdef (on `b825e8d` + agent stack).

### 020.1
- Gate `clear_ocr_error` and `apply_ocr_page_crop` behind
  `THUMTOO_HAVE_TESSERACT` (unused-function warnings when OCR is OFF).

### Prior
- 019.1 fz_style_document
- 018.1 silent tile cancel

### Bundle policy
Work-line base: `b825e8d`. Full stack in each tip bundle.
