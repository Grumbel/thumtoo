# TODO / agent handoff

## Status (2026-09-26)

**Tip: thumtoo-343.6-ocr-epub-pages** (base `66fc03e`).

### 343.6 — OCR rasterize for EPUB //page: URIs
Previously `path_from_file_uri` stripped pipes → “multipage without page pipe”.
Errors now include the URI/path.

### Apply
```bash
git pull --ff-only …/thumtoo-343.6-ocr-epub-pages-66fc03e.bundle HEAD
```
