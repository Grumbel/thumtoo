# TODO / agent handoff

## Status (2026-09-26)

**Tip: thumtoo-342.2-ocr-region-kinds** (base `241d2d3`).

### 342.2 — Region kinds (page number / header / footer)
- `TextRegionKind`: Body | PageNumber | Header | Footer
- TTL6 serialization (+kind); TTL3–5 still load
- OCR post-pass `annotate_region_kinds` (geometry + numeric text heuristics)

### 342.1 — Tesseract OCR dual-slot layers

### Apply
```bash
git pull --ff-only …/thumtoo-342.2-ocr-region-kinds-241d2d3.bundle HEAD
```
