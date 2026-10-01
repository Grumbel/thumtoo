<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO / agent handoff

## Status (2026-10-01)

**Tip:** thumtoo-025.1-gp-tile-rgb-smoke (on `743dbf4` + agent stack).

### 025.1
- `thumtoo-gp-tile`: golden 256² JPEG quality encode/decode matrix (vips only)
- `tools/bench_smoke.sh`: offline smoke (corpus + decode + gp-tile + bench)
- CMake/flake install + app `gp-tile`
- Companion corpus: RGB synthetic (3-band) — see pixel-bench-corpus tip

### 024.1
- MuPDF stub; verified microbench + thumtoo-bench JSON

### 023.1 / 022.1
- JSON benches; BENCHMARK_KIT plan

### Bundle policy
Work-line base: `743dbf4`. Full stack in each tip bundle.

### Next
- WebP/AVIF/JXL in gp-tile; gp-archive; flake checks.bench-smoke + corpus input
- Richer synthetic (ramps/noise) for compression stress
