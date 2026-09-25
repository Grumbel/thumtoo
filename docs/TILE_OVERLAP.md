<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tile edge overlap (encode only)

`kTileOverlap` is **1**. Each cell **payload** may be up to **257×257** with a
shared strip on the **right and bottom**. The content **grid step stays 256**.

## Why encode overlap

Exclusive 256×256 PDF/vector raster clips (MuPDF region render) drop
hairlines that sit on the tile grid. Including +1 px past the exclusive edge
captures those strokes in the neighbour cell as well.

## Host paint (biltoo)

Do **not** map full 257 → exclusive 256 dest (scales the cell). Paint the
**exclusive** subrect only (`src` width/height ≤ 256). Per-tile
`SmoothPixmapTransform` can still show mild seams; that is separate from
missing line content.

## Cache

Re-prepare after changing `kTileOverlap` so Store tiles match. Mixed caches
are readable: host exclusive-crops when `w > 256`.

## Debug

```bash
export THUMTOO_DEBUG_TILE_OVERLAP=1
```

Pink strip on the extra right/bottom pixels (read-path only).
