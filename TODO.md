# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `thumtoo-353.2-mupdf-pc-version` (base `fb6a408`).

### 353.2
- Fix mupdf.pc Version (was stuck at 1.27.2 after src pin; store path was already 1.28.5)
- postFixup sed on `$dev`/`$out` pkgconfig

### 353.1
- Pin MuPDF 1.28.5 via pinMupdf / mkBuildInputs

### Verify
```bash
# re-enter shell / rebuild mupdf
pkg-config --modversion mupdf   # expect 1.28.5
pkg-config --libs mupdf         # store path mupdf-1.28.5
```

### Apply
```bash
git pull --ff-only …/thumtoo-353.2-mupdf-pc-version-fb6a408.bundle HEAD
```
