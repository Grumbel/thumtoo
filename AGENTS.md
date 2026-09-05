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
| [README.md](README.md) | What / links / status / scope table |
| [DESIGN.md](DESIGN.md) | Normative design + Phase 0 constants |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Planned tree and dependencies |
| [TODO.md](TODO.md) | Phase tracking |

## Handoff

Design is **implementable**. Second review pass closed; directory snapshot
boundary is visible in the README scope table (thumtoo = durable snapshot
storage; dirtoo = live listing / when to snapshot).

Phase 0 constants are in DESIGN §6b (edges 128…2048, WebP q=80, archive
512 MiB / 100:1 / 2 GiB, WAL, still_count=16). Next work is Phase 1 headers +
SQLite spike and a biltoo adapter note.

## Commits

Author: Ingo Ruhnke \<grumbel@gmail.com\>  
Co-authored-by: Grok \<grok@x.ai\> when applicable  
License: GPL-3.0-or-later, REUSE SPDX headers on new files
