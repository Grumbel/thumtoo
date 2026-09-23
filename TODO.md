# TODO / agent handoff

## Status (2026-09-23)

**Tip: thumtoo-335-prepare-doc-tile-pyramid** (base `f71d183`, includes 324–334).

### prepare --tiles for PDF/DjVu/EPUB
`request_tile_pyramid` only baked file:// + archive members. Document page
URIs (`//page:N`) always got `reply_pyramid_done(false)` → prepare reported
`miss` / `tiles=0` even after successful size probes.

**Fix:** Pyramid path now region-renders PDF (`pdf_build_tile_cell`), DjVu
(`djvu_build_tile_cell`), and EPUB (`epub_render_tile_cell` + JPEG) for each
scale/cell, stores durable JPEG (floor `kPdfMinDurableTileScale` = -2),
opportunistic LQIP unchanged.

`thumtoo-prepare --min-scale` may be negative down to -2 (document durable
floor). Help text documents PDF/DjVu/EPUB expand + tile support.

```bash
thumtoo-prepare --tiles /tmp/Mondo.2000.Issue.01.1989_text.pdf
# expect: phase 1 probes ok; tile phase ready (not miss); tiles>0 in summary
```

### Apply
```bash
git pull --ff-only …/thumtoo-335.1-prepare-doc-tile-pyramid-f71d183.bundle HEAD
```

Next: **336**.

## Prior — 334
unarr close log; ensure_archive_cursor + order_uris avoid extra TOC opens.
