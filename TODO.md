# TODO / agent handoff

## Status (2026-09-29)

**Tip:** `thumtoo-354.1-markdown-pathkind` (base `fb6a408`).

### 354.1
- PathKind::Markdown + is_markdown_path / is_mupdf_page_document_path
- expand + prepare_paths: //page:N via MuPDF (same as PDF)
- parse_pdf_uri accepts .md paths; image/ocr skip Vips for markdown
- MIME text/markdown; docs/MARKDOWN.md

### Prior
- 353.x MuPDF 1.28.5 pin + mupdf.pc Version

### Next
- biltoo: PagePath / session expand / open filters for .md
- Optional: .txt synthetic HTML
- Manual: thumtoo-prepare sample.md; biltoo open after filters

### Apply
```bash
git pull --ff-only …/thumtoo-354.1-markdown-pathkind-fb6a408.bundle HEAD
```
