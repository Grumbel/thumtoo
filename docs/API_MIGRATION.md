<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# API migration — legacy Database → Store (Phase E)

Status: **in progress** (dual-path). Normative schema: [DATABASE.md](DATABASE.md),
plan: [PLAN.md](PLAN.md).

## Layout during dual-path

| Role | Path | Owner |
|------|------|-------|
| Legacy index (pixels, levels, tiles meta) | `$cache/index.sqlite` | `Database` |
| Legacy blob files | `$cache/blobs/` | `BlobStore` |
| Redesign index | `$cache/store/index.sqlite` | `Store` |
| Redesign bulk (tile payloads, http_body) | `$cache/store/bulk.sqlite` | `Store` |
| User overlays | `$data/user.sqlite` (default `$cache`) | `Store` |

`Client::open(cache_root, …, data_root)` opens both. Production should pass
`$XDG_DATA_HOME/thumtoo` (or equivalent) as `data_root` so user tags survive a
cache wipe.

## Feature probe

```c
#ifdef THUMTOO_API_STORE
  client->store().…;
#endif
```

## What still uses legacy `Database`

- Size / meta probe, soft ladder levels, tile encode path, document index,
  archive TOC in the old tables, purge of pixel cache.

## What uses `Store` today

- Direct access via `Client::store()` (collections, bookmarks, links, …).
- **Tags dual-write:** `add_tag` / `remove_tag` update legacy `tags` and, when
  `content_id` is pure `sha256:<64hex>`, also `blob_tag` on `blob:sha256:…`.
  `get_tags` returns the union of both.
- **Probe mirror:** after a successful `request_size` probe (or cache hit with
  known size), `mirror_probe_to_store`:
  - pure `sha256:<hex>` → blob + hash + locator + `ensure_image_media`
  - `sha256:<hex>:page:N` → same file blob + locator + document media + page region
  - other composite ids (`:pdfimage:`, …) are skipped for now
- **Tile dual-write:** `store_tiles` also calls `mirror_tiles_to_store` →
  Store `put_tile` under the matching media/region (same id rules as probe).
- **Tile read fallback:** `get_tile` / `has_tile` / `get_tile_coverage` and
  TileSynth (`get_pixels_from_tiles`) fall back to Store when legacy has no
  tile rows (still needs legacy meta/size for canvas geometry).

## Next cutover steps

1. ~~Resolve locator → blob/hash on Store when probing~~ (pure image + page done).
2. ~~Write new tiles to Store bulk~~ (dual-write from `store_tiles`; legacy
   levels still written until hosts stop relying on them).
3. ~~Soft `get_pixels` assemble from Store tiles~~ (TileSynth via Store fallback).
4. Drop legacy `index.sqlite` after biltoo ships on Store-only open.
5. Move redesign files to top-level `$cache/index.sqlite` + `bulk.sqlite` once
   legacy is gone (epoch already ≥ 100).

## Non-goals until hosts are ready

- Migrating old ladder rows into tiles.
- Sharing one SQLite file between schema 4 and schema 100.
