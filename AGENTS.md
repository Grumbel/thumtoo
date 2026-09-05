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

## Handoff

## Status (2026-09-06)

**Phase 1 done:** Client, prepare/status CLIs, system SQLite, libvips+JXL ladder,
get/request size & pixels, biltoo INTEGRATION.md. `thumtoo-prepare` reports
per-job progress on stderr (`--quiet` to suppress).
flake ships util-linux so gio `mount.pc` is on PKG_CONFIG_PATH.

**Phase 2 in progress:** archive TOC + **member extract** for
`file:///…//archive:member` → sha256 identity + JXL ladder (same as plain files).
Caps: 512 MiB member uncompressed.

**Note:** prepare re-queues non-ready locators; drain claims jobs before dequeue.

**Next:** archive batch coalesce (one open, many members); prepare expands
archive image members; tag API vs dirtoo; biltoo wiring.

Ladder blobs: `blobs.sqlite` (not a directory of files). System SQLite via pkg-config/Nix. Optional vendored code under `external/`.

## Commits

Author: Ingo Ruhnke \<grumbel@gmail.com\>  
Co-authored-by: Grok \<grok@x.ai\> when applicable  
License: GPL-3.0-or-later, REUSE SPDX headers on new files
