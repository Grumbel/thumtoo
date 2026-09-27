# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `thumtoo-346.1-mupdf-page-y-down` (base `e87420a`).

### 346.1 — MuPDF page/stext is Y-down
- PDF + EPUB native text layers: `page_y_up = false` (MuPDF top-left space).
- OCR for PDF/EPUB matches (`page_y_up = false`). DjVu stays Y-up.
- docs/PAGE_SPACE.md corrected (PDF *file* vs MuPDF *API* space).
- Hosts must prefer `layer.page_y_up`. Re-extract cached PDF text layers
  written with the old `true` flag.

### Apply
```bash
git pull --ff-only …/thumtoo-346.1-mupdf-page-y-down-e87420a.bundle HEAD
```
