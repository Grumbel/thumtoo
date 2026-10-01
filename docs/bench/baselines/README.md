<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Benchmark baselines

Store golden-tool JSON snapshots per machine class for regression detection.

## Layout

```
docs/bench/baselines/<machine-class>/
  microbench-decode.json
  gp-tile-jpeg.json
  gp-archive-libarchive.json
```

## Capture

Preferred:

```bash
# after building tools and generating corpus (or nix build github:Grumbel/benchtoo#corpus-smoke)
CORPUS=$(nix build --print-out-paths github:Grumbel/benchtoo#corpus-smoke)
MACHINE=nixx86-ref CORPUS="$CORPUS" ./tools/capture_baselines.sh
```

Manual:

```bash
thumtoo-microbench-decode --json --repeat 5 FILE > microbench-decode.json
thumtoo-gp-tile --json --codec jpeg --quality 80 FILE > gp-tile-jpeg.json
thumtoo-gp-archive --json --backend libarchive ARCHIVE > gp-archive-libarchive.json
```

## Compare

```bash
python3 tools/compare_bench_json.py \
  --baseline docs/bench/baselines/nixx86-ref/gp-tile-jpeg.json \
  --current /tmp/gp-tile.json \
  --tolerance-pct 25
```

Exit code 0 = within tolerance; 1 = regression; 2 = usage/schema error.

Tolerances are relative on timing fields (`*_ms`, `encode_ms`, `decode_ms`).
Byte counts are compared with a smaller default tolerance (5%).

See `example/` for schema-only placeholders (not for CI gates).
