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
| `w` | **pixels** at `kEpubLayoutDpi` | Virtual page width |
| `h` | **pixels** at `kEpubLayoutDpi` | Virtual page height |
| `fs` | points | MuPDF default font size (`fz_layout_document` em arg) |
| `mt`/`mr`/`mb`/`ml` | **pixels** at `kEpubLayoutDpi` | Top/right/bottom/left margin (optional) |

`w`/`h`/margins are converted to points when calling MuPDF:
`pt = px * 72 / kEpubLayoutDpi`.

User CSS is always installed before layout: font-size from `fs`, optional
margins when `mt`/`mr`/`mb`/`ml` are non-zero (else `margin: 0` on body so
UA em-margins do not dominate).

`fs` is the layout font size. It is passed to `fz_layout_document` **and**
forced through user CSS (`html`/`body` font-size with `!important`, body-text
elements inherit) so books that hard-code absolute sizes still respond.
(The old `em` URI key was dropped: UA/CSS margins sized in `em` made font-size
changes look like margin control.)

Optional later: user CSS blob (`css=sha256:…`), `pubcss=0`, disable document CSS.

**Content id** stays `sha256` of the `.epub` file bytes.  
**Tile / size rows** key off the full locator URI including the layout pipe.

## URI

```
file:///books/foo.epub//epub:w=900,h=1350,fs=15//page:3
```

| Pipe | Meaning |
|------|---------|
| `//epub:w=…,h=…,fs=…[,mt=…,mr=…,mb=…,ml=…]` | Layout profile (canonical emit order `w,h,fs` then margins if any; unknown keys ignored) |
| `//page:N` | 1-based page **after** that layout |

Helpers: `with_epub_layout(base, layout)`, `epub_page_uri(path, page, layout)`.

### Defaults (`constants.hpp`)

| Constant | Value |
|----------|-------|
| `kEpubDefaultPageWidthPx` | 900 |
| `kEpubDefaultPageHeightPx` | 1350 |
| `kEpubDefaultFontSizePt` | 15 |
| `kEpubLayoutDpi` | 144 |

`expand_media_uris` emits pages under the default profile only.

### Cache-key stability

`format_epub_layout_params` always emits keys in fixed order (`w,h,fs` then `mt,mr,mb,ml` when any margin is set).
Hand-written URIs with a different key order still parse the same values but
produce a different string until something re-serializes them — full
normalization on parse is a follow-up (see TODO).

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
