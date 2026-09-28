# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `thumtoo-348.1-leptonica-pkg-config` (base `53e62cd`).

### 348.1 — silence tesseract→lept pkg-config spam
- `flake.nix` mkBuildInputs: add `pkgs.leptonica` (tesseract.pc Requires: lept)
- CMake warning mentions leptonica when OCR is disabled

### Prior
- 347.1: purge_uri/path drops page_text_layer

### Apply
```bash
git pull --ff-only …/thumtoo-348.1-leptonica-pkg-config-53e62cd.bundle HEAD
```
