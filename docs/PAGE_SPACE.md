<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Page space (text / OCR regions)

Region bboxes live in **page space** defined by `PageTextLayer::page_bounds`
and `PageTextLayer::page_y_up`. Raster (source) space is always top-left,
Y-down. Hosts map with the **flag on the layer** — do not guess from the URI
when a layer is present.

## Conventions

| Kind | `page_y_up` | Units | Origin |
|------|-------------|-------|--------|
| PDF native / OCR | `true` | PDF points (media box) | lower-left |
| DjVu native / OCR | `true` | page pixels | lower-left |
| EPUB native / OCR | `true` | page box (MuPDF / layout) | lower-left |
| Plain image / archive member OCR | `false` | source pixels | top-left |

Native extractors set `page_y_up = true`. OCR for document pages maps Tesseract
(top-left) boxes into the **same** Y-up page space. OCR for plain images keeps
Y-down page_bounds = image size.

## Wire format

`serialize_page_text_layer` magic **TTL7** stores `page_y_up` after
`page_bounds`. Older blobs:

- Native → treat as Y-up
- Ocr → treat as Y-down (historical raster mapping before the OCR Y fix)

Re-OCR refreshes stored OCR layers to TTL7 + Y-up for documents.

## Host contract

1. Prefer `layer.page_y_up` for every page↔source map.
2. Fallback when no layer yet: document page refs (PDF/DjVu/EPUB) → Y-up;
   plain paths → Y-down.
3. Host-prepared RGB OCR (`ocr_rgb_page_text_layer`) returns boxes in the
   buffer’s top-left space (`page_y_up = false`); the host remaps to page space
   when installing overlays.

## OCR source DPI

Tesseract is told a source resolution via `SetSourceResolution` before
`Recognize`:

| Input | DPI |
|-------|-----|
| `OcrOptions::dpi > 0` | host value (clamped 70–600) |
| Document page box (PDF points / Y-up) | `72 × raster_w / page_bounds_w` |
| Pixel page box (plain image / RGB host buffer) | **300** (not 72) |

Appearance OCR should pass the true page density (same as full-page OCR of that
document) so a crop does not change implied character size in inches. Mixed font
sizes on one page segment poorly when DPI is left at Tesseract’s ~70 default.
