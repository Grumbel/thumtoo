#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare current golden-tool JSON against a baseline snapshot.

Timing keys (name ends with _ms or is encode_ms/decode_ms) use --tolerance-pct
(default 25). Byte-like keys use --bytes-tolerance-pct (default 5).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def walk(obj: Any, prefix: str = "") -> dict[str, float]:
    out: dict[str, float] = {}
    if isinstance(obj, dict):
        for k, v in obj.items():
            p = f"{prefix}.{k}" if prefix else k
            if isinstance(v, (int, float)) and not isinstance(v, bool):
                out[p] = float(v)
            else:
                out.update(walk(v, p))
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            out.update(walk(v, f"{prefix}[{i}]"))
    return out


def is_timing(key: str) -> bool:
    k = key.rsplit(".", 1)[-1]
    return k.endswith("_ms") or k in ("encode_ms", "decode_ms", "median_ms")


def is_bytes(key: str) -> bool:
    k = key.rsplit(".", 1)[-1]
    return "byte" in k.lower() or k in ("bytes_total", "jpeg_bytes", "members")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--baseline", type=Path, required=True)
    ap.add_argument("--current", type=Path, required=True)
    ap.add_argument("--tolerance-pct", type=float, default=25.0)
    ap.add_argument("--bytes-tolerance-pct", type=float, default=5.0)
    args = ap.parse_args()

    base = json.loads(args.baseline.read_text(encoding="utf-8"))
    cur = json.loads(args.current.read_text(encoding="utf-8"))
    bf = walk(base)
    cf = walk(cur)

    regressions = []
    missing = []
    for key, bv in bf.items():
        if key not in cf:
            if is_timing(key) or is_bytes(key):
                missing.append(key)
            continue
        cv = cf[key]
        if bv == 0:
            if cv != 0 and (is_timing(key) or is_bytes(key)):
                regressions.append((key, bv, cv, float("inf")))
            continue
        pct = abs(cv - bv) / abs(bv) * 100.0
        lim = args.tolerance_pct if is_timing(key) else (
            args.bytes_tolerance_pct if is_bytes(key) else None
        )
        if lim is None:
            continue
        # Only flag slowdowns for timings (current much larger)
        if is_timing(key) and cv > bv and pct > lim:
            regressions.append((key, bv, cv, pct))
        elif is_bytes(key) and pct > lim:
            regressions.append((key, bv, cv, pct))

    if missing:
        print("missing keys in current:", ", ".join(missing), file=sys.stderr)
    if regressions:
        print("regressions:", file=sys.stderr)
        for key, bv, cv, pct in regressions:
            print(f"  {key}: baseline={bv:.4g} current={cv:.4g} delta={pct:.1f}%",
                  file=sys.stderr)
        return 1
    print("ok: within tolerance")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
