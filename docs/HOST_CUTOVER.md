<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Host cutover — dual-path Store → Store-only

**Audience:** biltoo (and other hosts) after thumtoo Phase E library dual-path.

Library spine (0.1.0): see [PLAN.md](PLAN.md) success criteria and
[API_MIGRATION.md](API_MIGRATION.md). This document is the **host** checklist
before dropping legacy `Database` / durable soft `levels`.

---

## 1. What hosts already get (dual-path)

| Open args | Layout |
|-----------|--------|
| `Client::open(cache_root, executor, workers, data_root)` | Legacy `cache_root/index.sqlite` + `blobs.sqlite`; redesign under `cache_root/store/`; `user.sqlite` under `data_root` (XDG data) |

| Path | Dual-path behaviour |
|------|---------------------|
| Size probe | Legacy content/locator + Store blob/media/region mirror |
| Tiles encode | Legacy BlobStore + Store `put_tile` |
| Soft PreferCache | Soft levels if present; else TileSynth (legacy or Store tiles) |
| Soft EnsurePixels | Skips soft-ladder **write** when tiles already cover |
| Tags | Legacy `tags` + Store `blob_tag` for pure `sha256:` |
| Directory | `Client::refresh_directory_snapshot` / list → Store |
| Archive TOC | Dual-write + Store read fallback |

**biltoo:** pass `$XDG_DATA_HOME/thumtoo` as `data_root` (tip biltoo-1002).

---

## 2. Host tile-native (before dropping levels)

1. **Filmstrip / SoftOnly** — Prefer durable soft ladder **or** TileSynth reply
   from `get_pixels(..., allow_tile_synth=true)` / `get_pixels_from_tiles`.
   Do not require a soft `levels` row for “have pixels”.
2. **PreferCache / Overview** — Treat TileSynth as a valid PreferCache delivery
   (already true for thumtoo `get_pixels` default). Host climb SM must not
   treat “no soft level row” as permanent miss when tiles cover.
3. **Full / crop** — Keep full-native encode path; still may use legacy levels
   until a full-level-from-tiles path exists (out of scope for first cutover).
4. **Gallery** — Overview edge may be TileSynth; escalate to Full as today.
5. **Verify** with cache wipe of legacy `levels` only (tiles remain): filmstrip
   and PreferCache still paint within budget.

Normative climb ownership stays in biltoo
[THUMTOO_HOST_CONTRACT.md](../../biltoo/docs/THUMTOO_HOST_CONTRACT.md) (external
tree); do not invent a second PreferCache retry loop.

---

## 3. Drop legacy index (after tile-native ships)

1. ~~Stop writing durable soft `levels` / soft ladder~~ (**done** ≥234; default
   tiles-first; `THUMTOO_SOFT_LEVELS=1` restores).
2. **Top-level Store (default ≥245):** Store at `$cache/{index,bulk}.sqlite` and
   legacy Database/BlobStore under `$cache/legacy/`. Opt out with
   `THUMTOO_STORE_ROOT=0` for classic dual-path (`$cache/store/` + top-level legacy).
   **Auto-migrate (≥238):** on open, classic dual-path caches move top-level legacy
   schema files → `legacy/` and `store/` redesign files → cache root (skips if
   destinations already exist). Soak-confirmed with biltoo.
3. **Store-only (`THUMTOO_STORE_ONLY=1`, ≥247–251):** no legacy Database/BlobStore.
   Durable Store only for the common URI kinds (file, PDF, DjVu, EPUB, archive
   members, HTTP images): probe, session pixels, tile cells.
4. Dual-write (`mirror_probe_to_store`) skipped under STORE_ONLY; tags are
   Store-only. `mirror_tiles_to_store` remains the durable tile write path.
5. Public APIs null-safe under STORE_ONLY: `prepare_paths`, archive TOC,
   document page count, text/outline (session extract), `invalidate_tile`.
6. Host soak with **biltoo ≥ 1002** (`THUMTOO_STORE_ONLY=1`, dedicated cache).
7. After soak confirm → default STORE_ONLY → delete dual-write helpers.
8. Migration: **no** ladder→tile conversion (PLAN non-goal); cold rebuild tiles.

Under tiles-first, EnsurePixels does not write durable `levels` for soft *or*
full_native. Session encode + TileSynth (when the pyramid covers the want) supply
the reply. Opt in to durable levels with `THUMTOO_SOFT_LEVELS=1`.

---

## 4. Soft-level write policy (tiles-first default)

**Default (thumtoo ≥ 234, extended ≥244):** **no durable `levels` writes** (soft,
overview, or full_native). Tiles still persist. Full EnsurePixels replies via
session encode and/or TileSynth when a pyramid covers the want. Restore durable
levels with `THUMTOO_SOFT_LEVELS=1` or `THUMTOO_TILES_ONLY=0`.

| Variable | Effect |
|----------|--------|
| *(unset)* | Tiles-first — no soft/overview level writes |
| **`THUMTOO_SOFT_LEVELS=1`** | Restore durable soft/overview/**full** level writes |
| **`THUMTOO_TILES_ONLY=0`** | Same (explicit opt-out of tiles-first) |
| **`THUMTOO_TILES_ONLY=1`** | Explicit tiles-first (same as default) |
| **`THUMTOO_STORE_ROOT`** | Default **on**: Store at `$cache/`; legacy under `$cache/legacy/`. `=0` restores dual-path under `store/` |

Pair with biltoo ≥1007 (`scheduleSoftPixels`) for PreferCache when tiles exist.

---

## 5. Status

| Item | State |
|------|--------|
| Library dual-path | **Done** (thumtoo-229) |
| biltoo `data_root` | **Done** (biltoo-1004; XDG data root) |
| biltoo tile-native PreferCache / filmstrip | **Partial** (1005 Prefer plateau; 1006–1007 `scheduleSoftPixels`) |
| Tiles-first soft writes (default) | **On** (thumtoo-234; opt out via `THUMTOO_SOFT_LEVELS=1`) |
| Top-level Store layout (`THUMTOO_STORE_ROOT`) | **Default on** (≥245; migrate ≥238; soak OK; `test_store_root`) |
| Full-from-tiles (EnsurePixels Full) | **On** when tile pyramid covers want (≥243) |
| Tiles-first skips full_native levels | **On** (≥244; same env as soft) |
| Store-only (`THUMTOO_STORE_ONLY`) | **Soak-ready** (≥247–253; public APIs null-safe) |

Soft ladder rows may still exist in older caches; new soft encodes no longer
persist them unless soft levels are explicitly re-enabled.

Purge existing soft/overview levels (keep tiles + full_native):

```bash
thumtoo-gc --cache "$XDG_CACHE_HOME/thumtoo" --soft-levels
# or: thumtoo-gc --soft-levels --dry-run
```

