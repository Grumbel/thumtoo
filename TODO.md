# TODO / agent handoff

## Status (2026-10-05)

**Tip:** thumtoo-040.1-image-heavy-denser (linear on prior tip).

### 040.1 — Image-heavy PDF denser live tiles
Thumtoo previously returned nullopt for **every** `scale < 0` cell on
image-heavy pages (XObject coverage ≥ 0.45, typical scans). Viewers (biltoo)
saw `FAILED N/N live denser` while scale 0 still worked.

**Fix:** denser for image-heavy pages is allowed when the page level fits the
full-page raster + exclusive crop path (`page_pixels ≤ kTileMaxSourcePixels`).
Per-cell region draws remain forbidden for image-heavy denser (grid seams).

`pdf_page_allows_live_tiles` now returns true for image-heavy pages when the
densest durable level (−2) still fits that path.

### Bundle policy
Deliver cumulative `.bundle` from the agreed base; no parallel histories.
