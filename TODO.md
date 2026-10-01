<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO / agent handoff

## Status (2026-10-01)

**Tip:** thumtoo-039.1-cli-csv-json (base `e442169` + agent commits; cumulative).

### 039.1 — CLI / output overhaul of the bench tools
Design and usage: docs/BENCHMARK_KIT.md §6.6. Summary:
- Shared layers in `tools/golden/`: `gp_cli.hpp` (declarative options →
  parsing, strict validation, --help/--version, directory expansion,
  progress on a tty), `gp_output.hpp` (text tables, CSV writer, mode
  selection), `gp_json.hpp` (streaming JSON writer). Unit tests `gp_json`,
  `gp_cli`, `gp_output`, extended `gp_verdict` (Aggregator).
- gp-archive, gp-tile, microbench-decode: many files/dirs per run
  (`-r`), human-readable default, `--csv` / `--json` (+ `--no-header`),
  cross-input summary for comparisons, in-band failures (exit 1, batch
  continues). thumtoo-bench: same CLI layer, `--csv`, structured
  `counters` in JSON, phase table.
- JSON: single-input documents keep their schema-1 keys (additive
  `status`); several inputs give `{"kind":"batch",results,aggregate}`.
  Checked-in baselines still compare clean (verified for gp-tile-jpeg,
  gp-archive-libarchive, microbench-decode).
- compare_bench_json: batch documents; `--fail-on-winner-change` gates on
  the overall/aggregate winner only (per-metric winners flip on near-ties).
- Bugs found on the way: microbench-decode timed failed/truncated decodes
  as ~0 ms; libunarr logged on every timed call. Both fixed.
- `tests/test_bench_tools_cli.py` (ctest `bench_tools_cli`) runs all four
  binaries end to end. flake: python3 added to nativeBuildInputs so the
  test is registered in nix builds.
- BREAKING for scripts: microbench-decode's default output used to be CSV;
  it is now a table (`--csv` gives CSV, with different columns: path as
  given, width/height/status/reason added). `--jobs -1`, `--ladder -5` and
  similar invalid values are now usage errors (exit 2). thumtoo-bench
  exits 1 when no image was found.
- **Not verified under nix / with the real library.** Built with g++
  against Ubuntu libs (vips 8.15, libarchive 3.7, libunarr 1.0.1); no
  CMake in the sandbox, so CMakeLists.txt/flake.nix edits are reviewed by
  eye only, and thumtoo_bench.cpp was run against a stub of the Client
  API (it compiles against the real headers). Run `nix flake check` and
  `ctest` first.
- Not touched: the production CLIs (thumtoo-tile/-export/-status/-prepare/
  -gc/-xdg-thumb/-archive); they have their own man pages and could adopt
  gp_cli.hpp (it would belong outside tools/golden/ then).

### Follow-ups (CLI)
- gp-tile: no cross-image timing interleave; `--sweep` is text-only.
- A `--format` alias or `--output FILE` if shells without `>` matter.
- Move gp_cli/gp_output out of tools/golden/ if production tools adopt them.

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
