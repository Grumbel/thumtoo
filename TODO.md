# TODO / agent handoff

## Status (2026-10-05)

**Tip:** thumtoo-040.2-smooth-image-scale (stack on 040.1 / prior tip).

### 040.2 — Smooth image scaling for denser PDF tiles
`set_smooth_image_scaling(bool)` / `smooth_image_scaling()` process-wide.
MuPDF `fz_tune_image_scale` uses Mitchell for upscales when smooth (default
on). Page-level TLS key includes S/N so toggles do not reuse nearest buffers.

Hosts (biltoo View → Smooth Scaling) should call `set_smooth_image_scaling`
and invalidate live denser tile RAM.

### 040.1 — Image-heavy denser via full-page crop
(see prior commit)

### Bundle policy
Cumulative `.bundle`; no parallel histories.
