<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Architecture

[DESIGN.md](DESIGN.md) is normative for product rules. This file tracks the tree.

## Tree

```text
thumtoo/
  README.md DESIGN.md ARCHITECTURE.md TODO.md AGENTS.md
  REUSE.toml LICENSES/
  flake.nix CMakeLists.txt
  include/thumtoo/
    constants.hpp status.hpp types.hpp uri.hpp executor.hpp
    database.hpp client.hpp
  src/
    database.cpp uri.cpp client.cpp image.cpp schema.sql
  tools/
    thumtoo_status.cpp thumtoo_prepare.cpp
  tests/
    test_database.cpp test_client.cpp
  external/   # optional vendored sources (not system libs)
    # amalgamation (public domain)
```

## Build

```bash
nix develop          # optional
cmake -B build -GNinja
cmake --build build
ctest --test-dir build
./build/thumtoo-status --cache /path/to/cache summary
```

SQLite is **vendored** (amalgamation) so the library has no system sqlite
dependency. WAL is enabled at open.

## Dependencies

| Component | Status |
|-----------|--------|
| C++20, CMake ≥ 3.16 | required |
| SQLite3 | **required** (pkg-config / flake) |
| libvips + libjxl | **required** (flake.nix / pkg-config) |
| libarchive | **required** (TOC + member extract) |
| libarchive | Phase 2 |
| ffmpeg CLI (video stills) | Later |

## Consumers

- biltoo — first integration target
- dirtoo — optional later
- `thumtoo-status` — cache inspection (in-tree)
- `thumtoo-prepare` — prewarm (skeleton TBD)
