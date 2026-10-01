<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO / agent handoff

## Status (2026-10-01)

**Tip:** thumtoo-024.1-mupdf-stub-bench-verify (on `743dbf4` + agent stack).

### 024.1
- Fix: `pdf_mupdf_stub.cpp` when MuPDF absent (link of tools without mupdf)
- Verified: corpus generate, `thumtoo-microbench-decode`, `thumtoo-bench --json`
  on synthetic JPEG; numbers in MICROBENCH_RESULTS.md

### 023.1
- `thumtoo-bench` `--json` / `--keep-cache`; microbench-decode `--json`

### 022.1
- docs/BENCHMARK_KIT.md plan

### Bundle policy
Work-line base: `743dbf4`. Full stack in each tip bundle.

### Next (not in this tip)
- Publish pixel-bench-corpus; flake input + `checks.bench-smoke`
- gp-tile codec matrix; gp-archive libarchive/unarr
- RGB synthetic corpus (current greyscale is fine for relative decode)
