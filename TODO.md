# TODO / agent handoff

## Status (2026-09-25)

**Tip: thumtoo-341.6-defer-fullpage-bench-note** (base `75b1f60`).

### 341.5 — kill kTileOverlap; fix layout double-round
- Removed `kTileOverlap` and `THUMTOO_DEBUG_TILE_OVERLAP` entirely.
- Layout size is one `lround(page_pt * kPdfLayoutDpi/72)` from continuous
  `fz_bound_page` bounds — not `lround(pt)` then `×2` (up to 1px drift vs ctm).
- PDF cells: full-page raster + exclusive crop when under
  `kTileMaxSourcePixels` (TLS page-level cache); region fallback otherwise.

### Deferred — full-page PDF cost / design
Full-page-then-cut is already the default under the pixel guard. Treating it
as a deliberate perf/quality design (vs per-cell region) needs:

1. **Benchmark kit** — wall/cpu for single-cell, N-cell visible set, and
   full pyramid prepare; compare region-only vs full-page+crop vs prepare
   one-shot full level.
2. **Test data** — scanned page PDFs (the missing-line case), text/vector
   pages, large media boxes; fixed URIs in fixtures or a documented corpus.
3. **Decision** — keep full-page default, region-only, or hybrid (e.g. prepare
   full-page, interactive region) based on numbers — not anecdotes.

Do not expand this path further until (1)+(2) exist.

### Apply
```bash
git pull --ff-only …/thumtoo-341.6-defer-fullpage-bench-note-75b1f60.bundle HEAD
```
