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

## Status (2026-09-06)

**Phase 1–2 done:** Client, prepare/status CLIs, system SQLite, libvips+JXL ladder,
archive TOC + member extract, tags, PDF `//page:N`, biltoo INTEGRATION.md.

**Phase 4 started:** optional grid tiles (galapix-style). See [TILES.md](TILES.md)
and [TODO.md](TODO.md) Phase 4 checklist. Plain ladder remains the biltoo path.

**Later:** cache eviction / `thumtoo-gc`; optional D-Bus; video animated preview.

Ladder blobs: `blobs.sqlite`. Tile blobs will share that file under `tile_blobs`.

## Commits

Author: Ingo Ruhnke \<grumbel@gmail.com\>  
Co-authored-by: Grok \<grok@x.ai\> when applicable  
License: GPL-3.0-or-later, REUSE SPDX headers on new files
