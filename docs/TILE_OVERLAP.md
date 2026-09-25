<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tile edge overlap — removed

There is **no** `kTileOverlap`. Cells are exclusive **≤256×256**. Grid step is
`kTileSize`.

The 257 / paint-expand / encode-strip / overscan experiments are gone. Do not
reintroduce them.

PDF page tiles: full-page raster at the scale’s dpi, then exclusive crop (see
TILES.md). Layout pixels are a single `lround(page_pt * dpi/72)` from the
continuous page bound — not integer 72dpi points then scaled again.
