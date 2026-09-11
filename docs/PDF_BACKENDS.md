# PDF backend (MuPDF)

PDF pages use **MuPDF** only (`THUMTOO_HAVE_MUPDF`). Poppler was removed.

## URI routes

| Pipe | Meaning |
|------|---------|
| `//page:N` | Default page (MuPDF) |
| `//mupdf-page:N` | Explicit MuPDF |
| `//poppler-page:N` | Accepted for old paths; normalized to `PdfPage` / `//page:N` on format |

```
file:///data/doc.pdf//page:1
file:///data/doc.pdf//mupdf-page:1
```

`prepare_paths` / expand emit `//page:N`.

## Layout

| File | Role |
|------|------|
| `pdf.hpp` | Types, URI, scale math, public API |
| `pdf.cpp` | URI parse/build + dispatch to MuPDF |
| `pdf_mupdf.cpp` | MuPDF implementation |

`PdfBackend::Poppler` remains as a deprecated ABI alias of `Default`.

## Capabilities

- Page raster / region tiles (display list + device-space scissor)
- Image-heavy gate via `fz_stext` image blocks
- Text layer + outline (Find / ToC)
- Embedded images: `//pdfimage:N`

## Embedded images

```
file:///book.pdf//pdfimage:1
file:///book.pdf//pdfimages
```

Expand helpers: `expand_pdf_image_uris` / `expand_pdf_images_collection_uri`.
Default expand for PDFs uses rendered `//page:N`.
