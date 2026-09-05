<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Architecture (planned)

Until code lands, treat [DESIGN.md](DESIGN.md) as normative.

## Planned tree

```text
thumtoo/
  README.md
  DESIGN.md
  ARCHITECTURE.md
  REUSE.toml
  LICENSES/
  include/thumtoo/     # public C++ headers (phase 1)
  src/                 # sqlite, workers, archive, encode
  tools/               # thumtoo-prepare, thumtoo-status
  tests/
  dbus/                # phase 3
```

## Dependencies (expected)

- C++20 or C++23
- SQLite3
- Image decode/encode: prefer existing stack from consumers (Qt image I/O, VIPS,
  or a small dedicated path); exact choice deferred to phase 1 spike
- libarchive for archive TOC/members (align with dirtoo-archive)
- Video stills: ffmpeg CLI (subprocess) by default; optional in-process libav later
- Optional: JPEG-XL libraries if codec enabled

## Consumers

- [biltoo](https://github.com/Grumbel/biltoo) — first integration target
- [dirtoo](https://github.com/Grumbel/dirtoo) — optional rich previews later
- CLI tools in-tree for prewarm and cache inspection
