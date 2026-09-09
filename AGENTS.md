<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Agent notes — thumtoo

## Intent

thumtoo is a **cache and index library**, not a viewer. Do not add GUI code
here. Product rules for biltoo modes stay in biltoo’s DOMAIN/IDENTITY docs.

## Related repositories

- biltoo: <https://github.com/Grumbel/biltoo>
- dirtoo: <https://github.com/Grumbel/dirtoo>
- galapix: <https://github.com/Galapix/galapix>
- dirtoo-py: <https://github.com/Grumbel/dirtoo-py>

## Documentation map

| File | Purpose |
|------|---------|
| [README.md](README.md) | What / build / scope table |
| [DESIGN.md](DESIGN.md) | Normative design + Phase 0 constants |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Tree and dependencies |
| [TODO.md](TODO.md) | Phase tracking |
| [INTEGRATION.md](INTEGRATION.md) | biltoo API mapping |
| [TAGS.md](TAGS.md) | content-hash tags vs dirtoo TagStore |

## Handoff

## Status (2026-09-09)

**Tip: thumtoo-102.** Full session notes: top of [TODO.md](TODO.md).
PDF dual backend (MuPDF + Poppler) with `//page:` / `//poppler-page:` / `//mupdf-page:`.
Live PDF tiles return `rgb888` (tests + Galapix codec branch required).
Retrieval stack: Location URIs → content-id → read_source_bytes → PDF@144dpi → HTTP(S)+session cache.


**Phase 1–2 done:** Client, prepare/status CLIs, system SQLite, libvips+JXL ladder,
archive TOC + member extract, tags, PDF `//page:N`, biltoo INTEGRATION.md.

**Phase 4 tiles done (library side):** 256² JPEG pyramid, Client get/request_tile,
`prepare --tiles`, status. See [TILES.md](TILES.md).

**Performance (2026-09-07):** Multi-worker job queue (`Client::open` worker count /
`thumtoo-prepare --jobs`); parallel per-scale JPEG encode; 512 MiB extract cache;
same-archive coalesce for probe **and** tiles/ladder. Prepare `--stats` prints
`wall=` vs summed `cpu:` scopes. Tip bundle: **thumtoo-023** (session handoff 2026-09-07). (Location URI API).

Galapix uses thumtoo as a flake input (source `THUMTOO_DIR`); interactive
`request_tile` is single-scale. Location URI parse/format is in-tree; next gaps: http(s) fetch, content-id blob resolve, single-cell cut if still open.

### Threading contract (do not break)
* `get_*` — cache-only, GUI-safe reads
* `request_*` — enqueue; callbacks via `Executor` (not raw worker threads for GUI)
* Workers share one SQLite (WAL + busy_timeout); watch lock storms at high jobs

**Later:** cache eviction / `thumtoo-gc`; optional D-Bus; video animated preview.

Ladder blobs: `blobs.sqlite`. Tile blobs will share that file under `tile_blobs`.

## Commits

Author: Ingo Ruhnke \<grumbel@gmail.com\>  
Co-authored-by: Grok \<grok@x.ai\> when applicable  
License: GPL-3.0-or-later, REUSE SPDX headers on new files
