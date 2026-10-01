#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
# Offline smoke: corpus generate (if CORPUS_GEN set) + golden decode + gp-tile + optional thumtoo-bench.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORPUS_OUT="${CORPUS_OUT:-/tmp/pixel-bench-smoke-corpus}"
BUILD_DIR="${THUMTOO_BUILD_DIR:-/tmp/thumtoo-build}"
GEN="${CORPUS_GEN:-}"
if [[ -z "$GEN" && -f "${ROOT}/../pixel-bench-corpus/generators/gen_synthetic.py" ]]; then
  GEN="${ROOT}/../pixel-bench-corpus/generators/gen_synthetic.py"
fi

echo "== bench_smoke: corpus =="
if [[ -n "$GEN" && -f "$GEN" ]]; then
  python3 "$GEN" --out "$CORPUS_OUT"
else
  echo "CORPUS_GEN not set and sibling pixel-bench-corpus not found; using CORPUS_OUT=$CORPUS_OUT"
fi
JPEG=$(ls "$CORPUS_OUT"/synthetic/jpeg/synth_1920x1080_q90.jpg 2>/dev/null || true)
if [[ -z "$JPEG" ]]; then
  echo "missing $CORPUS_OUT/synthetic/jpeg/synth_1920x1080_q90.jpg" >&2
  exit 1
fi

need() {
  local b="$1"
  if [[ ! -x "$b" ]]; then
    echo "missing executable: $b (build tools first)" >&2
    exit 1
  fi
}

DECODE="${BUILD_DIR}/thumtoo-microbench-decode"
GPTILE="${BUILD_DIR}/thumtoo-gp-tile"
BENCH="${BUILD_DIR}/thumtoo-bench"
# Fallback to PATH
command -v thumtoo-microbench-decode >/dev/null 2>&1 && DECODE=$(command -v thumtoo-microbench-decode)
command -v thumtoo-gp-tile >/dev/null 2>&1 && GPTILE=$(command -v thumtoo-gp-tile)
command -v thumtoo-bench >/dev/null 2>&1 && BENCH=$(command -v thumtoo-bench)

need "$DECODE"
need "$GPTILE"

echo "== microbench-decode =="
"$DECODE" --repeat 2 "$JPEG"

echo "== gp-tile =="
"$GPTILE" --repeat 2 --quality 80 "$JPEG"

if [[ -x "$BENCH" ]]; then
  echo "== thumtoo-bench --json =="
  "$BENCH" --json --ladder 256 --tile-cell --cache /tmp/thumtoo-bench-smoke-cache "$JPEG" | head -c 2000
  echo
else
  echo "skip thumtoo-bench (not built)"
fi

echo "ok: bench_smoke finished"
