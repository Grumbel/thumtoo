<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Galapix integration (sketch)

Map Galapix develop’s tile pipeline onto thumtoo Phase 4 tiles. thumtoo remains
a **cache library** (no OpenGL / layout). Galapix keeps rendering and workspace.

## Mapping

| Galapix | thumtoo |
|---------|---------|
| `files` row (url, w, h) | `locators` + `content` (`get_size` / `get_meta`) |
| `tiles` (fileid, scale, x, y) → JPEG | `get_tile` / `request_tile` → `TileBlob` (JPEG) |
| `TileGenerator` | worker `EnsureTiles` + `build_tile_pyramid` |
| `thumbgen` / prepare | `thumtoo-prepare --tiles` |
| Archive `file://…//rar:…` | `file:///…//archive:member` |

Scale convention matches Galapix: **scale 0 = full resolution**, tile size **256**.

## Suggested flow (viewer)

1. Convert path / archive member to thumtoo URI (`file_uri_from_path` or archive form).
2. `get_size(uri)` / `request_size` for layout bounds.
3. For each visible tile: `get_tile(uri, scale, x, y)`; on miss `request_tile(…, cb)` and upload texture when the callback fires (on Galapix’s UI executor).
4. Optional session prewarm: `request_tile_pyramid(uri)` or CLI `--tiles`.

## Not wired yet

- Galapix still owns its SQLite by default; switching `SQLiteTileDatabase` to a
  thumtoo-backed `TileDatabaseInterface` is a develop-branch change.
- PDF / video tiles are out of scope for the first cut.
- Codec is JPEG in thumtoo tiles; ladder remains JXL for biltoo.

See [TILES.md](TILES.md) for storage and API details.
