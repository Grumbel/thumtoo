<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO / agent handoff

## Status (2026-10-01)

**Tip:** thumtoo-032.1-gp-archive-unarr (on `743dbf4` + agent stack).

### 032.1
- `gp-archive --backend auto|libarchive|unarr` when `THUMTOO_HAVE_UNARR`
- CMake links libunarr into gp-archive when found

### Prior
- 031 AVIF/JXL + bench-smoke-script check; 030 benchtoo rename

### Companion
https://github.com/Grumbel/benchtoo

### Next
- Full `checks.bench-smoke` with optional benchtoo flake input
- RAR4 fixture for unarr timing A/B
- Baseline JSON store + tolerances
