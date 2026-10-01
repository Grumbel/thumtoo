<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO / agent handoff

## Status (2026-10-01)

**Tip:** thumtoo-029.1-gp-archive-webp (on `743dbf4` + agent stack).

### 029.1
- `thumtoo-gp-archive`: golden libarchive TOC / sequential / first / last /
  scattered extract (`--json`)
- `thumtoo-gp-tile --codec jpeg|webp`
- CMake + flake apps; bench_smoke runs gp-archive when CBZ present

### Companion
pixel-bench-corpus 007+: labeled rasters, PDF book, CBZ, MD/TXT, archives

### Bundle policy
Work-line base: `743dbf4`. Full stack in each tip bundle.

### Next
- AVIF/JXL tile codecs; unarr path in gp-archive when linked
- flake `checks.bench-smoke`; pdf2djvu optional in corpus flake
