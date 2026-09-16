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

1. Stop writing durable soft `levels` / `blobs` soft ladder (thumtoo flag or
   compile-time once hosts verified).
2. Open Store at top-level `$cache/index.sqlite` + `bulk.sqlite` (epoch ≥ 100);
   remove `cache_root/store/` subdirectory layout.
3. Remove dual-write helpers (`mirror_*`) and legacy `Database` open.
4. Migration: **no** ladder→tile conversion (PLAN non-goal); cold rebuild tiles.

---

## 4. Optional test flag

`THUMTOO_TILES_ONLY=1` — refuse durable soft/overview **level** writes. Tiles and
`full_native` levels still persist. Soft EnsurePixels still encodes in memory
when needed; replies fall back to TileSynth when tiles cover. Use with biltoo
`scheduleSoftPixels` (PreferCache when tiles exist) for tiles-first soak tests.
Not required for dual-path production.

---

## 5. Status

| Item | State |
|------|--------|
| Library dual-path | **Done** (thumtoo-229) |
| biltoo `data_root` | **Done** (biltoo-1002) |
| biltoo tile-native PreferCache / filmstrip | **Open** |
| Drop legacy levels / index | **Blocked** on host tile-native |

