<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO / agent handoff

## Status (2026-10-01)

**Tip:** thumtoo-027.1-smoke-ebook-classes (on `743dbf4` + agent stack).

### 027.1
- `bench_smoke.sh`: multi-class samples (photo, landscape, bookpage, comic,
  spread); `--no-large` corpus generate; falls back if names missing
- BENCHMARK_KIT corpus § aligned with pixel-bench-corpus content classes

### Prior
- 026.1 smoke photo path; 025.1 gp-tile; 024.1 mupdf stub; 023/022 plan+JSON

### Companion
**pixel-bench-corpus** tip: ebook/comic/landscape classes (bookpage, scan,
comic, spread, landscape) + portrait/landscape size sets.
Bundle: `pixel-bench-corpus-004.1-ebook-classes-*.bundle`

### Bundle policy
Work-line base: `743dbf4`. Full stack in each tip bundle.

### Next
- WebP/AVIF/JXL in gp-tile; gp-archive (libarchive vs unarr)
- flake `checks.bench-smoke` + optional corpus flake input
- Archive fixtures in corpus (ZIP/CBZ of book pages)
