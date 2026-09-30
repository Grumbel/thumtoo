# TODO / agent handoff

## Status (2026-09-30)

**Tip:** thumtoo-015.1-ensure-pdf-page-sizes (on `b825e8d` + agent stack).

### 015.1
- `Client::ensure_pdf_page_sizes(path)`: one open, write region dims for every
  page missing size (no thumbs/tiles). Concurrent same-path callers coalesce.
- Fixes size-gate tail: first miss no longer serial-probes each remaining page.

### Prior
- 014.1 region width/height load
- 013.1 get_size light

### Bundle policy
Work-line base: `b825e8d`. Full stack in each tip bundle.
