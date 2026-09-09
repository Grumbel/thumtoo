# PDF backends (Poppler + MuPDF)

## URI routes

| Pipe | Backend |
|------|---------|
| `//page:N` | **Default** — MuPDF when `THUMTOO_HAVE_MUPDF`, else Poppler |
| `//poppler-page:N` | Force Poppler |
| `//mupdf-page:N` | Force MuPDF |

Examples:

```
file:///data/doc.pdf//page:1
file:///data/doc.pdf//poppler-page:1
file:///data/doc.pdf//mupdf-page:1
```

`prepare_paths` / expand emit `//page:N` (default route). Explicit pipes compare engines.

## Layout

| File | Role |
|------|------|
| `pdf.hpp` | Types, URI, scale math, public API with `PdfBackend` |
| `pdf.cpp` | Poppler implementation + dispatch + URI |
| `pdf_mupdf.cpp` | MuPDF implementation (optional `THUMTOO_HAVE_MUPDF`) |

## MuPDF specifics

- Per-worker TLS: `fz_context`, document, page, **display list**
- Region tiles: `fz_run_display_list` with page-space clip (list built once per page)
- Image-heavy gate: `fz_stext` image blocks (coverage) + sparse-text fallback

## Status

- [x] URI pipes + `PdfBackend`
- [x] MuPDF module + cmake/flake
- [x] Dispatch count / size / raster / tiles by backend
- [x] Galapix min_scale uses backend from URI
- [x] MuPDF image coverage via structured-text image blocks
