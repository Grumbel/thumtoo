<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO / agent handoff

## Status (2026-10-01)

**Tip:** thumtoo-031.1-avif-jxl-check (on `743dbf4` + agent stack).

### 031.1
- `gp-tile --codec jpeg|webp|avif|jxl` (AVIF via HEIF/AV1; needs libheif/libjxl in vips)
- flake apps: `gp-tile`, `gp-archive`
- flake check `bench-smoke-script` (script presence + benchtoo/golden refs)

### Prior
- 030 benchtoo rename/URL; 029 gp-archive + WebP; 028 documents smoke

### Companion
https://github.com/Grumbel/benchtoo

### Bundle policy
Work-line base: `743dbf4`. Full stack in each tip bundle.

### Next
- unarr path in gp-archive when linked
- full `checks.bench-smoke` with optional benchtoo flake input
- pdf2djvu optional in benchtoo flake
