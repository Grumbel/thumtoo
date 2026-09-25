# TODO / agent handoff

## Status (2026-09-25)

**Tip: thumtoo-341.2-export-preserve-page-pipe** (base `75b1f60`).

### 341.2 — thumtoo-export bare path + //page:
Bare `/path/doc.pdf//page:N` was fed through `lexically_normal()`, which
collapsed `//page:` → `/page:` and broke size probe (`no size for …/page:N`).

Match `thumtoo-tile` normalize: split path vs pipe suffix, then
`file_uri_from_path(abs) + suffix`. Also accept `-o` directory (append
default PNG name).

### 341.1 — pdf_page_size_at_scale negative scales
Regression from floor-half: `dim_at_tile_scale` is a no-op for `scale <= 0`.
- `s >= 0`: successive floor-half
- `s < 0`: exact `L * 2^{-s}`

### Apply
```bash
git pull --ff-only …/thumtoo-341.2-export-preserve-page-pipe-75b1f60.bundle HEAD
```
