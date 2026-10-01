<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO / agent handoff

## Status (2026-10-01)

**Tip:** thumtoo-023.1-bench-json-microdecode (on `743dbf4` + agent stack).

### 023.1
- `thumtoo-bench`: `--json`, `--keep-cache`
- `thumtoo-microbench-decode`: `--json` (golden-path vips; no Client)
- flake app `micro-decode`
- Plan remains [docs/BENCHMARK_KIT.md](docs/BENCHMARK_KIT.md)
- Companion corpus flake: **pixel-bench-corpus** (separate bundle)

### 022.1
- Plan only: docs/BENCHMARK_KIT.md

### Prior
- 021.1 no-batch-tile-strip
- 020.1 OCR unused ifdef
- 019.1 fz_style_document
- 018.1 silent tile cancel

### Bundle policy
Work-line base for this session stack: `743dbf4` (origin/master at plan start).
Full stack in each tip bundle from that base.

### Next
- Wire thumtoo flake input to pixel-bench-corpus once published
- `gp-tile` codec matrix; `gp-archive` libarchive/unarr
- `checks.bench-smoke` on synthetic 2 MP JPEG
- Refresh MICROBENCH_RESULTS under nix/vips with corpus
