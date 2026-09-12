<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Pixel pipeline (thumtoo)

Host-facing design lives in biltoo:

[`docs/PIXEL_PIPELINE_REDESIGN.md`](https://github.com/Grumbel/biltoo/blob/master/docs/PIXEL_PIPELINE_REDESIGN.md)

## Mapped onto existing thumtoo APIs

| Plan concept | thumtoo today |
|--------------|---------------|
| Soft ≤512 | `get_pixels` / `request_pixels`, `kMaxSoftLadderEdge` |
| FastBatch ≤1024 | `kBatchMaxEdge`; Lane A still to implement (archive cursor) |
| Tile pyramid | `get_tile` / `request_tile` / `request_tile_pyramid`, [TILES.md](../TILES.md) |
| Construct ladder from tiles | **`get_pixels_from_tiles`** (`PixelSource::TileSynth`) |
| Quality | `PixelSource`, `TileSource` |
| Focus full | full decode → `request_tile_pyramid` |

## Phase status

- [x] Tile pyramid + soft ladder (existing)
- [x] `PixelSource::TileSynth` + `get_pixels_from_tiles` (construct)
- [ ] FastBatch archive cursor + interest cancel
- [ ] Unified `set_interest` API
