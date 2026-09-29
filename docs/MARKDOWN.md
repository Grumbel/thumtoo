<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Markdown and plain text (MuPDF)

Status: **thumtoo PathKind + expand wired** (2026-09-29). Requires MuPDF ≥ 1.28
(see `pinMupdf` in `flake.nix`).

## Markdown

- Extensions: `.md`, `.markdown`, `.mdown`, `.mkd`
- `PathKind::Markdown`

## Plain text

- Extensions: `.txt`, `.text`
- `PathKind::PlainText`
- MuPDF opens these as reflowable text documents (same //page:N pipeline)

## Shared behaviour

- Expand / prepare: `file://…//page:N` via MuPDF page count + raster
- `parse_pdf_uri` accepts these paths (`is_mupdf_page_document_path`)
- MIME: `text/markdown`, `text/x-markdown`, `text/plain`

## Not yet

- biltoo filters for `.txt` (see biltoo follow-up)
- ePub-style layout CSS knobs for text/md
- Arbitrary source files as text (`.py`, …) without renaming

## Related

- biltoo `docs/TXT_MD_SUPPORT.md`
- `docs/PDF_BACKENDS.md`
