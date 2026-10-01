<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO / agent handoff

## Status (2026-10-01)

**Tip:** thumtoo-033.1-smoke-lite-baselines (on `743dbf4` + agent stack).

### 033.1
- flake `inputs.benchtoo` (github:Grumbel/benchtoo)
- `checks.bench-smoke-lite` — microbench-decode + gp-tile + gp-archive on corpus
- `tools/compare_bench_json.py` + `docs/bench/baselines/README.md`
- `docs/bench/RAR_FIXTURES.md` — how to produce RAR4 for unarr A/B

### Prior
- 032 gp-archive unarr; 031 AVIF/JXL; 030 benchtoo rename

### Companion
https://github.com/Grumbel/benchtoo

### Next
- Capture first machine-class baselines under docs/bench/baselines/
- Optional rar generator shell-out in benchtoo when `rar` present
- Tighten smoke-lite (smaller corpus attr / --no-large in benchtoo package)
