<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO / agent handoff

## Status (2026-10-01)

**Tip:** thumtoo-038.1-compare-effort-seek (base `e442169` + 12 agent commits).

### 038.1 — comparison follow-ups
- gp-tile effort variants: `--codec all,webp@e0,jxl@e1,jxl@e3` (Variant =
  codec + effort; JSON adds `variant`/`effort`; `--reference VARIANT:Q`).
- gp-archive `unarr-seek`: TOC-cached random member access via
  `ar_parse_entry_at`; verification now covers every timed member.
- Verdict robustness: noise-aware ties (fastest run vs best median) and
  interleaved backend timing in gp-archive (run order skewed 12–15%).
- compare_bench_json: identity-keyed lists, verdict winner-change report
  (`--fail-on-winner-change`), self-test in checks.baseline-compare-tool.
- capture_baselines: codec snapshot includes webp@e0, jxl@e1, jxl@e3.
- Same sandbox caveat as 037.1: Ubuntu libs, **not run under nix**.

Sandbox observations (indicative only):
- photo 1920x1080 PNG at jpeg:80 PSNR: jxl@e3 q79 56.8 KB (3.3x < jpeg) for
  ~50 ms / 16 cells encode (jpeg ~6 ms); jxl@e1 67.7 KB / 45 ms; webp@e0
  96.7 KB / 24 ms. Default jxl (e7) 40.9 KB / 215 ms. Decode: jpeg ~2.5x
  faster than any JXL variant. Overall still jpeg; bytes winner jxl.
- 160-member stored CBZ: unarr-seek extract_last 0.06 ms vs 0.51 (unarr
  walk) vs 1.19 (libarchive); TAR 0.008 vs 0.17 / 0.83. TOC: unarr ~2x
  faster than libarchive; extract-all a tie.

### Follow-ups (comparison)
- gp-tile: re-time the matched rows interleaved across variants for the
  verdict (sweep timings are sequential; gaps are mostly large).
- Better quality metric than PSNR (SSIMULACRA2 / butteraugli) for matching.
- RAR4 solid fixtures to exercise unarr/unarr-seek on solid archives.
- Optional weights for the overall score (e.g. decode-heavy for tiles).
- Production (needs discussion): the dispatcher sends ZIP/CBZ and TAR to
  libarchive, which walks from the start for every member. Routing Random
  archives through unarr with cached TOC offsets would be 8–20x faster on
  late members per the numbers above; weigh against unarr's format coverage
  (no RAR5, 7z build-dependent) and its smaller user base.

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

### 036.1
- Install `thumtoo-gp-archive` (was built but missing from install TARGETS → tools-bin check fail)

### Prior
- 035 capture_baselines; 034 corpus-smoke; …

### Companion
https://github.com/Grumbel/benchtoo (tip through 009.3-no-pdf2djvu)

### Next
- Real machine baselines; push benchtoo tip to GitHub
