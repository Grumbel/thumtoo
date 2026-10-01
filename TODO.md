<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO / agent handoff

## Status (2026-10-01)

**Tip:** thumtoo-028.1-smoke-documents (on `743dbf4` + agent stack).

### 028.1
- `bench_smoke.sh` also runs `gen_documents.py` (PDF/MD/TXT/CBZ) and
  `gen_archives.py` when sibling corpus is present

### Companion corpus
**pixel-bench-corpus** tip 006+: self-labeled rasters; sample_book.pdf;
Markdown/text; sample_book.cbz from pdftoppm; optional DjVu.

### Bundle policy
Work-line base: `743dbf4`. Full stack in each tip bundle.

### Next
- WebP/AVIF/JXL in gp-tile; gp-archive; flake checks.bench-smoke
- pdf2djvu in corpus flake for DjVu when wanted
