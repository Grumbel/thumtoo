<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tile edge overlap — removed

`kTileOverlap` is **0**. Cells are exclusive **≤256×256**. Grid step is
`kTileSize`.

The 257 / paint-expand / encode-strip experiments caused scale bugs, selection
of wrong source rects, and mixed caches. They are not coming back without a
separate design.

Legacy Store tiles wider than 256: hosts may crop to the exclusive subrect
when reading; new encodes never write overlap.

`THUMTOO_DEBUG_TILE_OVERLAP` is a no-op while `kTileOverlap == 0`.
