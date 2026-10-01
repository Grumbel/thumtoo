<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO / agent handoff

## Status (2026-10-01)

**Tip:** thumtoo-022.1-benchmark-kit-plan (on `b825e8d` + agent stack).

### 022.1
- Plan only: [docs/BENCHMARK_KIT.md](docs/BENCHMARK_KIT.md) — benchmark kit for
  codec matrix, archive random/sequential, JPEG shrink vs full, tiles vs
  full-frame, libarchive vs unarr, stage time-to-pixels, RGB vs RGBA, golden
  paths outside Client, separate corpus flake. No code yet.

### Prior
- 021.1 no-batch-tile-strip
- 020.1 OCR unused ifdef
- 019.1 fz_style_document
- 018.1 silent tile cancel

### Bundle policy
Work-line base: `b825e8d`. Full stack in each tip bundle.

### Next (after plan review)
- P1: `pixel-bench-corpus` flake skeleton + synthetic JPEG generators
- P2: golden `gp-decode` / `gp-tile` + JSON schema
- Extend `thumtoo-bench --json`; refresh MICROBENCH_RESULTS under nix/vips
