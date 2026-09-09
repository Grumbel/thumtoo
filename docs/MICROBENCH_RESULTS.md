# Microbench results (Pillow / system unzip)

Environment: PIL decode uses libjpeg DCT scaling via `Image.draft`.
Not identical to `vips_jpegload(shrink=N)` but same 1/2/4/8 factors.

## JPEG decode

| file | MP | size_only ms | full ms | draft/2 ms | × | draft/4 | × | draft/8 | × |
|------|----|--------------|---------|------------|---|--------|---|---------|---|
| synth_1920x1080.jpg | 2.1 | 0.03 | 22.4 | 17.1 | 1.3 | 16.2 | 1.4 | 13.4 | 1.7 |
| synth_3840x2160.jpg | 8.3 | 0.03 | 85.9 | 65.6 | 1.3 | 60.6 | 1.4 | 53.5 | 1.6 |
| synth_7680x4320.jpg | 33.2 | 0.03 | 359.5 | 275.8 | 1.3 | 248.4 | 1.4 | 210.8 | 1.7 |
| synth_800x600.jpg | 0.5 | 0.03 | 2.3 | 1.6 | 1.4 | 1.1 | 2.0 | 1.0 | 2.3 |

## Interpretation (2026-09-09, this environment)

- **Size-only is essentially free** (~0.03 ms) vs full decode (2–360 ms on this corpus).
- Pillow `Image.draft` (libjpeg DCT scale) only ~**1.3–2.3×** faster than full load
  on these files — Huffman/entropy decode still dominates; pixel IDCT shrink helps
  less than a naive pixel-count argument suggests. Expect **Vips `jpegload(shrink=N)`**
  to be in a similar ballpark (measure in-tree next).
- PSNR of draft vs high-quality downsample is modest (22–32 dB) — fine for
  coarse tiles / overview, not for scale-0 HQ.
- PIL `thumbnail(32)` on 8K is **~207 ms** (full decode then resize). If LQIP used
  this pattern it would be slow; **Vips `vips_thumbnail` uses shrink-on-load for JPEG**
  and should be much faster — must confirm with `thumtoo`/vips microbench under nix.
- ZIP (stored): random member via Python zipfile is cheap; `unzip -p` process
  spawn is slower than in-process (~2.6 ms vs 0.2 ms). libarchive sequential walk
  cost will dominate on large RAR/solid archives (not in this corpus yet).

## Gaps still to measure under full thumtoo/vips build

1. `vips_jpegload` shrink=1/2/4/8 wall + RSS
2. `vips_thumbnail` edge 32/256 vs draft
3. EXIF embedded thumb extract + decode
4. `build_tile_cell` file path vs buffer with/without decode_cache_key
5. libarchive vs unzip vs unrar on large RAR
6. Warm `get_tile` latency (SQLite + JPEG decode of 256² only)

