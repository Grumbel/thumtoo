<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO — thumtoo

## Done

- [x] Bootstrap documentation repo (README, DESIGN, ARCHITECTURE, AGENTS)
- [x] Link biltoo, dirtoo, galapix, dirtoo-py
- [x] Normative rules: cache-first browse, XDG-only, no source pollution, hash identity
- [x] Location URI form; video stills (`still_count` + `frame_idx`); review response
- [x] README scope row: directory snapshots vs dirtoo live listing
- [x] Phase 0 constants written down (ladder edges, JXL q=80, archive caps, WAL)

## Phase 0 remaining (thin)

- [ ] Encode constants as implementable C++/header names in the spike
- [ ] biltoo integration note: map `imageSizeForPath` / soft preview to Client API

## Phase 1

- [x] `include/thumtoo/` public headers + SQLite content/locator spike
- [x] CMake + flake.nix + `nix develop` shell
- [x] `thumtoo-status` CLI (inspect cache summary/locators/content)
- [x] Database open/migrate tests
- [x] `thumtoo-prepare` CLI skeleton (register paths + schedule probe)
- [x] Client API: get_size / get_meta / request_size + single worker queue
- [x] URI helpers (file:/// , //archive detection)
- [x] Image pipeline via libvips + JPEG-XL ladder (required; flake.nix)
- [x] SHA-256 content id promotion
- [ ] get_pixels / request_pixels reading levels
- [ ] Tag API sketch reconciled with dirtoo checksum tags

## Design open / discuss

- [ ] Archive on-demand vs batch coalescing details
- [ ] Optional convenience Location `//frame:N` (view only)
- [ ] Whether archive caps need per-format overrides

## Later

- [ ] Archive TOC tables + libarchive path (Phase 2)
- [ ] Cache eviction / LRU / orphan sweep + `thumtoo-gc` / `thumtoo-status`
- [ ] Optional D-Bus daemon
- [ ] Optional grid tiles (galapix-style)
- [ ] Adaptive video frame count (8–64 → still_count)
- [ ] Animated video preview level (must-have; deferred until consumers exist)
