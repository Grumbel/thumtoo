# TODO / agent handoff

## Status (2026-09-30)

**Tip:** thumtoo-011.1-epub-layout-tile-key (on `b825e8d` + agent commits).

### 011.1
- EPUB page tiles/regions key on layout: region `"N|{layout_key}"`,
  content_id `sha256:…:page:N:epub:{layout_key}` (docs/EPUB.md).
- `purge_uri` for page-qualified URIs deletes that page's region tiles even
  when the blob is still shared (Shift+F5 works for multi-page docs).
- Stacks on origin tip `b825e8d` (store_.get / 010.5).

### Bundle policy
Every tip bundle = full stack from the work-line base (`b825e8d`) to HEAD.
