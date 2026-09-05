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
| [DESIGN.md](DESIGN.md) | Goals, schema sketch, phases, non-goals |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Planned tree and dependencies |

## Handoff (review-ready)

- **Identity**: content-id (sha256) + Location URIs (`file:///…//archive:member`)
- **Ladder**: fixed long-edge levels; blobs under XDG only
- **Video**: `content.still_count` + `levels.frame_idx` (N=16 fixed for now);
  no storyboard in cache; subprocess preferred for decode
- **Next implementation**: Phase 0 freeze constants → Phase 1 headers + SQLite spike

## Commits

Author: Ingo Ruhnke \<grumbel@gmail.com\>  
Co-authored-by: Grok \<grok@x.ai\> when applicable  
License: GPL-3.0-or-later, REUSE SPDX headers on new files
