<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO / agent handoff

## Status (2026-10-01)

**Tip:** thumtoo-035.1-capture-baselines (on `743dbf4` + agent stack).

### 035.1
- `tools/capture_baselines.sh` — write machine-class JSON under docs/bench/baselines/$MACHINE
- `checks.baseline-compare-tool` — identity check of compare_bench_json on example/

### Prior
- 034 corpus-smoke; 033 smoke-lite + compare tool; 032 unarr; 031 avif/jxl

### Companion
https://github.com/Grumbel/benchtoo (pull tip through 009.3-no-pdf2djvu)

### Next
- Run capture_baselines on a real machine and commit under docs/bench/baselines/<host>/
- Optional CI job: capture vs committed baseline with compare_bench_json
- Push benchtoo 009.3 to GitHub
