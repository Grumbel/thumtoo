<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO — thumtoo

## Done

- [x] Bootstrap documentation repo (README, DESIGN, ARCHITECTURE, AGENTS)
- [x] Link biltoo, dirtoo, galapix, dirtoo-py
- [x] Normative rules: cache-first browse, XDG-only, no source pollution, hash identity
- [x] Adopt dirtoo/Galapix Location URI form for archives (`file:///…//archive:member`)
- [x] Video stills policy: adaptive count (8–64, ~8 s target interval) + storyboard

## Next

- [ ] Phase 0: freeze URI + content-id rules (implementable constants)
  - URI grammar: file:/// + //archive: pipe (nested ok); no JAR `!` as primary
  - Content-id: sha256: when known; path+size+mtime fingerprint for outer
- [ ] Phase 1: `include/thumtoo/` public headers + SQLite content/locator spike
- [ ] `thumtoo-prepare` CLI skeleton
- [ ] biltoo integration note: map `imageSizeForPath` / soft preview to Client API
- [ ] Decide default ladder edges and WebP quality
- [ ] Tag API sketch aligned with dirtoo checksum tags

## Design open / discuss

- [ ] Archive on-demand vs batch: coalesce requests that share an outer archive;
      prefer sequential walk for generate; nested via Location pipe
- [ ] Video worker isolation: default to subprocess (ffmpeg CLI) for crash
      safety; optional in-process libav later once timeouts/sandbox exist
- [ ] Exact storyboard grid policy and storage (single tiled image vs N stills)

## Later

- [ ] Archive TOC tables + libarchive path (Phase 2)
- [ ] Optional D-Bus daemon
- [ ] Optional grid tiles (galapix-style)
- [ ] Animated video preview level (must-have; deferred until consumers exist)
