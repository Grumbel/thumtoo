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

`prepare_paths` / expand still emit `//page:N` (default route). Side-by-side
comparison uses the explicit pipes.

## Layout

| File | Role |
|------|------|
| `pdf.hpp` / shared helpers | Types, URI parse, scale math, dispatch |
| `pdf.cpp` | Poppler implementation + URI + backend resolve |
| `pdf_mupdf.cpp` | MuPDF implementation (optional build) |

Public functions take path + page; render paths that go through `ParsedPdfUri`
honour `backend`. Path-only APIs (`pdf_page_count`, …) use the **default**
backend unless an overload is added later.

## Status

- [x] URI pipes + `PdfBackend` enum
- [ ] MuPDF raster / tile / stats (next)
- [ ] Dispatch from `Client` / `pdf_render_tile_cell` by backend
- [ ] Default expand → MuPDF when available
