# TODO / agent handoff

## Status (2026-09-29)

**Tip:** thumtoo-009.1-jpeg-dct-rgb-fallback (on 008.1 stack).

### 009.1
- Interactive JPEG DCT path: extract rgb888 after remain shrinks (no
  JPEG encode→decode round-trip before worker rgb888 delivery)
- On DCT+remain failure, fall through to full ladder instead of returning
  nullopt (fixed Gallery max-scale / s=5 ERROR while finer scales worked)

### 008.1
- Serialize MuPDF open/load for Markdown paths (cmark not reentrant)

### 007
- materialize_tile_cell / batch request_tiles / worker rgb888
