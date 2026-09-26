# TODO / agent handoff

## Status (2026-09-26)

**Tip: thumtoo-342.1-ocr-tesseract-text-layer** (base `241d2d3`).

### 342.1 — OCR text layers (Tesseract)
- `PageTextLayer`: `source` (Native|Ocr) + optional `OcrMeta` (engine/model/lang/dpi/params)
- Serialization TTL5 (TTL3/4 still read as Native)
- Dual Store slot: `ocr_store_layout_key(base, engine, model)`
- Optional Tesseract (`THUMTOO_HAVE_TESSERACT` / flake `tesseract`)
- `ocr_page_text_layer`, `Client::get_ocr_page_text_layer`, `ensure_ocr_page_text_layer`
- User-triggered only; no native-quality heuristic
- Defaults: eng, max_edge 3000, PSM_AUTO, line regions

### Next
- Batch OCR document API + biltoo progress
- Semantic tags (page number / header) for crop assist
- LLM engine slot (same OcrMeta shape)

### Apply
```bash
git pull --ff-only …/thumtoo-342.1-ocr-tesseract-text-layer-241d2d3.bundle HEAD
```
