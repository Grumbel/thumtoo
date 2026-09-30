# TODO / agent handoff

## Status (2026-09-30)

**Tip:** thumtoo-016.1-size-ensure-cleanup (on `b825e8d` + agent stack).

### 016.1
- `ensure_pdf_page_sizes` coalesce map is **Client-owned** (not process static).
- `request_size` on miss: ensure PDF page dims, then light reply via `get_size`
  before enqueueing ProbeSize — hosts need not special-case multipage.
- Region dim load (014) + light `get_size` (013) unchanged.

### Prior
- 015.1 ensure_pdf_page_sizes
- 014.1 region width/height load

### Bundle policy
Work-line base: `b825e8d`. Full stack in each tip bundle.

### Perf reference
Paired biltoo Gallery open baseline (~2400 PDF pages, settled Store, ~0.5 s TTFP): biltoo **docs/TTFP.md § Baseline 2026-09-30** — this tip `d6a341f` + biltoo `3907788`.
