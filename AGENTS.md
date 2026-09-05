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

## Handoff

- Latest: cache-first / XDG-only / hash identity / tags / deferred http(s) /
  Location URI form (`file:///…//archive:member`, nested via pipe)
- Next: phase 0 freeze URI + content-id rules; phase 1 headers + SQLite spike;
  video stills / storyboard design still open; biltoo adapter note

## Commits

Author: Ingo Ruhnke \<grumbel@gmail.com\>  
Co-authored-by: Grok \<grok@x.ai\> when applicable  
License: GPL-3.0-or-later, REUSE SPDX headers on new files
