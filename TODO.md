# TODO / agent handoff

## Status (2026-09-30)

**Tip:** thumtoo-010.3-document-page-count-store (on origin `b37b5e2`).

### 010.3
- `Client::document_page_count` Store-first (`media.page_count` via file://).
- `refresh_document_index` mirrors live page_count into Store.
- Works when PDF/DjVu/EPUB file is missing if previously indexed (archive TOC parity).

### On origin already
- 010.2 tile supersede activity (b37b5e2 / d32d292)
- 009.2 tile size stretch
