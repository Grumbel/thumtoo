<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO / agent handoff

## Status (2026-10-01)

**Tip:** thumtoo-037.1-auto-compare (base `e442169` + 7 agent commits).

### 037.1 — automatic comparisons (who wins)
- `gp-archive --backend all|a,b`: Backend interface, magic-byte format sniff;
  unarr now also reads ZIP/TAR (7z if libunarr has the SDK). Verification
  before timing; backends must agree or the verdict is refused.
- `gp-tile --codec all|a,b`: PSNR per row; codecs judged at matched quality
  vs `--reference jpeg:80` with bisection refinement; lossy-source warning.
- `tools/golden/gp_verdict.hpp` (+ ctest `gp_verdict`), `gp_common.hpp`.
- Fixed: gp-tile printed 0 bytes / 0 ms (exit 0) for codecs libvips could
  not encode (AVIF without AV1 encoder) — would have "won" a comparison.
- Wired into checks.bench-smoke-lite, capture_baselines.sh, bench_smoke.sh.
- Verified on Ubuntu 24.04 (vips 8.15, libarchive 3.7, libunarr 1.0.1) with
  benchtoo generator output. **Not run under nix** — run `nix flake check`.

Sandbox observations (not a reference machine; indicative only):
- photo 1920x1080 PNG, matched to jpeg:80 PSNR (35.9 dB): bytes — jxl q70
  40.9 KB < webp q78 57.4 KB < jpeg 188.7 KB; encode — jpeg ~14x faster than
  webp; decode — jpeg ~2.8x faster than jxl. Overall (geomean): jpeg.
- ZIP/TAR: unarr ~2–3.5x faster TOC; extract-all a tie. unarr 1.0.1 Ubuntu
  build lacks 7z.

### Follow-ups (comparison)
- Encoder effort axis (JXL e1–3, WebP effort, AVIF speed) as extra variants;
  current numbers use libvips defaults, which penalize JXL/AVIF encode.
- Better quality metric than PSNR (SSIMULACRA2 / butteraugli) for matching.
- unarr `ar_parse_entry_for` (true random access on ZIP central directory)
  as a separate "unarr-seek" variant; needs RAR4 solid fixtures to verify.
- compare_bench_json keys by name (codec/backend) instead of list index so
  `*-compare.json` could become regression gates.
- Optional weights for the overall score (e.g. decode-heavy for tiles).

### 036.1
- Install `thumtoo-gp-archive` (was built but missing from install TARGETS → tools-bin check fail)

### Prior
- 035 capture_baselines; 034 corpus-smoke; …

### Companion
https://github.com/Grumbel/benchtoo (tip through 009.3-no-pdf2djvu)

### Next
- Real machine baselines; push benchtoo tip to GitHub
