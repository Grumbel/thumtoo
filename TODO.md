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
- [x] Video stills model (`still_count` + `levels.frame_idx`, N=16, no storyboard)
- [x] Review response: status enum, schema_meta versioning, provisional-id promotion,
      archive security, concurrency/WAL, API executor contract, cache lifecycle named,
      directory snapshots kept (USB/NAS spin-up), tags schema note

## Next (Phase 0 — freeze before code)

- [ ] Freeze URI grammar + content-id / provisional-id constants
- [ ] Freeze status enum values and error_code conventions
- [ ] Freeze schema_version = 1 contents (ladder edges, default WebP quality)
- [ ] Freeze API threading contract (callback + caller executor)
- [ ] biltoo integration note: map `imageSizeForPath` / soft preview to Client API

## Phase 1

- [ ] `include/thumtoo/` public headers + SQLite content/locator spike
- [ ] `thumtoo-prepare` CLI skeleton
- [ ] Tag API sketch reconciled with dirtoo checksum tags

## Design open / discuss

- [ ] Archive on-demand vs batch coalescing details
- [ ] Optional convenience Location `//frame:N` (view only)
- [ ] Exact decompression size/ratio caps for archive security

## Later

- [ ] Archive TOC tables + libarchive path (Phase 2)
- [ ] Cache eviction / LRU / orphan sweep + `thumtoo-gc` / `thumtoo-status`
- [ ] Optional D-Bus daemon
- [ ] Optional grid tiles (galapix-style)
- [ ] Adaptive video frame count (8–64 → still_count)
- [ ] Animated video preview level (must-have; deferred until consumers exist)
