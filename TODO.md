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
- [x] Video stills model:
  - fixed N=16 initially; no cached storyboard (apps compose grids)
  - `levels` PK `(content_id, max_edge, frame_idx)` — 0 = poster/image, 1..N stills
  - `content.still_count` = planned N (NULL for pure images); no separate frame table
  - frames addressed via API / frame_idx, not special public URLs

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
- [ ] Optional convenience Location `//frame:N` (view only, same content_id)

## Later

- [ ] Archive TOC tables + libarchive path (Phase 2)
- [ ] Optional D-Bus daemon
- [ ] Optional grid tiles (galapix-style)
- [ ] Adaptive video frame count (duration-based 8–64; write to still_count)
- [ ] Animated video preview level (must-have; deferred until consumers exist)
