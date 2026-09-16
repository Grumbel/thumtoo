<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# API migration — legacy Database → Store (Phase E)

Status: **complete for Client** (Store-only ≥262). Normative schema:
[DATABASE.md](DATABASE.md), plan: [PLAN.md](PLAN.md), host notes:
[HOST_CUTOVER.md](HOST_CUTOVER.md).

## Layout (default, `THUMTOO_STORE_ROOT` on)

| Role | Path | Owner |
|------|------|-------|
| Redesign index | `$cache/index.sqlite` | `Store` |
| Redesign bulk (tile payloads, http_body) | `$cache/bulk.sqlite` | `Store` |
| User overlays | `$data/user.sqlite` | `Store` |
| On-disk legacy (migrated only) | `$cache/legacy/` | unused by Client |

`Client::open(cache_root, …, data_root)` opens **Store only**. Production should
pass `$XDG_DATA_HOME/thumtoo` (or equivalent) as `data_root` so user tags
survive a cache wipe.

Classic dual-path trees are migrated once at open: top-level schema-4 files
→ `legacy/`; redesign under `store/` → cache root. Opt out of that layout with
`THUMTOO_STORE_ROOT=0` (Store stays under `$cache/store/`).

## Feature probe

```c
#ifdef THUMTOO_API_STORE
  client->store().…;
#endif
```

## What Client uses

- All probe / pixels / tiles / tags / directory / archive TOC paths go through
  Store handlers (`*_store_only`).
- `has_legacy()` is always false; `Client::db()` throws.
- `store_only_mode()` always true; `dual_write_to_store_enabled()` always false.

## Legacy classes (tools / tests only)

`Database` and `BlobStore` remain for `thumtoo-gc`, `test_database`, and
on-disk migrate fixtures. They are not opened by `Client`.

## Cutover checklist (done)

1. ~~Dual-path Client open~~ → Store-only (≥262).
2. ~~Probe / pixels / tiles dual handlers~~ → Store-only forwards (≥263).
3. ~~Store size/meta fallback~~ (`get_size` / `get_meta`).
4. ~~Tiles-first (no durable soft levels)~~ (always on Client; env no-ops ≥265).
5. ~~Top-level Store path~~ (default ≥245; `test_store_root`).
6. ~~Full-from-tiles for Full EnsurePixels~~ when pyramid covers the request.
7. Hosts pass `$XDG_DATA_HOME/thumtoo` as `data_root` (biltoo does).

## Non-goals

- Migrating old ladder rows into tiles.
- Sharing one SQLite file between schema 4 and schema 100.
- Restoring Client dual-path via env (`THUMTOO_STORE_ONLY=0` is ignored).
