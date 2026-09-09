<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# EPUB (MuPDF reflowable layout)

## Why MuPDF

thumtoo already links MuPDF for PDF. MuPDF also opens EPUB (and FB2/HTML)
as reflowable documents via `fz_layout_document(ctx, doc, w, h, em)` then the
same page count / bound / raster path as fixed-layout docs.

CREngine remains a possible later backend if typography quality is insufficient.

## Layout is part of identity

Unlike PDF, page index and pixel size depend on:

| Parameter | Unit | Role |
|-----------|------|------|
| `w` | points | Virtual page width |
| `h` | points | Virtual page height |
| `em` | points | Default font size |

Optional later: user CSS (`css=sha256:…` + blob), `pubcss=0`.

**Content id** stays `sha256` of the `.epub` file bytes.  
**Tile / size rows** key off the full locator URI including the layout pipe.

## URI

```
file:///books/foo.epub//epub:w=600,h=900,em=12//page:3
```

| Pipe | Meaning |
|------|---------|
| `//epub:w=…,h=…,em=…` | Layout profile (order of keys free; unknown keys ignored) |
| `//page:N` | 1-based page **after** that layout |

Helpers: `with_epub_layout(base, layout)`, `epub_page_uri(path, page, layout)`.

### Defaults (`constants.hpp`)

| Constant | Value |
|----------|-------|
| `kEpubDefaultPageWidthPt` | 600 |
| `kEpubDefaultPageHeightPt` | 900 |
| `kEpubDefaultEmPt` | 12 |
| `kEpubLayoutDpi` | 144 |

`expand_media_uris` emits pages under the default profile only.

## API surface (`epub.hpp`)

- `is_epub_path` / `parse_epub_uri` / `epub_page_uri`
- `epub_page_count(path, layout)` — layouts then counts
- `epub_page_layout_size(path, page, layout)` — pixels at `kEpubLayoutDpi`
- `epub_render_tile_cell` / region raster (MuPDF, device-space scissor)

## Client

Size probe and live tiles treat `//epub:…//page:N` like PDF pages (rgb888 live,
durable JPEG at `scale >= kPdfMinDurableTileScale`).

## Non-goals (v1)

- User CSS / themes in URI
- Continuous scroll (app maps to pages)
- DRM
- Dual backend (CREngine)
