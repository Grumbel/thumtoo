<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tile edge overlap (retired for QPainter hosts)

`kTileOverlap` is **0**. New encodes are exclusive **256×256** (edge tiles
smaller). Grid step remains `kTileSize`.

## Why it was tried

A 1px right/bottom strip (257×257) was meant to help host bilinear filtering
at cell edges (“paint expands dest”). That model does not work with QPainter:
per-tile `SmoothPixmapTransform` clamps at each `drawImage` edge, and mapping
257→256 dest scales every cell.

## Host fix (biltoo)

Assemble exclusive tile pixels into one buffer at 1:1, then smooth-scale once.
See biltoo `docs/RESEARCH_TILE_OVERLAP.md` §18.

## Legacy Store

Caches may still contain 257-wide tiles until re-prepared. Hosts should paint
the exclusive subrect (first 256 columns/rows). Debug:

```bash
export THUMTOO_DEBUG_TILE_OVERLAP=1
```

Paints a pink strip on the extra right/bottom pixels when payload is larger
than exclusive (read-path only). No effect when `kTileOverlap == 0` and the
tile is exclusive-sized.
