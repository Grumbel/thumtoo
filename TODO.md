# TODO / agent handoff

## Status (2026-09-25)

**Tip: thumtoo-339.1-pdfimage-client** (base `75b1f60`).

### This tip
- Wire `//pdfimage:N` through Client Store path (was list-only).
- ProbeSize → `pdf_embedded_image_size`
- EnsurePixels → `pdf_rasterize_embedded_image` + ladder
- EnsureTiles → RGB extract + cell/pyramid
- meta / content_id `:pdfimage:N` + Fragment region; tile target + put_tiles

### Prior
- TextRegion.block_id (TTL4) on `75b1f60`
- XDG thumbnails verified (338.3)

### Apply
```bash
git pull --ff-only …/thumtoo-339.1-pdfimage-client-75b1f60.bundle HEAD
```
