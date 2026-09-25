# TODO / agent handoff

## Status (2026-09-25)

**Tip: thumtoo-340.1-tile-overlap-zero** (base `75b1f60`).

### 340.1 — kTileOverlap = 0
QPainter hosts (biltoo) assemble exclusive tiles then smooth-scale once; the
257 strip did not fix seams and caused scale/paint-order bugs. New encodes are
exclusive 256×256. Legacy 257 Store tiles remain readable.

### Prior on this base
- 339.2 cmake feature-summary align
- 339.1 Client `//pdfimage:N` size/soft/tiles
- 1px overlap experiment (f0c0055) — superseded by 340.1

### Apply
```bash
git pull --ff-only …/thumtoo-340.1-tile-overlap-zero-75b1f60.bundle HEAD
```
