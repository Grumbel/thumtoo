# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `thumtoo-353.1-mupdf-1.28.5` (base `fb6a408`).

### 353.1
- Pin MuPDF **1.28.5** via `pinMupdf` / `mkBuildInputs` (nixpkgs still 1.27.2)
- Clears nixpkgs 1.27 patches (may need refresh if build fails)
- Goal: Markdown document support for biltoo `.md` path (see biltoo `docs/TXT_MD_SUPPORT.md`)

### Verify
```bash
nix build .#default -L   # or thumtoo-configure && thumtoo-build
# pkg-config --modversion mupdf  → 1.28.5 in the build env
# PDF/EPUB still rasterize
```

### Next (if build OK)
- thumtoo PathKind / open for `.md` (and decide `.txt`)
- biltoo file filters once thumtoo accepts the format

### Apply
```bash
git pull --ff-only …/thumtoo-353.1-mupdf-1.28.5-fb6a408.bundle HEAD
```
