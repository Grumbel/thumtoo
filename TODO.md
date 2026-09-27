# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `thumtoo-347.1-purge-text-layers` (base `5758b37`).

### 347.1 — purge_uri/path drops page_text_layer
- `Store::delete_page_text_layers` / `_for_blob`
- `Client::purge_uri` deletes native+OCR text for the page even when the
  document blob is shared; `purge_path` clears all text for the file blob.
- Enables biltoo Shift+F5 to re-extract text after coord fixes.

### Apply
```bash
git pull --ff-only …/thumtoo-347.1-purge-text-layers-5758b37.bundle HEAD
```
