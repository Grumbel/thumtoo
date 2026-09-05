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
    constants.hpp   # Phase 0 freezes
    status.hpp      # ContentStatus enum
    types.hpp
    database.hpp    # SQLite index (spike)
  src/
    database.cpp
    schema.sql      # reference copy of DDL
  tools/
    thumtoo_status.cpp   # inspect cache
  tests/
    test_database.cpp
  third_party/sqlite/    # amalgamation (public domain)
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
| SQLite amalgamation | vendored |
| Image decode / WebP encode | not yet |
| libarchive | Phase 2 |
| ffmpeg CLI (video stills) | Later |

## Consumers

- biltoo — first integration target
- dirtoo — optional later
- `thumtoo-status` — cache inspection (in-tree)
- `thumtoo-prepare` — prewarm (skeleton TBD)
