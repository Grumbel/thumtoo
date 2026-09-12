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
| FastBatch ≤1024 | `kBatchMaxEdge`, `kBatchWindowMembers` (32) |
| Archive cursor | `ArchiveCursor`, `plan_archive_batch_window`, Client worker uses TOC-ordered window |
| Tile pyramid | `get_tile` / `request_tile` / `request_tile_pyramid`, [TILES.md](../TILES.md) |
| Construct ladder from tiles | **`get_pixels_from_tiles`** (`PixelSource::TileSynth`) |
| Quality | `PixelSource`, `TileSource` |
| Focus full | full decode → `request_tile_pyramid` |

## Archive cursor (Phase 2 partial)

- **One logical cursor per archive path** on `Client` (`archive_cursors_`).
- TOC is loaded once (image members only) via `ensure_archive_cursor`.
- `plan_archive_batch_window(ordered, interest, next_index, max_window)` is pure:
  returns a TOC-ordered contiguous window ≤ `kBatchWindowMembers`.
- Worker same-archive coalesce path plans the extract set through the cursor and
  advances `next_index` past the last extracted TOC index after a successful
  libarchive pass.
- Process extract LRU (existing) remains the shared member-byte cache for focus.

Still open: explicit `set_interest` / cancel-by-epoch, dedicated FastScale Q1
encode path at ≤1024 (soft remains capped at 512), second-handle focus policy.

## Phase status

- [x] Tile pyramid + soft ladder (existing)
- [x] `PixelSource::TileSynth` + `get_pixels_from_tiles` (construct)
- [x] Archive cursor + TOC-ordered windowed batch extract (worker path)
- [ ] Interest cancel / `set_interest`
- [ ] FastScale Q1 durable/overview path distinct from soft-512
- [ ] Unified `request_raster` API
