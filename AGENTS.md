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

## Handoff

Phase 1: Client + worker + prepare; **libvips + JPEG-XL** for probe/ladder
(required deps from flake.nix). Content id → `sha256:…`; blobs as `.jxl`.

**Next:** `get_pixels`/`request_pixels`, then archive path.

Vendored: `third_party/sqlite/` only. No stb fallback.

## Commits

Author: Ingo Ruhnke \<grumbel@gmail.com\>  
Co-authored-by: Grok \<grok@x.ai\> when applicable  
License: GPL-3.0-or-later, REUSE SPDX headers on new files
