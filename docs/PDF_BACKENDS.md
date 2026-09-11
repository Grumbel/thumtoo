# PDF backend (MuPDF)

Poppler support was removed. All PDF page work goes through MuPDF when
`THUMTOO_HAVE_MUPDF` is set at build time.

## URI routes

| Pipe | Meaning |
|------|---------|
| `//page:N` | Default (MuPDF) |
| `//mupdf-page:N` | Explicit MuPDF |
| `//poppler-page:N` | **Legacy alias** — still *parsed* so old session paths open; resolves to MuPDF. New URIs should use `//page:N`. |

Examples:

```
file:///data/doc.pdf//page:1
file:///data/doc.pdf//mupdf-page:1
```

`prepare_paths` / expand emit `//page:N`.

## Layout

| File | Role |
|------|------|
| `pdf.hpp` | Types, URI, scale math, public API with `PdfBackend` |
| `pdf.cpp` | URI parse/build + dispatch to MuPDF |
| `pdf_mupdf.cpp` | MuPDF implementation (`THUMTOO_HAVE_MUPDF`) |

`PdfBackend::Poppler` remains in the enum for ABI stability and is treated as
`Default` (MuPDF).

## MuPDF specifics

- Per-worker TLS: `fz_context`, document, page, **display list**
- Region tiles: `fz_run_display_list` with **device-space** scissor
- Image-heavy gate: `fz_stext` image blocks (coverage) + sparse-text fallback
- Text layer + outline for Find / ToC
- Embedded images: `//pdfimage:N` via `pdf_load_image`

## Embedded images (`//pdfimage:N`)

For scanned PDFs it is often better to extract **Image XObjects** at native
resolution instead of rendering the page at a chosen DPI.

```
file:///book.pdf//pdfimage:1
```

- Implemented with MuPDF (`pdf_load_image` + pixmap)
- Index is **1-based**, document order
- Expand helper: `expand_pdf_image_uris(path)` → `//pdfimage:1..N`
- Default `expand_media_uris` for PDFs still uses `//page:N` (rendered pages)

### Collection expand: `//pdfimages`

```
file:///book.pdf//pdfimages
```

Expands to `//pdfimage:1` … `//pdfimage:N`.
