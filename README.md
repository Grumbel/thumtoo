<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# thumtoo

**thumtoo** is a persistent **media index and display-pixel ladder** for the
\*too family of apps. It remembers native image size, archive tables of
contents, and multi-resolution preview pixels so viewers do not re-probe and
re-decode the same files on every session.

It is a **library first** (optional D-Bus service later). It is **not** an
image viewer and **not** a full file manager.

**Cache-first, source-read-only:** all durable data lives under
`$XDG_CACHE_HOME/thumtoo/`. Source trees are never modified (no xattrs, no
sidecars). Browse paths can run from cache alone until detail or refresh needs
source I/O. Content hashes couple previews and tags to file bytes across
renames; http(s) URLs are a later extension.

| Concern | thumtoo | Not thumtoo |
|---------|---------|-------------|
| Native width × height | Yes (SQLite) | — |
| Archive TOC + member identity | Yes | — |
| Fixed long-edge previews (ladder) | Yes | — |
| Video stills (frame ladder) | Yes (`still_count` + `frame_idx`) | Storyboard composition (apps) |
| Directory listing **snapshots** | Durable cache for cold/USB-safe folder open | Live listing UX, refresh policy (dirtoo) |
| Session edit identity (crop, flips) | — | App session (`SessionImageId` in biltoo) |
| Desktop file-manager icons | Optional consumer | Freedesktop Thumbnailer1 (dirtoo client) |
| Zoomable workspace UI | — | biltoo / galapix |

## Related projects

| Project | Role | Link |
|---------|------|------|
| **biltoo** | Qt image viewer (Image / Gallery / Workspace); primary consumer of size + preview ladder | <https://github.com/Grumbel/biltoo> |
| **dirtoo** | Modular file manager; checksum SQLite, MediaMetaCache, archive TOC, Thumbnailer1 client | <https://github.com/Grumbel/dirtoo> |
| **galapix** | Zoomable collection viewer; multi-scale **tile** cache in SQLite (historical pyramid model) | <https://github.com/Galapix/galapix> |
| **dirtoo-py** | Python/PyQt prototype (behavioral reference for dirtoo) | <https://github.com/Grumbel/dirtoo-py> |

Sister docs inside those trees (when present): biltoo `DOMAIN.md` / `IDENTITY.md` /
`SLIDESHOW.md`; dirtoo `ARCHITECTURE.md`; galapix `README` (tile DB, `prepare` /
`thumbgen`).

## Status

**Phase 1 spike in progress.** Design is frozen (see [DESIGN.md](DESIGN.md));
library opens a WAL SQLite index under a cache root, applies schema_version 1,
and ships `thumtoo-status` for inspection. Build with CMake or `nix develop`.
Workers, encode ladder, and `thumtoo-prepare` are next. Details in [TODO.md](TODO.md).

## Name

**thumtoo** follows biltoo / dirtoo. “Thumb” means *display proxy* (size index,
preview ladder, archive listing helpers)—not only 128² file-manager icons.

## Build

```bash
cmake -B build && cmake --build build && ctest --test-dir build
./build/thumtoo-status --cache ~/.cache/thumtoo summary
```

Use `nix develop` for the toolchain: **libvips** and **libjxl** are required (no stb/codec fallbacks). See `flake.nix`.

## License

GPL-3.0-or-later. See [LICENSES/GPL-3.0-or-later.txt](LICENSES/GPL-3.0-or-later.txt)
and [REUSE.toml](REUSE.toml).
