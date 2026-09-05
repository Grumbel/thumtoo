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
| [README.md](README.md) | What / links / status |
| [DESIGN.md](DESIGN.md) | Goals, schema, normative rules, phases |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Planned tree and dependencies |
| [TODO.md](TODO.md) | Done / Phase 0 freezes / Later |

## Handoff

Design has absorbed an external review pass. Before Phase 1 code:

1. Freeze URI + provisional-id + status enum + schema_version=1.
2. Freeze callback/executor threading contract for biltoo.
3. Keep directory snapshots (cache-first folder open on sleeping USB/NAS).
4. Archive path sanitization + decompression caps are normative.

Latest tip documents: status enum, schema_meta, still_count/frame_idx video
model, Location `//archive:` form, WAL + priority queues, cache lifecycle as
named Later gap.

## Commits

Author: Ingo Ruhnke \<grumbel@gmail.com\>  
Co-authored-by: Grok \<grok@x.ai\> when applicable  
License: GPL-3.0-or-later, REUSE SPDX headers on new files
