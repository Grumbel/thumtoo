# TODO / agent handoff

## Status (2026-09-25)

**Tip: thumtoo-340.6-export-tool** (base `75b1f60`).

### 340.6 — thumtoo-export
CLI: assemble all exclusive tiles at `--scale` into one PNG.
`--raw-pdf` uses region raster (no JPEG) for PDF page URIs.

```bash
thumtoo-export 'file:///path/doc.pdf//page:1' --scale 0 -o page.png
thumtoo-export 'file:///path/doc.pdf//page:1' --scale 0 --raw-pdf -o raw.png
```

### Apply
```bash
git pull --ff-only …/thumtoo-340.6-export-tool-75b1f60.bundle HEAD
```
