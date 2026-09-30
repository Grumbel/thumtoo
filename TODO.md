# TODO / agent handoff

## Status (2026-09-30)

**Tip:** thumtoo-013.1-get-size-light (on `b825e8d` + agent stack).

### 013.1
- `Client::get_size` is size-only Store read: locator → media → region dims.
  No `list_tile_scales`, no source open, no LQIP/EMB.
- `meta_from_store` no longer opens PDF/DjVu/EPUB when region size is missing
  (cache-only contract).

### Prior
- 012.1 activity orphan drop

### Bundle policy
Work-line base: `b825e8d`. Full stack in each tip bundle.

### Next
- Optional bulk SQL for all page sizes of one document (further TTFP cut).
