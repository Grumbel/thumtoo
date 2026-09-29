<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Markdown documents (MuPDF ≥ 1.28)

Status: **thumtoo PathKind + expand wired** (2026-09-29). Requires MuPDF 1.28+
(see `pinMupdf` in `flake.nix`).

## Behaviour

- Extensions: `.md`, `.markdown`, `.mdown`, `.mkd`
- `PathKind::Markdown`
- Expand / prepare: same as PDF — `file://…//page:N` via MuPDF page count + raster
- `parse_pdf_uri` accepts markdown paths (shared MuPDF page pipeline)
- MIME: `text/markdown`, `text/x-markdown` in `media_mime_types()`

## Not yet

- Plain `.txt` (separate decision; not MuPDF Markdown)
- biltoo open dialog / session expand filters
- ePub-style layout CSS knobs for markdown

## Related

- biltoo `docs/TXT_MD_SUPPORT.md`
- `docs/PDF_BACKENDS.md` (MuPDF pin)
