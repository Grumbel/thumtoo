<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tile edge overlap (bilinear seams)

Each cell payload is up to **257×257** (`kTileSize + kTileOverlap`) with a
1px strip on the **right and bottom** shared with the next cell. The content
grid step stays **256**.

Paint expands the destination from the bitmap size so `SmoothPixmapTransform`
samples the shared strip. Legacy 256×256 Store tiles still paint (no expand).

**Cache:** re-prepare or purge tiles for full seam quality; mixed caches are
safe (old tiles sharp but seamed, new tiles blended).

