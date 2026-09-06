<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Galapix integration

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

## Adapter on Galapix develop

Galapix develop ships an optional **`ThumtooTileProvider`** (`TileProvider`):

- Sources: `src/thumtoo/thumtoo_tile_provider.{hpp,cpp}`
- Build: `-DWITH_THUMTOO=ON -DTHUMTOO_DIR=/path/to/thumtoo`
- Docs: galapix `docs/THUMTOO.md`

```cpp
auto client = thumtoo::Client::open(cache_root);
auto provider = galapix::ThumtooTileProvider::create(
    client, thumtoo::file_uri_from_path(abs_path));
image.set_tile_provider(provider);
```

Cache hits use `get_tile`; misses use `request_tile` (JPEG → `surf::jpeg::load_from_mem`).

Default image open still uses `DatabaseTileProvider` + SQLite until the UI path
is switched under `HAVE_THUMTOO`.

## Viewer flow

1. Convert path / archive member to thumtoo URI.
2. `get_size` / `request_size` for layout bounds (also done inside `create`).
3. Visible tiles via `TileProvider::request_tile` → thumtoo.
4. Optional prewarm: `thumtoo-prepare --tiles` or `request_tile_pyramid`.

## Out of scope (first cut)

- PDF / video tiles
- Full replacement of `SQLiteTileDatabase` for all resources
- Remote HTTP tile sources

See [TILES.md](TILES.md) for storage and API details.
