# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-252-store-only-tags-gate-mirror.** Store-only tags; gate dual-write.
Prior: **251**.

### Change
- `mirror_probe_to_store` no-ops under STORE_ONLY / without legacy
- Tags get/add/remove work on Store when no legacy Database
- `test_store_only` covers tag round-trip

### Next
- Host soak of THUMTOO_STORE_ONLY=1
- Default STORE_ONLY after soak; delete dual-write helpers

### Apply
```bash
git pull /path/to/thumtoo-252-store-only-tags-gate-mirror.bundle HEAD
```

### Done criteria
- [x] Bundle **252**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-251-store-only-http.** Store-only HTTP image URIs.
Prior: **250**.

### Change
- Store-only probe/pixels/tiles for HTTP(S) image URLs (fetch cache + Store)

### Next
- Drop dual-write helpers (`mirror_*`) under STORE_ONLY / eventually always
- Host soak of THUMTOO_STORE_ONLY=1

### Apply
```bash
git pull /path/to/thumtoo-251-store-only-http.bundle HEAD
```

### Done criteria
- [x] Bundle **251**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-250-store-only-djvu-epub.** Store-only DjVu + EPUB pages.
Prior: **249**.

### Change
- Store-only probe/pixels/tiles for DjVu and EPUB page URIs
- `meta_from_store` reports layout size for djvu/epub pages

### Next
- HTTP Store-only probe/pixels
- Drop dual-write helpers when hosts use STORE_ONLY

### Apply
```bash
git pull /path/to/thumtoo-250-store-only-djvu-epub.bundle HEAD
```

### Done criteria
- [x] Bundle **250**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-249-store-only-pdf-archive.** Store-only PDF + archive member paths.
Prior: **248**.

### Change
- Store-only probe for PDF pages and archive image members
- Session pixels + tile cells for PDF (live) and archive members
- `meta_from_store` PDF pages report layout size

### Next
- DjVu/EPUB Store-only parity
- Drop dual-write helpers when hosts use STORE_ONLY

### Apply
```bash
git pull /path/to/thumtoo-249-store-only-pdf-archive.bundle HEAD
```

### Done criteria
- [x] Bundle **249**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-248-store-only-tiles.** Store-only file:// tile encode.
Prior: **247**.

### Change
- `handle_ensure_tiles_store_only`: single cell + pyramid for plain files
- Tiles written via `store_tiles` → Store bulk; `test_store_only` covers request_tile

### Next
- Store-only PDF/archive probe + tiles
- Drop dual-write helpers when hosts use STORE_ONLY

### Apply
```bash
git pull /path/to/thumtoo-248-store-only-tiles.bundle HEAD
```

### Done criteria
- [x] Bundle **248**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-247-store-only-no-legacy.** STORE_ONLY skips legacy open entirely.
Prior: **246**.

### Change
- `THUMTOO_STORE_ONLY=1`: no Database/BlobStore; `has_legacy()` / `db()` throws
- Store-first probe + session EnsurePixels for plain `file://` images
- Guards on get_size/meta/tile/pixels/locators; `test_store_only` updated

### Next
- Store-only tile encode; PDF/archive probe parity
- Drop dual-write helpers when hosts stay on STORE_ONLY

### Apply
```bash
git pull /path/to/thumtoo-247-store-only-no-legacy.bundle HEAD
```

### Done criteria
- [x] Bundle **247**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-246-store-only-ephemeral.** THUMTOO_STORE_ONLY in-memory legacy.
Prior: **245**.

### Change
- `Database::open_memory` / `BlobStore::open_memory`
- `store_only_mode()` / Client::open uses memory legacy when STORE_ONLY=1
- Durable Store remains on disk; no `legacy/` files
- `test_store_only`

### Next
- True Store-only: drop memory legacy; Store-first probe/pixels
- Hosts may try `THUMTOO_STORE_ONLY=1` with tiles-first

### Apply
```bash
git pull /path/to/thumtoo-246-store-only-ephemeral.bundle HEAD
```

### Done criteria
- [x] Bundle **246**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-245-store-root-default.** Top-level Store layout is the default.
Prior: **244**.

### Change
- `store_root_layout_enabled()` defaults **on**; opt out with `THUMTOO_STORE_ROOT=0`
- `test_store_root`: default + opt-out cases; `test_client` uses `legacy_db_root`
- HOST_CUTOVER / API_MIGRATION: soak-confirmed, default on

### Next
- Store-only Client (skip legacy Database/BlobStore open)
- Hosts: drop explicit `THUMTOO_STORE_ROOT=1` if set

### Apply
```bash
git pull /path/to/thumtoo-245-store-root-default.bundle HEAD
```

### Done criteria
- [x] Bundle **245**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-244-tiles-first-no-full-levels.** Tiles-first skips full_native levels too.
Prior: **243**.

### Change
- `tiles_only_mode()`: no durable put_level for soft *or* full_native
- Full still replies via session encode / TileSynth (full-from-tiles)
- HOST_CUTOVER / API_MIGRATION updated

### Next
- Store-only Client (legacy open optional / skipped)
- Soak STORE_ROOT with biltoo

### Apply
```bash
git pull /path/to/thumtoo-244-tiles-first-no-full-levels.bundle HEAD
```

### Done criteria
- [x] Bundle **244**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-243-full-from-tiles.** Full EnsurePixels accepts TileSynth when adequate.
Prior: **242**.

### Change
- EnsurePixels TILES_COVER path applies to full_native (level_adequate guards edge)
- Final Full reply may use TileSynth; session encode reply also for full_native
- HOST_CUTOVER / API_MIGRATION: full-from-tiles noted

### Next
- Store-only Client (no legacy open)
- Soak STORE_ROOT with biltoo

### Apply
```bash
git pull /path/to/thumtoo-243-full-from-tiles.bundle HEAD
```

### Done criteria
- [x] Bundle **243**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-242-tiles-first-tests-store-root.** Tiles-first tests + STORE_ROOT test/docs.
Prior: **241**.

### Change
- `test_client` / `test_soft_ladder`: align with tiles-first default (SOFT_LEVELS=1 for durable soft)
- `test_store_root`: fresh STORE_ROOT layout, dual-path migrate, default layout unchanged
- `docs/DATABASE.md`: status implemented (dual-path; Store-only pending)
- API_MIGRATION / HOST_CUTOVER: note `test_store_root` coverage

### Next
- Store-only Client (no legacy open) — needs full_native story without levels
- Soak STORE_ROOT with biltoo hosts

### Apply
```bash
git pull /path/to/thumtoo-242-tiles-first-tests-store-root.bundle HEAD
```

### Done criteria
- [x] Bundle **242**
- [ ] client + soft_ladder + store_root tests pass in CI / nix check

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-241-status-store.** thumtoo-status summary includes Store counts.
Prior: **240**.

### Change
- summary mode opens redesign Store and prints blobs/locators/media/tiles/…

### Next
- Soak STORE_ROOT with biltoo
- Store-only Client

### Apply
```bash
git pull /path/to/thumtoo-241-status-store.bundle HEAD
```

### Done criteria
- [x] Bundle **241**

---


# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-240-layout-helpers.** Shared layout.hpp; tools honor STORE_ROOT.
Prior: **239**.

### Change
- `layout.hpp` / `layout.cpp`: store_root_layout_enabled, legacy_db_root,
  redesign_store_root, migrate_dual_path_to_store_root
- Client::open uses shared helpers
- thumtoo-gc / thumtoo-status open legacy via legacy_db_root
- status summary prints layout + store_root

### Next
- Soak STORE_ROOT with biltoo
- Store-only Client

### Apply
```bash
git pull /path/to/thumtoo-240-layout-helpers.bundle HEAD
```

### Done criteria
- [x] Bundle **240**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-239-fix-store-root-dbg.** Forward-declare dbg for STORE_ROOT migrate.
Prior: **238**.

### Change
- Fix compile: `dbg` used before definition in migrate helpers

### Apply
```bash
git pull /path/to/thumtoo-239-fix-store-root-dbg.bundle HEAD
```

### Done criteria
- [x] Bundle **239**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-238-store-root-migrate.** Auto-migrate dual-path → STORE_ROOT on open.
Prior: **237**.

### Change
- On `THUMTOO_STORE_ROOT=1`, move legacy schema `<100` files to `legacy/` and
  redesign `store/` files to cache root when destinations are free

### Next
- Soak STORE_ROOT + migration with biltoo
- Store-only Client (no legacy open)

### Apply
```bash
git pull /path/to/thumtoo-238-store-root-migrate.bundle HEAD
```

### Done criteria
- [x] Bundle **238**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-237-store-root-layout.** THUMTOO_STORE_ROOT experimental layout.
Prior: **236**.

### Change
- `THUMTOO_STORE_ROOT=1`: Store at `cache_root/`; legacy Database+BlobStore at
  `cache_root/legacy/` (no schema clash)
- Default unchanged: Store under `cache_root/store/`, legacy at top-level

### Next
- Soak STORE_ROOT with biltoo; migrate existing dual-path caches if desired
- Store-only Client (no legacy open) after full_native has a non-levels path

### Apply
```bash
git pull /path/to/thumtoo-237-store-root-layout.bundle HEAD
```

### Done criteria
- [x] Bundle **237**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-236-gc-soft-levels.** thumtoo-gc --soft-levels purges soft ladder rows.
Prior: **235**.

### Change
- `thumtoo-gc --soft-levels`: delete levels with max_edge ≤ kBatchMaxEdge (index + blobs)
- HOST_CUTOVER documents purge for tiles-first caches

### Next
- Store-only Client open / top-level Store layout
- Remove dual-write helpers after Store-only is default

### Apply
```bash
git pull /path/to/thumtoo-236-gc-soft-levels.bundle HEAD
```

### Done criteria
- [x] Bundle **236**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-235-cutover-soft-writes-done.** Mark soft-level write stop in cutover docs.
Prior: **234**.

### Change
- HOST_CUTOVER §3.1 checked off (tiles-first default)
- API_MIGRATION next steps: soft writes done; Store-only open still pending

### Next
- Store-only Client open (no legacy Database) behind a flag, or top-level Store paths
- Remove dual-write helpers after Store-only is default

### Apply
```bash
git pull /path/to/thumtoo-235-cutover-soft-writes-done.bundle HEAD
```

### Done criteria
- [x] Bundle **235**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-234-tiles-first-default.** Soft/overview levels off by default.
Prior: **233**.

### Change
- `tiles_only_mode()` default **true** (tiles-first)
- `THUMTOO_SOFT_LEVELS=1` or `THUMTOO_TILES_ONLY=0` restores durable soft levels
- HOST_CUTOVER §4–5 updated for production default

### Next
- Drop legacy levels table / dual-write when hosts no longer need soft ladder rows
- Move Store to top-level `$cache/` after legacy index gone

### Apply
```bash
git pull /path/to/thumtoo-234-tiles-first-default.bundle HEAD
```

### Done criteria
- [x] Bundle **234**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-233-tiles-only-soak-ok.** TILES_ONLY soak confirmed working.
Prior: **232**.

### Change
- HOST_CUTOVER §5: TILES_ONLY soak **works** (user report + biltoo ≥1007)
- biltoo tile-native marked partial (1005–1007); drop-levels still explicit next step

### Next
- Broader soak (gallery, slideshow, PDF/archive, crop Full)
- When ready: stop soft-level writes by default (or make TILES_ONLY the default)
- Then drop legacy `index`/`blobs` soft ladder path and top-level Store layout

### Apply
```bash
git pull /path/to/thumtoo-233-tiles-only-soak-ok.bundle HEAD
```

### Done criteria
- [x] Bundle **233**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-232-tiles-only-session-reply.** TILES_ONLY soft reply from session encode.
Prior: **231**.

### Change
- Track `session_best_level` across EnsurePixels encode branches
- When TILES_ONLY skips put_level, final (and downscale) reply uses session bytes

### Next
- Host soak: biltoo ≥1007 + THUMTOO_TILES_ONLY=1
- Drop legacy levels write path after soak

### Apply
```bash
git pull /path/to/thumtoo-232-tiles-only-session-reply.bundle HEAD
```

### Done criteria
- [x] Bundle **232**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-231-tiles-only-env.** THUMTOO_TILES_ONLY skips soft/overview level writes.
Prior: **230**.

### Change
- `tiles_only_mode()` via `THUMTOO_TILES_ONLY=1`
- Soft/overview `put_level` skipped; full_native + tiles unchanged
- HOST_CUTOVER §4 documents the flag

### Next
- Host soak with biltoo ≥1007 + TILES_ONLY
- Drop legacy levels write path after soak

### Apply
```bash
git pull /path/to/thumtoo-231-tiles-only-env.bundle HEAD
```

### Done criteria
- [x] Bundle **231**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-230-host-cutover-doc.** Host cutover checklist after Phase E dual-path.
Prior: **229**.

### Change
- `docs/HOST_CUTOVER.md` — biltoo tile-native steps; when to drop legacy levels/index

### Next
- biltoo: PreferCache / filmstrip treat TileSynth as valid soft (tile-native)
- Optional `THUMTOO_TILES_ONLY` to refuse soft-level writes under test

### Apply
```bash
git pull /path/to/thumtoo-230-host-cutover-doc.bundle HEAD
```

### Done criteria
- [x] Bundle **230**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-229-phase-e-dual-path-complete.** Phase E library dual-path spine done.
Prior: **228**.

### Change
- `get_archive_entries` falls back to Store `container_member` when legacy TOC empty
- PLAN: Phase E dual-path marked complete; success criteria all checked
- Remaining work is host cutover (tile-native biltoo; drop legacy index)

### Next
- biltoo: PreferCache / filmstrip tile-native (post 0.1.0)
- Drop legacy `levels` write path when hosts no longer need soft ladder rows
- Move Store files to top-level `$cache/` after legacy gone

### Apply
```bash
git pull /path/to/thumtoo-229-phase-e-dual-path-complete.bundle HEAD
```

### Done criteria
- [x] Bundle **229**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-228-archive-store-mirror.** Archive TOC + member blob dual-write to Store.
Prior: **227**.

### Change
- `ensure_store_container_blob` — container blob + root/`file://` locators
- `refresh_archive_toc` → Store `replace_container_members`
- `mirror_probe_to_store` on `//archive:` → `set_container_member_blob`

### Next
- Host tile-native (biltoo after 0.1.0)
- Drop legacy levels when hosts ready

### Apply
```bash
git pull /path/to/thumtoo-228-archive-store-mirror.bundle HEAD
```

### Done criteria
- [x] Bundle **228**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-227-tiles-first-soft.** Skip soft ladder encode when tiles cover.
Prior: **226**.

### Change
- `handle_ensure_pixels`: after probe, if not full_native and tiles cover the
  request edge, reply TileSynth and skip durable soft-level encode.

### Next
- Host tile-native (biltoo after 0.1.0)
- Archive member Store mirror
- Drop legacy levels entirely when hosts ready

### Apply
```bash
git pull /path/to/thumtoo-227-tiles-first-soft.bundle HEAD
```

### Done criteria
- [x] Bundle **227**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-226-client-directory.** Client directory snapshot API on Store.
Prior: **225**.

### Change
- `Client::{find,list,replace,delete}_directory_*` → Store
- `refresh_directory_snapshot(path)` FS walk → Store snapshot
- PLAN success criteria updated for dual-path spine

### Next
- Reduce durable soft-level writes when tiles cover
- Host tile-native (biltoo after 0.1.0)
- Archive member Store mirror

### Apply
```bash
git pull /path/to/thumtoo-226-client-directory.bundle HEAD
```

### Done criteria
- [x] Bundle **226**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-225-nodiscard-warnings.** Silence ensure_region / add_link_edge nodiscard.
Prior: **224**.

### Change
- `(void)ensure_region(...)` in `ensure_image_media`
- `(void)add_link_edge(...)` in test_store re-add path

### Apply
```bash
git pull /path/to/thumtoo-225-nodiscard-warnings.bundle HEAD
```

### Done criteria
- [x] Bundle **225**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-224-meta-from-store.** Phase E: get_size/get_meta fall back to Store.
Prior: **223**.

### Change
- `meta_from_store`: locator + image media dims (and page region presence).
- `get_size` / `get_meta` prefer legacy, then Store.

### Next
- Reduce durable `levels` writes; prefer tiles
- Hosts: `data_root` = XDG_DATA (biltoo-1002)

### Apply
```bash
git pull /path/to/thumtoo-224-meta-from-store.bundle HEAD
```

### Done criteria
- [x] Bundle **224**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-223-tile-read-store.** Phase E: get_tile / TileSynth fall back to Store.
Prior: **222**.

### Change
- `store_tile_target_for_content_id` maps pure/page ids → media/region.
- `get_tile` / `has_tile` / `get_tile_coverage` fall back to Store.
- `get_pixels_from_tiles` uses Store scale range when legacy has no tiles.

### Next
- Store-primary size/meta (less reliance on legacy content row)
- biltoo XDG data_root

### Apply
```bash
git pull /path/to/thumtoo-223-tile-read-store.bundle HEAD
```

### Done criteria
- [x] Bundle **223**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-222-tile-mirror-store.** Phase E: dual-write tiles into Store bulk.
Prior: **221**.

### Change
- `store_tiles` → `mirror_tiles_to_store`: map pure/page `content_id` to
  media/region, `put_tile` on Store (create blob if probe race).
- Legacy BlobStore + Database tile rows unchanged.

### Next
- Soft get_pixels assemble from Store tiles (compat)
- biltoo XDG data_root

### Apply
```bash
git pull /path/to/thumtoo-222-tile-mirror-store.bundle HEAD
```

### Done criteria
- [x] Bundle **222**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-221-probe-mirror-store.** Phase E: mirror probe into Store blob/media.
Prior: **220**.

### Change
- `Client::mirror_probe_to_store` after successful size probe (and on size
  cache-hit path).
- Pure `sha256:<hex>` → blob + hash + locator + image media.
- `sha256:<hex>:page:N` → file blob + locator + document media + page region.
- Failures logged under THUMTOO_DEBUG only; never fail the probe.

### Next
- Dual-write tiles into Store bulk on encode
- Soft get_pixels assemble from Store tiles
- biltoo XDG data_root

### Apply
```bash
git pull /path/to/thumtoo-221-probe-mirror-store.bundle HEAD
```

### Done criteria
- [x] Bundle **221**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-220-client-store-dual-path.** Phase E start: Client opens Store dual-path.
Prior: **219**.

### Change
- `Client::open` opens redesign `Store` under `cache_root/store/` (index+bulk)
  and `user.sqlite` under optional `data_root` (default: cache_root).
- `Client::store()` accessor; `THUMTOO_API_STORE` feature probe.
- Tags dual-write/read: legacy + `blob:sha256:` when content_id is sha256.
- `docs/API_MIGRATION.md` layout and cutover steps.

### Next
- Probe path: ensure Store locator/blob/media on request_size success
- Soft pixels from Store tiles (compat assemble)
- biltoo: pass XDG data_root

### Apply
```bash
git pull /path/to/thumtoo-220-client-store-dual-path.bundle HEAD
```

### Done criteria
- [x] Bundle **220**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-219-store-phase-d-close.** Store Phase D close: annotation, http_body, tile list.
Prior: **218**.

### Change
- User: `create_annotation` / find / body·geom set / list / delete.
- Bulk: `put_http_body` / `get_http_body` / `delete_http_body`.
- Index: `list_tiles_for_region`, `list_tile_scales`.
- PLAN.md: Phase D complete; next Phase E (Client / hosts).

### Next
- Phase E: open Store from Client (or dual path); pixel resolve via tiles;
  biltoo cutover notes.

### Apply
```bash
git pull /path/to/thumtoo-219-store-phase-d-close.bundle HEAD
```

### Done criteria
- [x] Bundle **219**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-218-store-phase-d-user-sets.** Store Phase D: collections, bookmarks, links.
Prior: **217**.

### Change
- User APIs: `create_collection` / members / delete; `create_bookmark` /
  list-by-target; `add_link_edge` / list from·to / remove.
- Tests: membership upsert, link idempotency, user rows survive index+bulk wipe.
- PLAN.md Phase D marked complete for directory + user overlays (annotation /
  http_body still optional).

### Not in this tip
- annotation write API
- HTTPS → http_body
- Client wiring to Store (Phase E)

### Apply
```bash
git pull /path/to/thumtoo-218-store-phase-d-user-sets.bundle HEAD
```

### Done criteria
- [x] Bundle **218**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-217-store-phase-d-dir-tags.** Store Phase D (partial): directory + tags.
Prior: **216**.

### Change
- Index: `directory_snapshot` / `directory_entry`; `replace_directory_snapshot`,
  list/find/delete/count APIs (cache-first folder open).
- User: `ensure_tag_def`, `add_blob_tag` / `remove_blob_tag`,
  `tags_for_blob_ref` / `blob_refs_for_tag` on `blob:sha256:` refs.
- Tests: dir replace, tags survive index+bulk wipe; fix reopen blob count.
- PLAN.md Phase D partial status.

### Not in this tip
- collection / bookmark / link_edge / annotation write APIs
- HTTPS → http_body
- Client wiring to Store

### Apply
```bash
git pull /path/to/thumtoo-217-store-phase-d-dir-tags.bundle HEAD
```

### Done criteria
- [x] Bundle **217**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-216-store-phase-c.** Store Phase C: container_member, document pages.
Prior: **215**.

### Change
- container_member TOC; set_container_member_blob after hash
- replace_container_members; ensure_document_media / ensure_page_region
- Hash policy: TOC does not hash; preserve blob_id on upsert without new id

### Apply
```bash
git pull /path/to/thumtoo-216-store-phase-c.bundle HEAD
```

### Done criteria
- [x] Bundle **216**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-215-store-phase-b.** Store Phase B: media, region, tiles.
Prior: **214**.

### Change
- media / region / tile tables on index; tile_blob payload API
- `ensure_image_media`, `ensure_region`, `put_tile` / `get_tile_data`
- Tile PK (media_id, region_id, scale, x, y); full region for images
- test_store extended

### Apply
```bash
git pull /path/to/thumtoo-215-store-phase-b.bundle HEAD
```

### Done criteria
- [x] Bundle **215**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-214-store-phase-a.** Phase A Store: three DBs, blob/hash/locator.
Prior: **213**.

### Change
- `thumtoo::Store` opens index/bulk/user SQLite (epoch 100); seeds hash_algo +
  codec; legacy index schema &lt; 100 replaced (user DB kept).
- Blob insert, blob_hash put/lookup, locator upsert, `blob:sha256:` format/parse.
- `tests/test_store.cpp`. Client still on legacy Database.

### Apply
```bash
git pull /path/to/thumtoo-214-store-phase-a.bundle HEAD
```

### Done criteria
- [x] Bundle **214**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-213-final-plan.** Final pre-implementation plan; tile PK includes region_id.
Prior: **212**.

### Change
- Add `docs/PLAN.md`: locked decisions, API impact, phases A–E, non-goals,
  success criteria. Next coding step: Phase A (schema open + blob/locator).
- `docs/DATABASE.md`: tile / tile_blob PK = (media_id, region_id, scale, x, y).

### Apply
```bash
git pull /path/to/thumtoo-213-final-plan.bundle HEAD
```

### Done criteria
- [x] Bundle **213**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-212-database-schema.** DATABASE.md schema for review; split 0.2.0 TODOs.
Prior: **211**.

### Change
- Add `docs/DATABASE.md`: concrete SQL for index/bulk/user DBs, rationale,
  policies (tiles-only, hash-on-read, directory cache-first, text link refs).
- `docs/BLOB_AND_OVERLAY.md` reduced to a pointer; 0.2.0 topics listed in
  DATABASE.md §12 only.

### Apply
```bash
git pull /path/to/thumtoo-212-database-schema.bundle HEAD
```

### Done criteria
- [x] Bundle **212**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-211-overlay-decisions.** Record redesign decisions + Hypertia sketch.
Prior: **210**.

### Change
- `docs/BLOB_AND_OVERLAY.md`: tiles-only; hash-on-read/tag; EPUB multi-key;
  three DB roles; thumtoo-as-library; link text refs; provisional PK vs export
  hash; directory cache-first; media+kind; 0.2.0 deferrals; Hypertia HTTP
  sketch; updated priority and narrow open list.

### Apply
```bash
git pull /path/to/thumtoo-211-overlay-decisions.bundle HEAD
```

### Done criteria
- [x] Bundle **211**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-210-archive-pipe-uri.** Prefer //archive: pipes; no JAR !/ in docs.
Prior: **209**.

### Change
- `docs/BLOB_AND_OVERLAY.md`: archive member examples use
  `file://…//archive:member` (and nested `//archive:`) per DESIGN.md, not
  JAR-style `path!/member`.

### Apply
```bash
git pull /path/to/thumtoo-210-archive-pipe-uri.bundle HEAD
```

### Done criteria
- [x] Bundle **210**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-209-ia-archive-org.** Document Internet Archive APIs in BLOB_AND_OVERLAY.
Prior: **208**.

### Change
- `docs/BLOB_AND_OVERLAY.md` §14: archive.org metadata/search/download/IIIF,
  locator mapping (`ia:` / details / download), biltoo product phases, CDL limits.
- Priority list and open questions updated.

### Apply
```bash
git pull /path/to/thumtoo-209-ia-archive-org.bundle HEAD
```

### Done criteria
- [x] Bundle **209**

---

# TODO / agent handoff

## Status (2026-09-16)

**Tip: thumtoo-208-blob-overlay-brainstorm.** Design note: blob identity, overlays, hypertia.
Prior: **207**.

### Change
- Add `docs/BLOB_AND_OVERLAY.md` — consolidated brainstorm (no code):
  integer blob ids, `blob_hash`, media/region, member hashing, tiles vs levels,
  dirtoo coverage, Memex trails, hypertia/UI/curl examples, IPFS/IPLD borrow.
- No schema migration; discussion only.

### Apply
```bash
git pull /path/to/thumtoo-208-blob-overlay-brainstorm.bundle HEAD
```

### Done criteria
- [x] Bundle **208**

---

# TODO / agent handoff

## Status (2026-09-14)

**Tip: thumtoo-207-exif-autorot-display-size.** EXIF orientation is a decode contract.
Prior: **206**.

### Contract
- **Probe / stored native size** = display size after EXIF (tags 5–8 swap axes)
- **Tiles / full Vips opens** = `vips_autorot` before cut/encode
- **Ladder soft** = `vips_thumbnail` already autorots by default; long-edge pick uses display size

Hosts (biltoo) must not re-apply a second autorot policy on thumtoo samples.
Session ContentXform remains app-side (user turns/flips).

### Apply
```bash
git pull /path/to/thumtoo-207-exif-autorot-display-size.bundle HEAD
```

### Done criteria
- [x] Bundle **207**

---

# TODO / agent handoff

## Status (2026-09-14)

**Tip: thumtoo-206-full-requires-coverage.** Full hit requires pixel coverage; soft 512 is not Full.
Prior: **205**.

### Problem
Soft ladder levels tagged `PixelSource::Full` (edge >= soft raster long edge).
`request_full_pixels` accepted Source::Full without coverage → returned 512 forever
for PDF pages; biltoo soft-tier RETRY looped.

### Change
- get_full_pixels / request_full: coverage only
- build_level_rgb_at_edge: Full only if edge > kMaxSoftLadderEdge && edge >= long_edge

### Apply
```bash
git pull /path/to/thumtoo-206-full-requires-coverage.bundle HEAD
```

### Done criteria
- [x] Bundle **206**

---

# TODO / agent handoff

## Status (2026-09-14)

**Tip: thumtoo-205-full-adequate-real-native.** Full CACHE_HIT only when pixels cover request or true native meta.
Prior: **204**.

### Problem
Meta size was often a soft/TileSynth probe (~2048). `level_adequate` treated
covering that meta as success for full_native want=6048 → CACHE_HIT TileSynth
forever; Gallery stayed low-res.

### Change
- full_native: meta-size cover only if meta itself is ≥ ~request (real native)
- Final EnsurePixels reply: no TileSynth for full_native
- Ladder levels above soft max tagged PixelSource::Full
- Archive Full encode promotes content width/height when larger than soft probe

### Apply
```bash
git pull /path/to/thumtoo-205-full-adequate-real-native.bundle HEAD
```

### Done criteria
- [x] Bundle **205**

---

# TODO / agent handoff

## Status (2026-09-14)

**Tip: thumtoo-204-full-no-tilesynth-cachehit.** Full EnsurePixels must not CACHE_HIT on TileSynth shortfall.
Prior: **203**.

### Problem
`level_adequate` for full_native used soft max (512) as want. TileSynth 2048
looked adequate for request 6048 → CACHE_HIT → biltoo shortfall RETRY forever.

### Change
- `level_adequate`: full_native compares against Full request edge (≤ kFullMaxEdge)
- `get_full_pixels`: any non-Full shortfall when want > soft max → nullopt

### Apply
```bash
git pull /path/to/thumtoo-204-full-no-tilesynth-cachehit.bundle HEAD
```

### Done criteria
- [x] Bundle **204**

---

# TODO / agent handoff

## Status (2026-09-14)

**Tip: thumtoo-203-full-past-2048.** Full ladder past 2048; soft shortfall is not a Full hit.
Prior: **202**.

### Problem
`kLadderEdges` ended at **2048**. `pick_preview_edge` for Full with edge_limit 6048
still encoded max 2048 — biltoo Gallery zoom stuck below native.
`get_full_pixels` returned soft shortfall as "best short", which hosts could settle on.

### Change
- `kLadderEdges` → `{128,256,512,1024,2048,4096,8192}`
- `pick_preview_edge`: soft band snaps ≤512; display/Full may use exact limit past soft
- `get_full_pixels`: soft/JpegShrink/Embedded shortfall when want > soft max → nullopt (force Ensure Full)

### Apply
```bash
git pull /path/to/thumtoo-203-full-past-2048.bundle HEAD
```

### Host
Rebuild biltoo against this thumtoo tip so Gallery Full climb can store 4k/8k levels.

### Done criteria
- [x] Bundle **203**

---

# TODO / agent handoff

## Status (2026-09-14)

**Tip: thumtoo-202-document-index.** Durable page-count index for PDF / DjVu / EPUB.
Prior: **201**.

### Design
- Schema **v4** table `document_index (document_uri, layout_key, page_count, size, mtime_ns, indexed_at)`
- `Client::document_page_count` — cache-first; validate size+mtime; else source open + store
- `Client::refresh_document_index` — force re-index
- EPUB `layout_key` = `format_epub_layout_params` (empty for PDF/DjVu)
- `prepare_paths` uses the cached path

### Host
biltoo expand PDF/EPUB/DjVu should call `Client::document_page_count` like archive TOC.

### Done criteria
- [x] Bundle **202**

---

# TODO / agent handoff

## Status (2026-09-14)

**Tip: thumtoo-201-softonly-no-tilesynth.** SoftOnly / soft request skip TileSynth.
Prior: **200**.

### Problem
Gallery SoftOnly called `get_pixels`, which fell through to **TileSynth** when the
soft ladder was missing but grid tiles existed. TileSynth JPEG-decodes every
cell sequentially on one worker, then re-encodes — multi-second per image and
feels single-threaded even with a "cached" archive.

### Change
- `get_pixels(..., allow_tile_synth=true)` — SoftOnly / `request_pixels` pass false
- Missing soft → EnsurePixels (parallel soft ladder), not TileSynth
- PreferCache / overview still may TileSynth

### Apply
```bash
git pull /path/to/thumtoo-201-softonly-no-tilesynth.bundle HEAD
```

### Done criteria
- [x] Bundle **201**

---

# TODO / agent handoff

## Status (2026-09-14)

**Tip: thumtoo-200-size-reply-lqip.** Size probe returns durable LQIP with size.
Prior: **199**.

### Change
- `SizeReply { size, lqip }` replaces `optional<Size>` on `SizeCallback`
- Every size path attaches cache-only `get_lqip` (never generates on probe)
- Hosts get size+LQIP together on open; soft/full still fill later

### ABI
- **Breaking** for `request_size` / `prepare_paths` callbacks — update callers

### Done criteria
- [x] Bundle **200**

---

# TODO / agent handoff

## Status (2026-09-14)

**Tip: thumtoo-199-lqip-from-soft.** Opportunistic LQIP from durable soft levels.
Prior: **198**.

### Problem
LQIP was rarely present for biltoo cold opens. Size probe correctly does not
encode LQIP. Soft PreferCache hits returned pixels without ever filling LQIP.
`ensure_lqip` also re-decoded the source to “upgrade” ThumbHash→Handsum, which
is expensive and starved the worker.

### Change
- `store_lqip_if_missing`: keep **any** existing LQIP (no re-encode)
- `ensure_lqip`: return existing; else encode from **smallest soft/full level**
  blob via `lqip_thumbhash_from_buffer`; only then fall back to source/page paths
- `request_pixels` / `request_overview_pixels` soft cache hits: `request_lqip`
  when missing (worker fills from soft — not a host ensure API)

### Done criteria
- [x] Bundle **199**

---

# TODO / agent handoff

## Status (2026-09-13)

**Tip: thumtoo-198-rar5-skip-unarr.** Probe RAR5 magic; skip unarr (no spam); use libarchive.
Prior: **197**.

### Problem
Every unarr open on RAR5 printed `rar.c:214: RAR 5 format isn't supported` and
returned null. TOC/extract retried → biltoo/thumtoo spam loops. Libarchive can
list/extract many non-solid RAR5 archives.

### Change
- `file_is_rar5` 8-byte magic probe
- `archive_prefers_unarr` false for RAR5 → TOC + extract use libarchive
- `open_rar` refuses RAR5 before `ar_open_rar_archive`
- `archive_is_rar5` public; `thumtoo-archive info` reports rar5

### Done criteria
- [x] Bundle **198**

---

# TODO / agent handoff

## Status (2026-09-13)

**Tip: thumtoo-197-parent-scope-guard.** Only PARENT_SCOPE feature exports when nested.
Prior: **196**.

### Change
- `thumtoo_export_to_parent` macro: skip PARENT_SCOPE when standalone (no parent)

### Done criteria
- [x] Bundle **197**

---

# TODO / agent handoff

## Status (2026-09-13)

**Tip: thumtoo-196-status-archives-restore.** Restore full `archives` listing (members) after path-query edit.
Prior: **195**.

### Done criteria
- [x] Bundle **196**

---

# TODO / agent handoff

## Status (2026-09-13)

**Tip: thumtoo-195-status-path-query.** `thumtoo-status path PATH|URI` shows cache rows for one path.
Prior: **194**.

### Change
- `thumtoo-status path` / `query` / `show`: resolve path or URI → locator(s), content, levels, tile scale summary
- Matches exact URI, `file://` from absolute path, outer_path, uri prefix

### Usage
```bash
thumtoo-status path /tmp/photo.jpg
thumtoo-status path 'file:///tmp/photo.jpg'
thumtoo-status path /tmp/archive.rar   # locators + archive members under outer_path
```

### Done criteria
- [x] Bundle **195**

---

# TODO / agent handoff

## Status (2026-09-13)

**Tip: thumtoo-194-export-mkBuildInputs.** Export `lib.mkBuildInputs` for biltoo nested builds.
Prior: **193**.

### Change
- `flake.nix` `lib.mkBuildInputs` = same list as package `buildInputs` (includes libunarr)

### Done criteria
- [x] Bundle **194**

---

# TODO / agent handoff

## Status (2026-09-13)

**Tip: thumtoo-193-unarr-backslash-literal.** Fix `'\\'` char literal in member_paths_equal.
Prior: **192**.

### Change
- `archive_unarr.cpp`: backslash compare was `'\'` (invalid C++); use `'\\'`

### Done criteria
- [x] Bundle **193**

---

# TODO / agent handoff

## Status (2026-09-13)

**Tip: thumtoo-192-feature-parent-scope.** Export THUMTOO_HAVE_* to parent (biltoo About/summary).
Prior: **191**.

### Change
- `THUMTOO_HAVE_UNARR|MUPDF|DJVU|CURL` set with `PARENT_SCOPE` for biltoo
  `add_subdirectory` consumers

### Done criteria
- [x] Bundle **192**

---

# TODO / agent handoff

## Status (2026-09-13)

**Tip: thumtoo-191-rar-unarr-no-libarchive-fallback.** RAR extract stays on unarr; no libarchive fallback.
Prior: **190**.

### Problem
`extract_archive_members` tried unarr for `.rar`/`.cbr`, then on empty result fell
through to libarchive. Libarchive cannot extract solid RAR, so hosts saw
"RAR solid archive support unavailable" even when the real issue was unarr open
failure (typically **RAR5**, unsupported by libunarr).

### Change
- `.rar`/`.cbr` extract: **unarr only** when `THUMTOO_HAVE_UNARR` (no libarchive fallback)
- Member path match: case-insensitive + `\` → `/` normalize
- Docs: RAR5 not supported by unarr; convert to RAR4/ZIP

### Done criteria
- [x] Bundle **191**

### Verify
```bash
# RAR4 solid should extract via unarr
xxd /tmp/archive.rar | head -1   # Rar!..07 00 = RAR4; ..07 01 = RAR5
thumtoo-archive list /tmp/archive.rar
```

---

# TODO / agent handoff

## Status (2026-09-13)

**Tip: thumtoo-190-purge-test-fix.** Qualify Database::LocatorRow/ContentRow in purge test.
Prior: **189**.

### Change
- test_database purge block uses `Database::LocatorRow` / `Database::ContentRow`
- `(void)` on nodiscard blob delete counts

### Done criteria
- [x] Bundle **190**

---

# TODO / agent handoff

## Status (2026-09-13)

**Tip: thumtoo-189-purge-path.** Forget a path/URI from the cache (cold for debug).
Prior: **188**.

### API
- `Database::list_locators_for_outer_path` — exact outer_path match
- `Database::PurgeStats` — removed_uris, purged_content_ids, tiles/levels counts
- `Client::purge_uri(uri, dry_run)` — cancel queue + drop locator; purge content
  blobs when last locator for that content_id
- `Client::purge_path(path, dry_run)` — all locators for outer_path + file:/// form
- `thumtoo-gc --uri URI` / `--path PATH` (repeatable; works with `--dry-run`)

### Behaviour
- Does not delete source files on disk
- Shared content_id kept until no locators remain
- After purge: `get_size` / `get_pixels` miss (cold)

### Done criteria
- [x] Bundle **189**

### Next
- Optional biltoo debug action to call purge_path on current image
- Host ImageCache clear for same path when debugging

---

# TODO / agent handoff

## Status (2026-09-13)

**Tip: thumtoo-188-archive-cli.** thumtoo-archive + flake checks.tools-bin (no postInstall).
Prior: **187**.

### Change
- `thumtoo-archive` CLI (list/info/cat/extract)
- CMake build + install into `$out/bin`
- flake app `archive`, `thumtoo-run-archive`
- **`checks.tools-bin`** for `nix flake check` — not postInstall asserts

### Done criteria
- [x] Bundle **188**

---

# TODO / agent handoff

## Status (2026-09-13)

**Tip: thumtoo-187-libunarr-rar.** Optional libunarr for solid RAR/CBR under //archive:.
Prior: **186**.

### Change
- Optional `libunarr` (`THUMTOO_WITH_UNARR`, flake `libunarr`)
- `.rar`/`.cbr` TOC + extract prefer unarr (sequential solid-safe walk)
- libarchive remains default for ZIP/7z and RAR fallback
- `//archive:` unchanged (no forced `//unarr:` URI yet)
- Docs: TILES.md archive backends + seek notes

### Limits
- unarr: **no RAR5** yet; solid extract is sequential (not random seek of payloads)

### Done criteria
- [x] Bundle **187**

---

# TODO / agent handoff

## Status (2026-09-13)

**Tip: thumtoo-186-request-full-pixels.** Full/near-native via request_full_pixels.
Prior: **185**.

### API
- `request_full_pixels(uri, max_edge, cb)` / `get_full_pixels`
- `RasterPolicy::Full`, `kFullMaxEdge` (8192)
- EnsurePixels `job.full_native` uses edge_limit up to kFullMaxEdge

### Done criteria
- [x] Bundle **186**

---

# TODO / agent handoff

## Status (2026-09-13)

**Tip: thumtoo-181-debug-overlay-visible.** Overlay was silent (JXL decode fail / low contrast).
Prior: **180**.

### Change
- JXL load fallback for soft ladder decode
- Magenta border; log when active / decode fail / stamp
- Must rebuild thumtoo **and** biltoo (host also stamps QImage)

### Done criteria
- [x] Bundle **181**

---

# TODO / agent handoff

## Status (2026-09-13)

**Tip: thumtoo-180-debug-overlay.** THUMTOO_DEBUG_OVERLAY stamps soft/tiles with size/scale/source.
Prior: **179**.

### Change
- Env `THUMTOO_DEBUG_OVERLAY=1` (or `BILTOO_DEBUG_OVERLAY=1`)
- On `get_pixels` / `get_tile` / live tile reply: black border + text
  (basename, WxH, request edge / scale/x/y, PixelSource / TileSource)
- Applied on the **read path only** — durable cache bytes unchanged
- Soft levels re-encoded as JPEG when stamped (debug-only)

### Done criteria
- [x] Bundle **180**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-179-pipeline-hit-miss-log.** get_pixels HIT/MISS + EnsurePixels CACHE_HIT.
Prior: **178**.

### Change
- `get_pixels`: log HIT / SHORT / MISS with source and dimensions
- `EnsurePixels` early return: CACHE_HIT
- `set_interest`: log epoch + item count

### Done criteria
- [x] THUMTOO_DEBUG shows cache vs build clearly
- [x] Bundle **179**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-178-levelrow-optional-dims.** Fix LevelRow optional width/height compile.
Prior: **177**.

### Change
- `larger->width/height` are `std::optional<int>` — use `value_or(0)` before `std::max`

### Done criteria
- [x] Builds
- [x] Bundle **178**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-177-embedded-covers-edge.** Embedded EXIF only when it covers request.
Prior: **176**.

### Root cause
`build_ladder_*` accepted APP1 thumbs whenever `emb_edge >= 128`, so soft
requests for 256/512 stored Embedded ~160–320 and never ran `vips_thumbnail`
(JpegShrink). EnsurePixels also short-circuited on stored levels keyed at
max_edge=512 with smaller pixels.

### Change
- Embedded only if `emb_edge >= (edge * 9) / 10`
- Early level reuse requires row pixel long-edge coverage + `level_adequate`

### Next
- biltoo-502 removes Gallery have≥128 upgrade stop
- Optional: GC / rewrite mis-tagged Embedded rows in existing caches

### Done criteria
- [x] Soft 512 builds JpegShrink when APP1 is small
- [x] Bundle **177**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-176-test-queue-stats.** Unit test for Client::queue_stats.
Prior: **175**.

### Change
- tests/test_queue_stats.cpp + ctest NAME queue_stats

### Next
- biltoo flake already on 175
- Pipeline stable; only bugfixes unless new scope

### Done criteria
- [x] Test
- [x] Bundle **176**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-175-fix-delete-level-ns.** Move Database::delete_level into namespace thumtoo.
Prior: **174**.

### Change
- delete_level was under anonymous namespace → -fpermissive error

### Done criteria
- [x] Builds
- [x] Bundle **175**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-174-queue-stats.** Client::queue_stats for host metrics.
Prior: **173**.

### Change
- QueueStats { pending, inflight, focus_full_inflight, interest_epoch }
- PIXEL_PIPELINE.md stale "Still open" cleaned

### Next
- biltoo debug status can surface queue_stats
- Soft-queue metrics optional

### Done criteria
- [x] API
- [x] Bundle **174**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-173-request-raster.** Unified get_raster / request_raster API.
Prior: **172**.

### Change
- RasterPolicy, RasterRequest
- get_raster (cache-only), request_raster (async route)
- THUMTOO_API_REQUEST_RASTER

### Next
- biltoo optional migration to request_raster
- Provenance UI

### Done criteria
- [x] API
- [x] Bundle **173**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-172-invalidate-q1-on-pyramid.** Drop JpegShrink levels after FocusFull pyramid.
Prior: **171**.

### Change
- Database::delete_level / BlobStore::delete_level
- invalidate_q1_levels after successful tile_pyramid store
- Keeps Embedded + Full soft levels

### Next
- Prefer TileSynth construct in more get_pixels paths (already falls through)
- Unified request_raster
- Provenance UI

### Done criteria
- [x] Q1 invalidate on Q2
- [x] Bundle **172**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-171-speculative-idle.** Defer Speculative interest when queue is busy.
Prior: **170**.

### Change
- kSpeculativeEnqueueWhenQueueBelow = 8
- set_interest skips Speculative when queue_len ≥ threshold

### Next
- Unified request_raster API (optional)
- Q1 invalidate when Q2 appears

### Done criteria
- [x] Speculative idle policy
- [x] Bundle **171**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-170-focus-full-inflight.** Inflight-aware FocusFull concurrency.
Prior: **169**.

### Change
- focus_full_inflight_ counter on claim/release (single + batch)
- enqueue cap uses pending + inflight
- Worker rotates blocked pyramid jobs when already at inflight cap

### Next
- biltoo soft-path polish
- metrics / debug dump of focus_full_inflight_

### Done criteria
- [x] Inflight accounting
- [x] Bundle **170**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-169-focus-full-cap.** Cap pending FocusFull tile pyramids.
Prior: **168**.

### Change
- kFocusFullMaxConcurrent = 1
- enqueue: supersede same-uri pyramid; drop oldest pending pyramids beyond cap

### Next
- biltoo Workspace Primary
- inflight-aware FocusFull accounting (optional)

### Done criteria
- [x] Cap
- [x] Bundle **169**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-168-focus-full-primary.** Primary interest builds tile pyramid.
Prior: **167**.

### Change
- set_interest: InterestRole::Primary → request_tile_pyramid after overview

### Next
- biltoo Image-mode Primary interest
- Cap concurrent FocusFull jobs

### Done criteria
- [x] Primary FocusFull hook
- [x] Bundle **168**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-167-set-interest.** Interest snapshot API schedules overview work.
Prior: **166**.

### Change
- InterestRole, InterestItem
- Client::set_interest — bump epoch, dedupe, request_overview_pixels
- THUMTOO_API_SET_INTEREST
- Test: set_interest

### Next
- biltoo: call set_interest from gallery/filmstrip window
- FocusFull for Primary (tile pyramid)
- Speculative idle-only policy

### Done criteria
- [x] API + test
- [x] Bundle **167**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-166-api-feature-macros.** THUMTOO_API_OVERVIEW_PIXELS / INTEREST_EPOCH.
Prior: **165**.

### Change
- Feature macros in client.hpp for host #ifdef against older trees

### Next
- Hosts should rebuild against this tip for overview/interest

### Done criteria
- [x] Macros
- [x] Bundle **166**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-165-fix-const-mutex.** Fix interest_epoch const mutex + nodiscard.
Prior: **164**.

### Change
- `mu_` is mutable so const `interest_epoch()` can lock
- `(void)` on cursor advance after extract (nodiscard)

### Next
- biltoo filmstrip overview; set_interest snapshot

### Done criteria
- [x] Compiles with -Werror-ish pedantic
- [x] Bundle **165**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-164-overview-pixels.** FastBatch request_overview_pixels ≤1024.
Prior: **163**.

### Change
- `request_overview_pixels` public API
- Job.overview → EnsurePixels encodes/stores up to kBatchMaxEdge (JpegShrink)
- Soft `request_pixels` unchanged (clamp 512)
- Docs: PIXEL_PIPELINE.md

### Next
- biltoo: schedule overview for gallery/filmstrip; bump_interest_epoch on scroll
- set_interest snapshot API
- FocusFull / tile build priority

### Done criteria
- [x] Overview API
- [x] Bundle **164**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-163-interest-cancel.** Interest epoch + cancel pending/uri.
Prior: **162**.

### Change
- Job.epoch stamped at enqueue from interest_epoch_
- bump_interest_epoch / cancel_pending / cancel_uri
- Worker skips stale queue head; coalesce stays same-epoch
- Test: interest_cancel
- Docs: PIXEL_PIPELINE.md

### Next
- FastScale Q1 path ≤ kBatchMaxEdge
- Full set_interest snapshot
- biltoo: bump epoch on gallery scroll + TileSynth provenance

### Done criteria
- [x] Epoch + cancel API
- [x] Queue purge
- [x] Bundle **163**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-162-fastbatch-archive-cursor.** Archive cursor + TOC window + MuPDF optional.
Prior: **161**.

### Change
- MuPDF: compile `pdf_mupdf.cpp` only when found; guard fz helpers
- `kBatchWindowMembers` (32)
- `ArchiveCursor`, `plan_archive_batch_window`, `archive_member_toc_index`
- Client: per-archive cursor, worker coalesce uses TOC-ordered extract window and advances cursor
- Test: `archive_cursor`
- Docs: PIXEL_PIPELINE.md

### Next
- Interest cancel / set_interest epoch
- FastScale Q1 path ≤ kBatchMaxEdge (distinct from soft-512)
- biltoo consume TileSynth provenance

### Done criteria
- [x] Cursor + plan API
- [x] Worker wired
- [x] MuPDF optional build
- [x] Bundle **162**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-161-pixels-from-tiles-verify.** Harden TileSynth construct + smoke test.
Prior: **160**.

### Change
- VIPS-safe composite (wio_input, colourspace, band copy)
- `image_library_init` before decode
- Smoke test `test_pixels_from_tiles` (miss path + constants)

### Verify
- Sandbox has no sqlite3/vips — full compile not run here
- User: `nix develop` / local build + `ctest -R pixels_from_tiles`

### Next
- FastBatch archive cursor
- biltoo consume TileSynth

### Done criteria
- [x] Harden construct
- [x] Smoke test wired
- [x] Bundle **161**

---

# TODO / agent handoff

## Status (2026-09-12)

**Tip: thumtoo-160-pixels-from-tiles.** Construct full-frame pixels from grid tiles.
Prior: **159**.

### Change
- `PixelSource::TileSynth`
- `kBatchMaxEdge` (1024)
- `Client::get_pixels_from_tiles` — cache-only composite at target edge
- `get_pixels` falls through to tile construct when soft does not cover edge
- Docs: TILES.md section, docs/PIXEL_PIPELINE.md

### Next
- FastBatch archive cursor + interest (biltoo plan Phase 2)
- Host (biltoo) call `get_pixels_from_tiles` / trust TileSynth provenance

### Done criteria
- [x] Construct API
- [x] Bundle **160**

---

# TODO / agent handoff

## Status (2026-09-11)

**Tip: thumtoo-159-text-utf8.** Valid UTF-8 for MuPDF text extract; TTL3 cache.
Prior: **158**.

### Change
- `utf8_append_codepoint` skips surrogates / out-of-range
- Text layer magic **TTL3** (re-extract; drop bad cached layers)

### Note
PDFs without a working ToUnicode map still yield wrong *codepoints* from MuPDF
(boxes/CIDs). Encoding is fixed; content may still be unusable for those files.

### Done criteria
- [x] UTF-8 encode hardened
- [x] Bundle **159**

---

## Status (2026-09-11)

**Tip: thumtoo-158-poppler-alias-cleanup.** Normalize remaining Poppler surface.
Prior: **157**.

### Change
- `//poppler-page:N` parses as `LocationPipeKind::PdfPage` (not PdfPagePoppler)
- `format_location` never re-emits `//poppler-page:`
- Docs (PDF_BACKENDS, TILES, DESIGN, audit note) cleaned
- Enum/API names kept for ABI only

### Done criteria
- [x] No Poppler re-emit / normalize parse
- [x] Bundle **158**

---

## Status (2026-09-11)

**Tip: thumtoo-157-uri-test-poppler.** Fix uri/pdf_tiles tests after Poppler
removal. Prior: **156**.

### Change
- `with_pdf_page_poppler` expects `//page:N` + PdfPage
- Explicit `//poppler-page:` still parsed as PdfPagePoppler
- `test_pdf_tiles` skips on missing MuPDF (not Poppler)

### Done criteria
- [x] uri test updated
- [x] Bundle **157**

---

## Status (2026-09-11)

**Tip: thumtoo-156-remove-poppler.** Poppler removed; MuPDF is the only PDF
backend. Prior: **155**.

### Change
- Drop poppler-cpp / poppler-glib from CMake + flake
- Rewrite `pdf.cpp` as MuPDF dispatch only (~330 lines)
- `//poppler-page:N` still *parsed* as legacy alias → MuPDF
- `with_pdf_page_poppler` emits `//page:N`
- Docs: `PDF_BACKENDS.md` rewritten

### Done criteria
- [x] No Poppler link/deps
- [x] PDF API still works via MuPDF
- [x] Bundle **156**

---

## Status (2026-09-11)

**Tip: thumtoo-155-grade-invert-macro.** `THUMTOO_APPEARANCE_GRADE_INVERT` so
biltoo can `#if` grade_invert. Prior: **154**.

### Done criteria
- [x] Macro in appearance.hpp
- [x] Bundle **155**

---

## Status (2026-09-11)

**Tip: thumtoo-154-pdf-text-always-mupdf.** PDF text/outline extract always uses
MuPDF when built, independent of page raster backend. Prior: **153**.

### Cause
`pdf_page_text_layer` returned nullopt unless resolved backend was MuPDF, so
Poppler-default page URIs had no Find/Export Text even with MuPDF linked.

### Done criteria
- [x] Text + outline prefer MuPDF when available
- [x] Bundle **154**

---

## Status (2026-09-11)

**Tip: thumtoo-153-grade-invert.** Optional `grade_invert` on ContentAppearance
(photographic negative). Prior: **152**.

### Change
- `grade_invert` optional int on ContentAppearance
- SQLite column + ALTER migration; get/put bind
- is_identity accounts for invert

### Done criteria
- [x] API + persistence
- [x] Bundle **153**

---

## Status (2026-09-11)

**Tip: thumtoo-152-resolve-internal-links.** MuPDF `fz_resolve_link` for EPUB
spine paths and non-# internal links (outline + page link regions). Prior: **151**.

### Change
- `resolve_internal_link_page` (was hash-only `#…`) in epub.cpp and pdf_mupdf.cpp
- Outline items and link regions: resolve relative `*.xhtml` / path#frag to page
- Skip http(s)/mailto/ftp/file as external
- Cache magic TTL2 / TTO2 so old unresolved blobs are re-extracted

### Done criteria
- [x] Outline spine paths → page_1based
- [x] In-page links resolve when MuPDF can
- [x] Bundle **152**

---

## Status (2026-09-11)

**Tip: thumtoo-151-pixel-source-tag.** Tag soft ladder levels with PixelSource
(Embedded vs JpegShrink vs Full). Prior: **150**.

### Change
- `enum class PixelSource` on `PixelLevel` / `LevelBlob` / `levels.source`
- EXIF path → `Embedded`; `vips_thumbnail` → `JpegShrink`; native edge extract → `Full`
- Additive SQLite migration on `levels`
- Documented in DESIGN.md

### Done criteria
- [x] API + persistence
- [x] Encode paths set source
- [x] Bundle **151**

---

## Status (2026-09-11)

**Tip: thumtoo-150-fprintf-fix.** Fix broken multi-line string in put debug log.
Prior: **149**.

### Done criteria
- [x] Compiles
- [ ] Bundle **150**

---

## Status (2026-09-11)

**Tip: thumtoo-149-put-normalize-log.** Log when put rejects a content id.
Prior: **148**.

### Done criteria
- [x] Bundle **149**

---

## Status (2026-09-11)

**Tip: thumtoo-148-page-content-id.** Content ids for multipage refs:
`sha256:<filehex>:page:<n>`. Prior: **147**.

### Done criteria
- [x] normalize accepts page-qualified ids
- [x] unit tests
- [ ] Bundle **148**

---

## Status (2026-09-11)

**Tip: thumtoo-147-put-step-check.** Check sqlite3_step result in AppearanceStore::put.
Prior: **146**.

### Done criteria
- [x] Bundle **147**

---

## Status (2026-09-11)

**Tip: thumtoo-146-appearance-normalize-verify.** Normalize quarter-turns on put;
integration-style cycle + XDG_STATE_HOME test. Prior: **145**.

### Verified (unit)
- [x] Rotate cycle 1→2→3; turns=4 deletes (identity)
- [x] Flip+turn round-trip
- [x] `default_state_root()` uses `XDG_STATE_HOME/thumtoo`
- [x] All prior appearance tests still pass

### Done criteria
- [x] Bundle **146**

---

## Status (2026-09-11)

**Tip: thumtoo-145-appearance-test-verify.** Unit test expanded + verified
against system libsqlite3 (amalgamation headers). Prior: **144**.

### Verified
- [x] put / get / identity-delete / reopen durable
- [x] bare hex + uppercase normalize
- [x] invalid content_id is a no-op
- [x] open() non-throwing path compiles

### Done criteria
- [x] Bundle **145**

---

## Status (2026-09-11)

**Tip: thumtoo-144-appearance-open-nothrow.** AppearanceStore::open never throws;
invalid store on I/O failure. Prior: **143**.

### Done criteria
- [x] open() returns invalid store instead of throwing
- [ ] Bundle **144**

---

## Status (2026-09-11)

**Tip: thumtoo-143-content-appearance-state.** Durable flip/rotate/crop under
`$XDG_STATE_HOME/thumtoo` (not cache, not source tree). Prior: **142**.

### Design
See [docs/APPEARANCE.md](docs/APPEARANCE.md).

- Key: `content_id` = `sha256:<hex>` (source bytes; never SessionImageId)
- DB: `appearance.sqlite3` WAL under state root
- API: `AppearanceStore` + `ContentAppearance` in `appearance.hpp`
- Identity rows deleted (sparse)
- biltoo seeds/saves via ThumtooCache façade (paired tip)

### Done criteria
- [x] Design doc
- [x] AppearanceStore + unit test
- [ ] Human: biltoo flip survives restart without .biltoo project
- [ ] Bundle **143**

---

## Status (2026-09-09)

**Tip: thumtoo-142-debug-log-file.** Also append traces to ~/.cache/thumtoo/debug.log. Prior: 141.

### 127 (this tip)
- Gallery showed correct **size** (e.g. 1908×2246) but **pixels** were soft
  ladder 214×256: first `request_pixels(256)` stored only 256, and any smaller
  level satisfied larger requests (no upgrade).
- `//pdfimage:` always stores a **native** JXL level (`max_edge` = source long
  edge) plus soft edge when requested smaller.
- `get_pixels` for `//pdfimage:` returns the **largest** stored level (native).
- `handle_ensure_pixels` only short-circuits when the cached level is adequate
  (covers request edge / full native for pdfimage).

### Prior 126
- **Bug:** `//pdfimage:` tile requests fell through to `path_from_file_uri` (strips
  pipes) → Vips opened the **PDF file** at ~72 dpi page 1 — not the embedded
  Image XObject → extremely low-res “pages”.
- **Fix:** interactive + pyramid `EnsureTiles` branches for `parse_pdf_image_uri`:
  `pdf_rasterize_embedded_image(..., max_edge=0)` then `build_tile_cell_rgb` /
  `build_tile_pyramid_rgb`.
- `EnsurePixels` ladder: rasterize **native** then encode with `edge_limit` (no
  pre-scale in MuPDF that discarded detail before JXL).
- Defense: `build_tile_cell` refuses PDF/DjVu/EPUB paths (pyramid already did).

### Prior 125
- Define missing `with_pdf_image` / `with_pdf_images` in `src/uri.cpp` (declared in
  uri.hpp, used by test_uri; link failed with undefined reference).

### Prior 124
- Document-wide xref scan for Image XObjects (mutool-style); skip ImageMask
- Load via `pdf_new_indirect` + `pdf_load_image`
- biltoo: soft-miss await ladder for pdfimage; tooltip on permanent fail



### Shipped
- Embedded image size via PDF dict `/Width` `/Height` (no stream decode).
- Locate image by `pdf_to_num` + `pdf_load_object` (no borrowed page-resource ptrs).
- `request_size` / `request_pixels` register `//pdfimage:` locators (member_path `pdfimage:N`).
- biltoo: treat `//pdfimage:` like page refs for probe/placeholder; preparePaths skip leaves;
  ImageLoader load/thumbnail paths for pdfimage ladder.

### biltoo pairing
Stack **biltoo-354** with this tip.

---

## Plan / work — bundle `thumtoo-128-pdf-text-layer`

### Goal
First slice of the semantic text layer (paired with biltoo-357 plan):

- Extract **text regions** (line-level strings + axis-aligned bboxes in page
  space / 72 dpi points) and **link regions** from PDF via MuPDF `fz_stext` +
  `fz_load_links`.
- Extract **document outline** via `fz_load_outline`.
- Public types in `thumtoo/text.hpp`; MuPDF implementation; thin `pdf_*`
  facade. No SQLite cache yet (always-cache comes once the extract API is
  solid). Coordinates = same page space as `fz_bound_page` / media box points.

### Done criteria
- [x] `TextRegion` / `PageTextLayer` / `DocumentOutline` types (`text.hpp`)
- [x] `mupdf_page_text_layer` + `mupdf_document_outline`
- [x] `pdf_page_text_layer` / `pdf_document_outline` (MuPDF path; Poppler later)
- [x] Unit test `test_pdf_text` with embedded mutool fixture (Hello)
- [ ] Build/run verification on host with MuPDF (sandbox lacked lib)
- [ ] TODO/AGENTS handoff; next **129** (DjVu text or cache)

### Non-goals this bundle
- DjVu / EPUB extract
- Durable SQLite cache of text layers
- biltoo consumer UI

---

## Plan / work — bundle `thumtoo-129-djvu-text-layer`

### Goal
DjVu side of the semantic text layer: word zones from the hidden text layer,
hyperlink mapareas, and document outline (bookmarks) when present.

### Done criteria
- [x] `djvu_page_text_layer` — word regions + maparea links
- [x] `djvu_document_outline` via `ddjvu_document_get_outline`
- [x] Bboxes in native page pixels, origin bottom-left (documented)
- [x] Smoke test `test_djvu_text` (optional fixture via `THUMTOO_TEST_DJVU`)
- [ ] Host verify with a real OCR'd DjVu
- [ ] next **130** (EPUB text or SQLite cache)

### Notes
- Coordinates differ from PDF (pixels vs points; both Y-up / bottom-left).
  Consumers must use `page_bounds` + format-specific scale rules.
- Empty text layer is valid (image-only DjVu).

---

## Plan / work — bundle `thumtoo-130-epub-text-layer`

### Goal
EPUB side of the semantic text layer: line regions + links after MuPDF layout,
with `layout_key` from `format_epub_layout_params`. Outline via `fz_load_outline`.

### Done criteria
- [x] `epub_page_text_layer` (layout-bound geometry + layout_key)
- [x] `epub_document_outline`
- [x] Smoke test `test_epub_text` (`THUMTOO_TEST_EPUB` optional)
- [ ] Host verify on a real EPUB
- [ ] next **131** — SQLite always-cache of text layers

### Notes
- Invalidate / regenerate when EPUB Layout changes (biltoo already rewrites
  `//epub:` params).
- Bboxes: page space points after layout (Y up); scale by dpi/72 for pixels.

---

## Plan / work — bundle `thumtoo-131-text-layer-cache`

### Goal
Always-cache text layers + outlines in SQLite (schema v3).

### Done criteria
- [x] schema_version 3: `text_layers`, `document_outlines`
- [x] binary serialize/deserialize for PageTextLayer + DocumentOutline
- [x] `extract_page_text_layer` / `extract_document_outline` URI dispatch
- [x] Database upsert/find; purge drops text rows
- [x] Client `get_*` (cache-only) + `ensure_*` (extract + store when content_id known)
- [x] Unit test serialize + DB round-trip
- [ ] Host build verify
- [ ] next **132** — biltoo consumer / async request API

### Notes
- Cache key: `(content_id, page_1based, layout_key)`; EPUB layout_key from
  `format_epub_layout_params`. Empty layout_key for PDF/DjVu.
- Without content_id, extract still works but is not stored.

---




## Plan / work — bundle `thumtoo-122-pdfimage-keep-obj`

Fix borrowed `pdf_obj*` lifetime for embedded image size/raster.

### Done criteria
- [x] keep/drop target obj in size + rasterize
- [x] grey pixmap_to_rgb path
- [ ] next **123** after human verify

---

## Plan / work — bundle `thumtoo-118-pdfimages-collection`

`//pdfimages` collection expand to `//pdfimage:1..N`.

---


## Plan / work (2026-09-09) — bundle `thumtoo-112-pixel-filters-design`

### Change
Document future **pixel filters** vs **layout/container pipes** (design only).

### Done criteria
- [x] Design notes in TODO.md; next **113**

---

## Design notes — pixel filters (future)

Not implemented. Captures the 2026-09-09 brainstorm so URI/cache work does not
paint us into a corner.

### Two layers (keep separate)

| Layer | Role | Examples |
|-------|------|----------|
| **Container / layout** | How a document becomes pages / members | `//epub:w,h,fs,…`, `//page:N`, `//archive:…`, PDF backend pipes |
| **Pixel filter** | Appearance of **one** decoded raster | crop, invert, grey, rotate, flip |

`//epub:` is **not** a pixel filter: it changes pagination and text flow.
Pixel filters hang off the **leaf** (the session path that is one image), not
the bare container file alone (unless expand-time inheritance is explicit).

### Suggested pipe order (outer → inner)

```text
file → //archive:…? → //epub:…? → //page:N? → //pixel-filters*
```

Canonical formatting should emit filters in a fixed order so cache keys are
stable (same lesson as `//epub:` key order).

### Candidate filters (first cut)

Geometry / framing:
- `//crop:x,y,w,h` — pixels, origin top-left of full raster (normalized 0–1 later?)
- `//rotate:0|90|180|270`
- `//flip:h|v|hv`

Tone / colour:
- `//invert`
- `//grey` (alias `//gray`)
- optional later: `//bright:`, `//contrast:`, `//gamma:`

Out of scope for path language (viewer chrome): slideshow, HUD, matte UI.

### Containers (zip / PDF / EPUB) producing many images

**Rule:** a pixel filter applies to **one raster identity**.

1. **Filter on a leaf** (clear, preferred for shareable paths):
   ```text
   archive.zip//archive:scans/001.png//crop:10,10,800,1200//invert
   book.pdf//page:3//grey
   book.epub//epub:w=900,h=1350,fs=15//page:3//invert
   ```
   Expand does not need to understand invert; cache key is the full locator.

2. **Filter on the container** (ambiguous — pick a policy):
   ```text
   archive.zip//invert
   book.pdf//grey
   ```
   - **A. Expand-time inherit** — every produced leaf gets the filter suffix  
     (“open this CBZ inverted”). Preferred for location-bar / Open semantics.
   - **B. Reject** — invalid until expanded.
   - **C. Viewer-only** — session display modifier, not part of the path  
     (global “Invert all” without rewriting paths).

   Recommendation: **A for open/expand and shareable URIs**; **C for
   ephemeral UI toggles** (optional “materialize into path” later).

3. **EPUB** — layout pipe stays document policy; pixel filters only on
   `…//page:N` (or inherited onto each page at expand). Do not interleave
   `//invert` before `//epub:`.

### Identity and cache

- **Content id** = hash of source file bytes (unchanged by filters).
- **Size / tile / ladder rows** must key off the **full locator** including
  pixel filters (otherwise invert and plain share tiles).
- Apply filters in a defined pipeline relative to the scale ladder:
  - **crop / rotate / flip** before ladder identity (geometry changes pixels);
  - pure tone filters (`invert`, `grey`) may run at decode or as a cheap
    post-step on displayed tiles — document the choice when implementing so
    LQIP/thumbs match the filmstrip.

### Relation to biltoo session transforms

Biltoo already has interactive rotate/flip/crop. Long-term options:

1. URI filters are the **durable / shareable export** of those transforms; or
2. URI filters are **load-time** (thumtoo) and session transforms stay
   biltoo-only until “Copy path” materializes them.

Prefer (1) when paths are shown in the location bar so strip/copy/round-trip
match what is on screen.

### Open questions

- [ ] Crop in pixels vs normalized 0–1 (normalized survives scale better)
- [ ] Filter before vs after ladder decode for tone-only ops
- [ ] LQIP / thumbs always follow full filtered path?
- [ ] Multi-frame / animated sources: whole file vs current frame
- [ ] Strict unknown-filter rejection vs ignore
- [ ] Full param normalization when formatting filter lists

### Implementation checklist (when started)

- [ ] Parse/format helpers for pixel-filter pipes (canonical order)
- [ ] Client decode path applies filters; cache keys include them
- [ ] Expand-time inherit when filter suffix is on a container path
- [ ] Docs page (e.g. `docs/FILTERS.md`) + URI examples
- [ ] biltoo: location bar / session path rewrite for crop-invert; optional
      global viewer modifiers (policy C)

---


## Plan / work (2026-09-09) — bundle `thumtoo-109-pdf-nopoppler-build`

### Change
Gate Poppler-only body of `pdf_render_tile_cell` so MuPDF-only configures
compile (cache TLS lived under `#if HAVE_POPPLER`).

### Verify
- [x] `libthumtoo` builds without Poppler
- [x] `thumtoo-test-uri` ok (EPUB w/h/fs + margin roundtrip)
- [x] Docs; next **110**

---
## EPUB via MuPDF — **thumtoo-103**

- [x] Design [docs/EPUB.md](docs/EPUB.md)
- [x] URI `//epub:w,h,fs` (pixels + font size pt) + `//page:N`; format classify `.epub` before zip
- [x] `epub.cpp`: layout, page count, layout size, region/tile raster (MuPDF)
- [x] `expand_media_uris` default profile pages
- [x] Client size probe + live tiles + LQIP
- [x] EnsurePixels ladder + tile pyramid prewarm for EPUB
- [x] prepare_paths + is_likely_epub_path; bare .epub refuses Vips
- [x] Galapix open/expand via expand_media_uris; tile min_scale for EPUB
- [ ] User CSS / presets beyond defaults

---

## PDF dual backend (MuPDF + Poppler) — **thumtoo-094+**

- [x] Design [docs/PDF_BACKENDS.md](docs/PDF_BACKENDS.md)
- [x] URI: `//page:`, `//poppler-page:`, `//mupdf-page:` + `PdfBackend`
- [x] MuPDF module + cmake/flake (`THUMTOO_HAVE_MUPDF`)
- [x] Dispatch render/count/stats by backend
- [x] Default `//page:` → MuPDF when available (`pdf_resolve_backend`)
- [x] Galapix min_scale + URI backend (galapix-165)
- [x] MuPDF image coverage via fz_stext image blocks
- [x] Implement `with_pdf_page_poppler` / `with_pdf_page_mupdf` (link fix for test_uri)
- [x] Silence `-Wclobbered` in `pdf_mupdf.cpp` via MuPDF `fz_var` (not C++ `volatile` on structs)
- [x] `is_pdf_page_uri` recognizes `//poppler-page:` / `//mupdf-page:`
- [x] test_pdf_tiles: denser fixture text (avoid sparse-text image_heavy gate)
- [x] MuPDF region scissor is device-space (fix white bottom tiles)

Tip: **thumtoo-106**.

## Plan / work (2026-09-09) — bundle `thumtoo-107-epub-layout-pixels`

### Change
- `//epub:` **w/h are pixels** at `kEpubLayoutDpi` (was points). Converted to
  points only at `fz_layout_document`.
- Font size key renamed **`fs=`** (points). Dropped `em=` — CSS rules often
  size margins in `em`, so changing font size looked like a margin control.
- Defaults: 1200×1800 px, fs=12 (same physical page as old 600×900 pt @ 144 dpi).
- Canonical emit order always `w,h,fs` for stable cache keys from format().

### Follow-ups
- [x] Per-side margins (`mt`/`mr`/`mb`/`ml`) via injected user CSS
- [x] Full layout-param normalization on parse so `fs=12,w=10` and `w=10,fs=12`
      become the same cache key even for hand-written URIs
- [ ] Optional minimal user CSS (kill/replace MuPDF default sheet)

### Done criteria
- [x] pixels w/h + fs in URI/API/docs/tests
- [x] Docs; next **108**

## Plan / work (2026-09-09) — bundle `thumtoo-108-epub-margins`

### Change
Per-side margins on `//epub:` layout: `mt`, `mr`, `mb`, `ml` (pixels at
layout DPI). Applied via `fz_set_user_css` as
`body { margin: Tpt Rpt Bpt Lpt !important; }` before `fz_layout_document`.
Default remains no extra margin (empty user CSS). Format emits the four
keys only when at least one is non-zero.

### Still open
- [x] Full layout-param normalization on parse (key order independence)
- [ ] Optional minimal / replacement user CSS beyond margins

### Done criteria
- [x] mt/mr/mb/ml in URI + CSS application
- [x] Docs/tests; next **109**

---


---


---

## PDF image-heavy gate for live tiles — **thumtoo-093**

- [x] `PdfPageContentStats` + `pdf_page_allows_live_tiles`
- [x] Optional poppler-glib image coverage; text heuristic fallback
- [x] Refuse scale < 0 render for image-heavy pages
- [x] Full-page cache only at scale ≥ 0
- [x] Bundle thumtoo-093

---

## PDF page/raster TLS cache — **thumtoo-092**

- [x] TLS `poppler::page` cache (avoid create_page per cell)
- [x] Full-page RGB cache ≤4096 long edge for scanned multi-tile pages
- [x] Document: no PDF “scanned” metadata; region re-decode cost
- [x] Bundle thumtoo-092

---

## Archive batch parallel encode — **thumtoo-090**

`thumtoo-bench --tiles foo.rar` looked "stuck" with low CPU: coalesce ran
one sequential RAR extract then every pyramid on a single worker.

- [x] Hit extract_cache before opening the archive again
- [x] Parallelize post-extract probe/tile/pixel work across pool threads
- [x] Bench help notes solid RAR / --tiles cost
- [x] Bundle thumtoo-090

---

## Fix: LQIP must not starve tile workers — **thumtoo-088**

Inline Handsum after the first tile blocked the same worker from encoding
more cells → black/empty until the queue drained. LQIP is now a separate
low-priority `EnsureLqip` job after durable tile store.

- [x] `request_lqip` / `JobKind::EnsureLqip`
- [x] Remove inline post-tile Handsum from `handle_ensure_tiles`
- [x] Bundle thumtoo-088

---

## LQIP after first thumbnail (2026-09-09) — **thumtoo-086**

- [x] No LQIP on size probe (archive path cleaned)
- [x] Generate LQIP after first interactive durable tile
- [x] EnsurePixels still stores LQIP from in-memory RGB after ladder
- [x] Bundle thumtoo-086

---

## Code: Option A + LQIP + LRU + tile source (2026-09-09) — **thumtoo-083**

- [x] `build_tile_cell` / `_buffer`: JPEG `scale > 0` → `vips_jpegload(shrink=N)`
- [x] Size probe no longer generates LQIP (ensure_lqip / EnsurePixels only)
- [x] Extract cache size-based LRU (no clear-all)
- [x] `tiles.source` + `TileSource` enum; migrate via PRAGMA table_info
- [x] Bundle thumtoo-083

---

## DjVu blank pages → white tiles (2026-09-09) — **thumtoo-075**

Some intentional blank pages fail `ddjvu_page_render` (returns 0). That became
nullopt → Galapix purple missing-tile placeholder. Emit solid white RGB instead.

- [x] Full-page + region render: white on render failure
- [x] Bundle thumtoo-075

---

## DjVu tile Y-direction (2026-09-09) — **thumtoo-074**

Pages shifted upward when zooming in: ddjvu default y-direction is bottom-up
(PostScript). Tile crops used top-down (image) coordinates without setting
`ddjvu_format_set_y_direction(fmt, 1)`.

- [x] Set y_direction + row_order top-to-bottom on all page renders
- [x] Bundle thumtoo-074

---

## BUG: never open PDF/DjVu via Vips/Magick (2026-09-09) — **thumtoo-073**

Root cause of 60GB+: `vips_image_new_from_file` / `vips_thumbnail` on a
`.djvu` goes through ImageMagick, which decodes multipage DjVu at full
resolution (see prior stack: ReadDJVUImage → ddjvu_page_render).

- [x] Refuse PDF/DjVu in probe_image_file, build_ladder, lqip_from_file, build_tile_pyramid
- [x] ensure_pixels / size probe: fail bare container URIs (page_uri_required)
- [x] Pyramid: cell-by-cell, no full native RGB
- [x] Bundle thumtoo-073

---

## DjVu: one shared document + Vips concurrency 1 (2026-09-08) — **thumtoo-071**

TLS per-worker document cache opened the same multipage book N times (RAM thrash).
Vips concurrency × worker pool compounded threads/memory on --ladder.

- [x] Process-wide DjVu document cache, all API under one mutex
- [x] vips_concurrency_set(1)
- [x] Bundle thumtoo-071

---

## Fast size probe for multipage docs (2026-09-08) — **thumtoo-070**

Opening a 250-page DjVu was still extremely slow at "Probing image sizes":
each page re-hashed the whole file and rasterized for LQIP.

- [x] Cache sha256_file_hex by path+mtime
- [x] Size probe: dimensions only for PDF/DjVu (no per-page LQIP raster)
- [x] Bundle thumtoo-070

---

## LQIP: no Magick on PDF/DjVu containers (2026-09-08) — **thumtoo-069**

Size probe / ensure_lqip used path_from_file_uri which strips //page: and fed
the .djvu/.pdf path to Vips→Magick→ddjvu_page_render (whole doc, GUI stall).

- [x] ensure_lqip: per-page small raster for PDF/DjVu
- [x] size probe LQIP: same; skip Magick for page URIs
- [x] Bundle thumtoo-069

---

## CMake feature summary (2026-09-08) — tip **thumtoo-067** / bundle **thumtoo-067**

- [x] Configure-time feature summary (PDF / DjVu / curl / archive / vips / sqlite)
- [x] Bundle thumtoo-067

---

## expand_media_uris + PRIVATE decoder link (2026-09-08) — tip **thumtoo-066** / bundle **thumtoo-066**

- [x] `thumtoo::expand_media_uris` / `is_openable_media_path` (PDF, DjVu, archive, image)
- [x] Link Poppler / DjVuLibre / libcurl **PRIVATE** (feature macros stay PUBLIC)
- [ ] Galapix uses expand API; drops format-specific expand
- [x] Bundle thumtoo-066

---

## DjVu multipage: require DjVuLibre discovery (2026-09-08) — tip **thumtoo-065** / bundle **thumtoo-065**

Symptom: only the first page of a multipage .djvu appears. Galapix compile
flags showed THUMTOO_HAVE_POPPLER but **not** THUMTOO_HAVE_DJVU — thumtoo was
built without finding `ddjvuapi.pc`, so `djvu_page_count` always returns null
and page expansion never runs (single URL → first page via other paths).

- [x] CMake: clearer WARNING when ddjvuapi missing; try `djvulibre` pc name
- [x] flake: `djvulibre.dev` on PKG_CONFIG_PATH
- [x] page_count: extra message pump after decode
- [x] Bundle thumtoo-065

---

## DjVu pages via DjVuLibre (2026-09-08) — tip **thumtoo-064** / bundle **thumtoo-064**

Mirror PDF page support for `.djvu` / `.djv` using **ddjvuapi** (DjVuLibre).

- `is_djvu_path` / PathKind::Djvu / MIME `image/vnd.djvu`
- `//page:N` URIs (same pipe as PDF); `parse_pdf_uri` only matches `.pdf`
- Size probe, live RGB888 tiles, durable JPEG ≥ `kPdfMinDurableTileScale`
- Thread-local document cache (same worker model as Poppler)
- Optional: `pkg-config ddjvuapi` → `THUMTOO_HAVE_DJVU`

- [x] Code
- [x] Bundle thumtoo-064

---

## Interactive tiles: FIFO queue (2026-09-08) — **thumtoo-063**

LIFO (`enqueue(..., front=true)`) starved older EnsureTiles under continuous
pan/zoom — Galapix saw permanent REQUESTED with no fail/abort.

### Fix
- `request_tile` / `request_tiles` enqueue **FIFO**
- Same-cell supersede still drops obsolete single-cell pending jobs (nullopt)
- Rebased onto origin (PDF document cache already on master)

- [x] Code
- [x] Bundle thumtoo-063

---

## PDF: thread-local document cache (2026-09-08) — **thumtoo-061**

Interactive PDF tiles called `poppler::document::load_from_file` on **every**
cell (layout size + region render + optional full-page fallback). A viewport
of N tiles reopened the same PDF N–3N times.

### Fix
- `thread_local` open-document cache keyed by path + mtime (Poppler is not
  cross-thread safe; matches Client worker model)
- Cache media-box size at 72 dpi per path+page for layout queries

- [x] Code
- [x] Bundle thumtoo-061

---

## Index-based tile batch completion (2026-09-08) — **thumtoo-060**

Fresh-generate tiles could stay REQUESTED forever: batch used one shared
TileCallback that re-matched scale/x/y; missed matches never completed
Galapix JobHandles. `request_tiles` now calls `on_cell(index, tile)`.

- [x] Code
- [x] Bundle thumtoo-060

---

## Unstick interactive tile batch (2026-09-08) — **thumtoo-059**

Pending requests could sit at ~100 after batch path:

1. Every cell re-ran `handle_probe_size` (LQIP backfill could re-encode full image)
2. Exception on one cell aborted the rest with JobHandles left REQUESTED

### Fix
- Probe **once** per `request_tiles` batch; children set `skip_probe`
- try/catch per cell + nullopt reply so every JobHandle completes

- [x] Code
- [x] Bundle thumtoo-059

---

## Interactive tiles: reply before durable JPEG (2026-09-08) — **thumtoo-058**

### Root cause
Interactive `EnsureTiles` JPEG-encoded and wrote SQLite **before** `reply_one`.
After the shrink ladder held the image, each cell still paid encode+store
before the next cell could run — ~1s trickle for a zoomed grid.

### Fix
- Reply RGB (or cache hit) **first**
- `request_tiles` batch sets `skip_durable` and flushes JPEG/SQLite **after**
  every cell has been replied

- [x] Code
- [x] Bundle thumtoo-058

---

## Interactive multi-cell request_tiles batch (2026-09-08) — **thumtoo-057**

`Client::request_tiles(uri, coords, cb)` enqueues **one** EnsureTiles job for
many cells of the same image. The worker shares size-probe / shrink-ladder
work instead of N competing queue entries. Galapix deep-zoom uses this so
visible tiles arrive together instead of trickling over ~1s.

- [x] API + handle_ensure_tiles batch loop
- [x] Bundle thumtoo-057

---

## Interactive tile queue LIFO + coalesce (2026-09-08) — **thumtoo-056**

`request_tile` (single-cell interactive) enqueues at the **front** of the
worker queue so the latest view is processed before a backlog of intermediate
pan/zoom cells. Older pending jobs for the same (uri, scale, x, y) are dropped
and complete with empty. `request_tile_pyramid` / size / pixels stay FIFO
(`push_back`).

- [x] deque + enqueue(front)
- [x] Bundle thumtoo-056

---

## Optional unrar/unzip extract backends (2026-09-08) — backlog

libarchive covers most zip/rar/7z/tar. Gaps worth a future thumtoo path:

1. **unrar** for RAR subformats libarchive cannot open
2. **External unrar/unzip** for single-member extract (often faster than
   iterating the whole archive with libarchive)

Keep behind capability detection; default remains libarchive.
Galapix no longer ships arxpcpp — any such backends belong here.

- [ ] Design capability probe + fallback order
- [ ] unrar single-member extract
- [ ] unzip single-member extract (optional)

---

## Fix Database GC methods outside namespace (2026-09-08) — **thumtoo-054**

Same class of bug as thumtoo-053: GC helpers from thumtoo-052 were appended
after `} // namespace thumtoo` in `database.cpp`.

- [x] Move methods inside `namespace thumtoo`
- [x] Bundle thumtoo-054

---

## Fix BlobStore GC methods outside namespace (2026-09-08) — **thumtoo-053**

`delete_tiles_below_scale` / `delete_tiles_for_content` /
`delete_levels_for_content` were appended after `} // namespace thumtoo` in
`blob_store.cpp` (thumtoo-052), so the compiler saw free functions and
`BlobStore` was undeclared.

- [x] Move methods inside `namespace thumtoo`
- [x] Bundle thumtoo-053

---

## thumtoo-gc (2026-09-08) — **thumtoo-052**

Manual cache cleanup CLI (no automatic eviction):

```
thumtoo-gc --dry-run --min-scale 3 --orphans --dead-paths
```

- `--min-scale N` — drop tiles with scale < N (keep coarser overview)
- `--orphans` — content with no locators + blob purge
- `--dead-paths` — locators whose outer file is gone

- [x] Database/BlobStore GC helpers
- [x] tools/thumtoo_gc.cpp + CMake/flake

---

## LQIP backfill when size already known (2026-09-08) — tip **thumtoo-045**

Probe early-return skipped LQIP for content that already had width/height
(typical warm cache from before ThumbHash). Backfill on that path +
`Client::ensure_lqip()` for explicit fill.

### Status
- [x] Bundle thumtoo-045

---

## HTTP probe LQIP + DESIGN note (2026-09-08) — tip **thumtoo-044**

Encode ThumbHash during HTTP size probe (cached body). Document LQIP stack in
DESIGN.md.

### Status
- [x] Bundle thumtoo-044

---

## Archive-member LQIP on probe (2026-09-08) — tip **thumtoo-043**

`lqip_thumbhash_from_buffer` + encode ThumbHash during archive member size
probe (bytes already in memory).

### Status
- [x] Bundle thumtoo-043

---

## LQIP on size probe + unit test (2026-09-08) — tip **thumtoo-042**

Encode ThumbHash during `handle_probe_size` for local files so cold gallery
open gets inline LQIP without waiting for `request_pixels` / ladder.

Add `tests/test_lqip.cpp` encode/decode round-trip.

### Status
- [x] Bundle thumtoo-042

---

## Inline LQIP (ThumbHash) on content rows (2026-09-08) — tip **thumtoo-040**

### Goal
Extremely small gallery placeholders stored **on the content row** (~25–37 B
ThumbHash) so first paint for ~1000 images does not touch blob storage.

### Done
- Schema v2: `content.lqip` BLOB + `content.lqip_kind`
- ThumbHash encode/decode (`lqip.hpp` / `lqip.cpp`)
- `Database::get_lqip` / `set_lqip`
- `Client::get_lqip(uri)` cache-only
- Encode after successful ladder build (file path + PDF rgb)

### Next (Galapix)
- Paint LQIP under overview when present
- Optional: request_pixels only after LQIP shown / larger on-screen size

### Status
- [x] thumtoo-040 bundle

---

## tests: request_tile callback expects rgb888 (2026-09-08) — tip **thumtoo-039**

Interactive `request_tile` returns `codec=rgb888`; durable `get_tile` remains JPEG.

---

## chore: drop unused encode_cell_from_level (2026-09-08) — tip **thumtoo-038**

Interactive path uses `extract_rgb_cell_from_level` only; JPEG durable encode is `encode_tile_cell_rgb` in Client.

---

## Interactive tiles: rgb888 delivery, no store→get round-trip (2026-09-08) — tip **thumtoo-037**

### Problem
Interactive `build_tile_cell*` JPEG-encoded every cell; Client then
`store_tiles` + `reply_one(get_tile(...))` re-read the blob. Galapix JPEG-decoded
again before GL upload — encode/decode on the hot path.

### Fix
* Ladder cut returns **rgb888** (crop + colourspace + write_to_memory)
* Client stores durable JPEG via `encode_tile_cell_rgb`, replies with the
  in-memory cell (no get_tile)
* Galapix already accepts `codec=rgb888` (PDF live path)

### Status
- [x] extract_rgb_cell_from_level
- [x] Client reply path
- [ ] Galapix pending-upload drain (separate)

---

## Interactive tile decode ladder cache (2026-09-08) — tip **thumtoo-036**

### Problem
Galapix requests one (scale,x,y) cell at a time. `build_tile_cell*` reloaded the
source and re-ran the shrink chain **per cell** → extreme CPU when many
high-res tiles are missing.

### Fix
In-process **shrink-ladder cache** (up to 4 sources):

* Key: file path+mtime, or caller-supplied key for buffers (`a:archive\nmember`, `h:uri`)
* Level 0 = full decode once; coarser levels via successive `vips_shrink` ×2
* Concurrent cells for the same source share the ladder (mutex per entry)
* Single-cell encode is crop + JPEG only after the level exists

`build_tile_cell_buffer(..., decode_cache_key)` optional key; Client passes
keys for archive members and HTTP bodies.

### Status
- [x] Ladder cache in image.cpp
- [x] Client cache keys for archive / HTTP
- [ ] Bundle / land for Galapix flake bump

---

## request_tile: no sync cache hit on caller thread (2026-09-07) — tip **thumtoo-035**

### Symptom (Galapix)
Frame drops when zooming: `Image::draw` → `request_tile` → Client did
`get_tile` (SQLite + blob read) on the GUI thread; default Executor then ran
the completion (JPEG/rgb888 decode) **inline** on that same thread.

### Fix
`Client::request_tile` always enqueues `EnsureTiles`. Cache hits are resolved
inside `handle_ensure_tiles` on a worker, then `executor_.post` delivers the
callback (inline on worker with default Executor; GUI-marshaled if the host
installs a queue).

### Note
`get_tile` remains available for tools/CLI that intentionally want a sync read.
`request_size` still fast-paths meta in memory on the caller (no blob I/O).

---

## Galapix ResourceDatabase → thumtoo gaps (2026-09-07) — tip **thumtoo-034**

Galapix removed its `ResourceDatabase` / `cache4.sqlite3` resource index
(galapix-068). View path already used thumtoo only. The following features
existed in Galapix schema or pipeline stubs but were **unfinished or unused
in the viewer**; implement in thumtoo when needed rather than resurrecting
cache4.

### Coverage already in thumtoo

| Need | thumtoo |
|------|---------|
| Path + mtime + size | `locators` |
| Content identity | `content_id` (sha256 preferred) |
| Image WxH | `content.width/height`, `get_size` |
| Archive member list | `archive_entries`, `//archive:` URIs |
| HTTP(S) sources | locators + optional curl fetch |
| Tags | `tags` table |
| Grid tiles + ladder | `tiles` / `levels` + blob store |
| PDF pages | `//page:N`, live rgb888 + durable JPEG |

### Still to implement in thumtoo (from Galapix ResourceDatabase)

1. **Archive passwords**  
   Galapix `archive.password` column — never wired through the view path, but
   needed for encrypted ZIP/RAR/7z. Design: store password material only in a
   host-controlled secret store or session; optional `locator` / archive meta
   flag that a password is required, not the password itself in the shared
   cache if multi-user.

2. **Video / still metadata beyond duration**  
   Galapix `video` table: width, height, duration, **aspect_ratio**. thumtoo
   has `duration_ms`, `still_count`; add aspect (or derive from WxH) and
   multi-frame still policy if video browsing becomes a product goal.

3. **Remote URL content-type / HTTP validators**  
   Galapix `url.content_type`, `url.mtime`. thumtoo HTTP path should persist
   Content-Type and validators (ETag / Last-Modified) on the locator or a
   small `http_meta` table for revalidation.

4. **Explicit resource “handler” / pipeline status**  
   Galapix `resource.type`, `handler`, `arguments`, `status` modeled a
   multi-stage job graph. Prefer thumtoo `content.status` + `error_code` +
   format; avoid a parallel handler table unless job orchestration returns.

5. **Directory / collection UX index** (optional product)  
   thumtoo already has `directory_snapshots` / `directory_entries`. Galapix
   never finished a full library UI on cache4; grow thumtoo listing APIs
   instead of a Galapix SQL index.

### Explicit non-goals

* Galapix tile SQLite (`cache4_tiles`) — already replaced by thumtoo tiles.
* Galapix SHA-1 blob ids as primary keys — keep sha256 content_id.
* Resurrecting `DatabaseThread` / `FileEntryGenerationJob` in Galapix.

### References

* Galapix `docs/CACHE4_VS_THUMTOO.md` (removal history + matrix)
* Removed Galapix tables: `file`, `blob`, `image`, `archive`, `archive_file`,
  `url`, `video`, `resource`

---

## Live PDF tiles + negative scale investigation (2026-09-07) — tip **thumtoo-031**

### Symptom
Cached PDF tiles (JPEG from durable store) render in Galapix. Live /
interactive `request_tile` (especially negative scale, or any PDF cell that
bypasses cache and returns **uncompressed** pixels) does not.

### thumtoo-side findings (verified with `thumtoo-test-pdf-tiles` + `thumtoo-tile`)

| Path | Behaviour |
|------|-----------|
| Interactive `request_tile` for `//page:N` | Always builds via `pdf_render_tile_cell` → replies **`codec=rgb888`** raw RGB888 in the callback `TileBlob`. |
| Durable store | Only if `scale >= kPdfMinDurableTileScale` (−2). Stored blob is **JPEG** (`kPdfTileQuality`). |
| `get_tile` after store | Returns the **JPEG** row (not rgb888). |
| Scale &lt; −2 | Live-only; never written to `tiles` / `tile_blobs`. |
| Negative scale geometry | `full = layout * 2^{-scale}`, `dpi = 144 * 2^{-scale}`, crop `(x*T, y*T, tw, th)` on that grid. Matches TILES.md; Poppler region + soft-crop fallback both produce correct 256² (or edge) cells. |
| `get_tile_coverage` | Reflects **stored** min/max only. Theoretical fallback starts at min_scale=0. Apps must request negative scales explicitly; coverage does not advertise them. |

Library path is consistent. Manual checks with Ghostscript test PDF:

* scale 0 / −1 / −2 / −3 → non-empty rgb888 256×256 (or edge size)
* scale −3 → `has_tile` stays false
* `pdftoppm` full-page vs region crop align on the same pixel grid

### Root cause (most likely Galapix)

`INTEGRATION_GALAPIX.md` documents the adapter as:

> misses use `request_tile` (JPEG → `surf::jpeg::load_from_mem`)

The callback payload for **every** interactive PDF tile is now **rgb888**, not
JPEG. JPEG-decoding raw RGB bytes fails (or yields garbage). Cache hits still
go through `get_tile` → JPEG → works. That matches “cached works, live does
not”.

Negative scale is a special case of the same path: those cells are often
live-only (or first miss), so they always hit the rgb888 reply.

### What Galapix must do

In `ThumtooTileProvider` (or equivalent):

1. Inspect `TileBlob::codec`.
2. If `codec == "rgb888"` (or `kTileCodecRgb888`): treat `bytes` as tightly
   packed RGB888, width×height from the blob meta; upload to GL / software
   surface **without** JPEG decode.
3. If `codec == "jpeg"` (or empty/default): keep existing `surf::jpeg::load_from_mem`.
4. To zoom past 1:1 on PDF pages, request `scale < 0` (layout is 144 dpi;
   scale −1 = 288 dpi, −2 = 576 dpi). Do not rely on `get_tile_coverage` for
   negative min_scale.

### Tests added this tip

* `tests/test_pdf_tiles.cpp` (`thumtoo-test-pdf-tiles`): end-to-end Client path
  for scales 0, −1, −2, −3; edge tiles; out-of-range; durable vs live-only;
  codec contract; geometry helpers. Embeds a minimal PDF-1.4 blob (no
  Ghostscript) so CI/nix only need Poppler.
* Existing `test_pdf_scale` remains the pure math check.

### Still open (not a thumtoo bug)

* Galapix adapter rgb888 branch (required for live PDF).
* Galapix requesting negative scales when zoomed past layout 1:1.
* Optional: expose theoretical min_scale for PDF in coverage (e.g. always allow
  down to `kPdfMinDurableTileScale` or a configurable live floor). Discuss
  before changing API semantics.

### Key files

* `src/client.cpp` — PDF branch of `handle_ensure_tiles` (live rgb888 reply)
* `src/pdf.cpp` — `pdf_render_tile_cell` / region raster
* `include/thumtoo/constants.hpp` — `kTileCodecRgb888`, `kPdfMinDurableTileScale`
* `tests/test_pdf_tiles.cpp`, `tools/thumtoo_tile.cpp` (`--raw-pdf`, codec-aware PNG out)

---

## PDF region tiles + negative scale (2026-09-07) — tip **thumtoo-030**

Interactive PDF tiles: `pdf_build_tile_cell` region-rasterizes a single cell at
`dpi = kPdfLayoutDpi * 2^{-scale}`. Negative scale is sharper than layout;
peak memory stays O(tile), not O(full page × dpi).

* `pdf_page_size_at_scale`, `pdf_dpi_for_scale`, `pdf_rasterize_page_region`
* `encode_tile_cell_rgb` for any scale
* `Client` single-cell PDF path uses region builder (not full-page + crop)

Pyramid prewarm still full-page at layout for scales ≥ 0 (unchanged).

Galapix follow-up: request tilescale &lt; 0 when zoomed past 1:1 on PDF pages;
**and** handle `codec=rgb888` on the live callback (see tip 031).

## Session handoff (2026-09-07) — tip **thumtoo-027**

Apply tip **`thumtoo-029.bundle`** (or stack 016…023). Author: Ingo Ruhnke
`<grumbel@gmail.com>` + `Co-authored-by: Grok <grok@x.ai>`.

### What landed this session (API / retrieval)

| Bundle | Change |
|--------|--------|
| thumtoo-016 | **Location URI API**: `parse_location` / `format_location`, nested `//archive` + `//page`, http/content-id helpers, `with_archive_member` / `with_pdf_page`; `Client::list_locators` / `find_locator`; `tests/test_uri` |
| thumtoo-017 | **Content-id resolve**: `meta_for_content_id`, `list_locators_for_content_id`, `meta_for_uri` accepts `sha256:`/`sha1:`; `Client::resolve_content_id`, `list_uris_for_content_id`, `get_meta_for_content_id` |
| thumtoo-018 | **`read_source_bytes`**: file + archive member (+ via content-id); `read_file_bytes`; size-capped |
| thumtoo-019 | **PDF live tiles**: `kPdfLayoutDpi = 144`, `pdf_page_layout_size`; probe + `request_tile`/pyramid rasterize at layout edge |
| thumtoo-020 | Fix `test_client` `read_source_bytes` scope |
| thumtoo-021 | **HTTP(S) fetch**: optional libcurl (`THUMTOO_HAVE_CURL`), `network.hpp` `http_get_bytes`; probe / pixels / tiles / `read_source_bytes`; flake + CMake |
| thumtoo-022 | **Session HTTP body cache** (`fetch_http_cached`, 512 MiB in-process) |
| thumtoo-023 | **Fix**: declare `Client::fetch_http_cached` in `client.hpp` (022 build break) |

Earlier tips (001–015) remain in the history; tip is **024** (handoff docs; code tip still 023 features).

### Product direction (do not grow Galapix SQL for this)

* thumtoo = shared **media index + display pixels + growing data retrieval**
* Nested location URIs, content-id identity, network + archive + PDF
* Query/library listing for apps (Galapix `-p` transitional)
* Galapix/biltoo/dirtoo consume; durable HTTP disk cache / TTL still future

### Build notes

* `pkg-config libcurl` → `THUMTOO_HAVE_CURL=1`; without curl, http(s) parses but fetch fails
* Poppler optional → `THUMTOO_HAVE_POPPLER` for PDF
* Tests: `thumtoo-test-uri`, database, client

### Next (priority)

1. ~~Durable HTTP/download cache + TTL~~ (025: blobs.sqlite `http_bodies`, 7d TTL)
2. ~~Higher-DPI / per-tile PDF crop render (true “mandelbrot-style” region)~~ (030)
3. ~~Query prefix/LIKE on locators~~ (026); tags/collections still open
4. Galapix flake: pin/update input to a tip that includes 021+ curl

### Key files

* `include/thumtoo/uri.hpp`, `network.hpp`, `client.hpp`, `pdf.hpp`, `constants.hpp`
* `src/uri.cpp`, `network.cpp`, `client.cpp`, `pdf.cpp`
* `DESIGN.md`, `TILES.md`, `AGENTS.md`, this TODO


<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TODO — thumtoo


## Session handoff (2026-09-07)

### Bundles
Apply in order or take tip **`thumtoo-029.bundle`**:
| Bundle | Change |
|--------|--------|
| thumtoo-001 | `request_tile` single-scale only |
| thumtoo-002 | Parallel JPEG encode per scale; prepare timings |
| thumtoo-003 | Clean `--help` |
| thumtoo-004 | **Multi-worker Client pool** (default HW concurrency); `--jobs N` |
| thumtoo-005 | **Extract cache** (512 MiB) + coalesce EnsureTiles/Pixels same archive |
| thumtoo-006 | Stats: **wall=** vs **cpu:** summed scopes + parallel~= |
| thumtoo-007 | **Single-edge preview** (not full ladder); thumb/jxl stats; Galapix-shaped cache |
| thumtoo-008 | DESIGN/INTEGRATION policy; downscale smaller preview from larger cache |
| thumtoo-009 | **Single-cell** interactive `request_tile` (not full scale grid) |
| thumtoo-010 | EXIF embedded thumb for preview; JPEG shrink-on-load for tile cells |
| thumtoo-011 | Pretty stats + `thumtoo-bench` phase benchmark tool |
| thumtoo-012 | flake apps + `nix develop` helpers (configure/build/test/run) |
| thumtoo-013 | `--min-scale` / `--max-scale` for tile prepare + bench |
| thumtoo-014 | `format.hpp`: central ext/MIME/classify for apps |
| thumtoo-015 | pdf.cpp: remove unused to_lower_ext warning |
| thumtoo-016 | **Location URI API** (parse/format nested archive+page, http/content-id); Client list_locators |

### Pixel cache policy (Galapix-first, biltoo API stable)
* **Durable:** size index + **one** JXL preview per `request_pixels(max_edge)` (largest
  `kLadderEdges` entry ≤ max_edge) + optional **tile** pyramid.
* **Not durable by default:** multi-edge ladder (128+256+512+…). Apps can
  downscale the stored preview or call `request_pixels` with another edge.
* **Original full-res:** not cached; viewers decode past max tile scale.
* Public API unchanged: `get_pixels` / `request_pixels(uri, max_edge, …)`.

### Measured (one ~26 MiB archive, 68 members, 4324 tiles)
* After cache/coalesce: **real ~3.6 s** with 12 workers; jpeg dominates **cpu-share**
* extract cpu-share dropped sharply vs double full-zip read
* Stats **cpu-sum ≫ wall** when parallel — expected
* Pre-007: `--ladder 256` still encoded full ladder (invisible to stats) → slow

### Done (parallelism / prepare)
* [x] `vips_concurrency_set(hardware_concurrency)`
* [x] Parallel JPEG encode of independent 256² cells per scale
* [x] Client worker pool (`open(..., worker_threads)`; 0 = auto, max 32)
* [x] Archive member extract cache across probe → tiles/ladder
* [x] Coalesce ProbeSize **and** EnsureTiles / EnsurePixels per archive
* [x] `thumtoo-prepare --stats --jobs --tiles` with clear wall vs cpu labels
* [x] Single-scale interactive `request_tile`
* [x] Single-edge `request_pixels` / `--ladder EDGE` (one `vips_thumbnail` + JXL)
* [x] Stats: `thumb=` / `jxl=` / `levels=`
* [x] DESIGN §5 + INTEGRATION_GALAPIX: preview + tiles, no double ladder
* [x] Downscale-from-larger-cached-preview when asking a smaller max_edge
* [x] Single-cell interactive `request_tile` (`build_tile_cell`)

### Next session — priority
1. [x] EXIF embedded thumb for even faster first preview
2. Optional: tune extract cache size / eviction (clear-all is crude)
3. [x] **Single-cell** tile cut (encode only requested (scale,x,y), not full scale grid)
4. Not worth yet: GPU JPEG (nvJPEG) — CPU jpeg still parallelizable; extract fixed
5. Not realistic: “cut tiles from JPEG without decode” (see below)

### JPEG region decode (design note)
Baseline JPEG is not randomly tiled. libjpeg can **DCT-scale** and limited
**skip/crop scanlines**; true per-tile extract without stream decode needs
RST markers or a different format. Prefer one decode → shrink → encode for
pyramids; use DCT-scale for overviews only.


## Done

- [x] Bootstrap documentation repo (README, DESIGN, ARCHITECTURE, AGENTS)
- [x] Link biltoo, dirtoo, galapix, dirtoo-py
- [x] Normative rules: cache-first browse, XDG-only, no source pollution, hash identity
- [x] Location URI form; video stills (`still_count` + `frame_idx`); review response
- [x] README scope row: directory snapshots vs dirtoo live listing
- [x] Phase 0 constants written down (ladder edges, JXL q=80, archive caps, WAL)

## Phase 0 remaining (thin)

- [x] Encode constants as implementable C++/header names (`constants.hpp`)
- [x] biltoo integration note (`INTEGRATION.md`)

## Phase 1

- [x] Fix prepare/drain race leaving locators pending
- [x] Ladder payloads in blobs.sqlite (not per-level files)

- [x] `include/thumtoo/` public headers + SQLite content/locator spike
- [x] CMake + flake.nix + `nix develop` shell
- [x] `thumtoo-status` CLI (inspect cache summary/locators/content)
- [x] Database open/migrate tests
- [x] `thumtoo-prepare` CLI skeleton (register paths + schedule probe)
- [x] Client API: get_size / get_meta / request_size + single worker queue
- [x] URI helpers (file:/// , //archive detection)
- [x] Image pipeline via libvips + JPEG-XL ladder (required; flake.nix)
- [x] SHA-256 content id promotion
- [x] get_pixels / request_pixels reading levels
- [x] `thumtoo-prepare` progress reporting (per-job lines + --quiet)
- [x] flake: add util-linux for gio `mount.pc` (silence pkg-config noise)
- [x] flake: add libselinux for gio `libselinux.pc` (silence pkg-config noise)
- [x] flake: add libsepol for libselinux `libsepol.pc` (silence pkg-config noise)
- [x] flake: add libthai, libdatrie, libxdmcp, libxml2 (silence pkg-config noise)
- [x] Tag API sketch reconciled with dirtoo checksum tags (`TAGS.md` + list/add/remove)

## Design open / discuss

- [x] Archive on-demand vs batch coalescing details (worker peeks same-archive jobs)
- [ ] Optional convenience Location `//frame:N` (view only)
- [ ] Whether archive caps need per-format overrides

## Phase 2 (started)

- [x] Archive TOC read (libarchive) + `archive_entries` cache
- [x] Extract member bytes + probe/ladder for `//archive:member` URIs
- [x] Size / ratio caps on extract (`kArchiveMaxMemberUncompressedBytes`)
- [x] Batch vs on-demand coalesce for archives (open once, many members)
- [x] Client shutdown clears job queue; skip re-probe when Ready
- [x] `thumtoo-prepare` archive expand (register all image members)

## Phase 4 — Grid tiles (galapix-style) — DONE (library)

Goal: provide optional **256×256 power-of-two tile pyramids** so Galapix
(develop) can consume thumtoo instead of its own SQLite `tiles` table.

Reference: galapix master `TileGenerator` + `tiles` schema
`(fileid, scale, x, y) → JPEG/PNG blob`. Scale 0 = full resolution;
each +1 halves linear size. Tile size fixed at 256.

### Design decisions (proposed)

1. **Tile size** `kTileSize = 256` (Galapix-compatible; not configurable for now).
2. **Scale convention** matches Galapix: `scale=0` full-res tiles, `scale=1`
   half linear, … up to the scale where the whole image fits in one tile.
3. **Codec** default **JPEG** (quality 80) for Galapix decode path simplicity;
   ladder stays JXL. (Revisit JXL tiles once Galapix has a JXL decoder path.)
4. **Storage**
   - Metadata rows in `index.sqlite` table `tiles`
     `(content_id, scale, x, y, width, height, codec, quality)` PRIMARY KEY
     `(content_id, scale, x, y)`.
   - Payload BLOBs in `blobs.sqlite` table `tile_blobs` with the same key.
   - Same pattern as ladder `levels` / `level_blobs` (no loose files).
5. **Schema** keep `kSchemaVersion = 1`; additive tables only (no bump) until
   a breaking change is required.
6. **API surface** (Client)
   - `get_tile(uri, scale, x, y) → optional<TileBlob>`
   - `request_tile(uri, scale, x, y, cb)` async; generates missing tiles for
     that scale (or full pyramid on prepare).
   - `get_tile_coverage(uri) → optional{min_scale, max_scale, image_size}`
   - `request_tiles(uri, min_scale, max_scale, cb)` / prepare flag.
7. **Generation** libvips: load once, successive `resize(0.5)` + crop 256²;
   partial edge tiles allowed (width/height < 256 stored).
8. **Prepare CLI** optional `--tiles` / `--tile-max-edge N` to prewarm pyramids
   (default off so biltoo path stays light).
9. **Non-goals this phase**
   - Replacing Galapix UI or OpenGL tile cache.
   - Video still tiles (images + archive + PDF page tiles done).
   - Eviction of tiles (shares future `thumtoo-gc`).

### Implementation order

- [x] `TILES.md` normative note + constants in `constants.hpp`
- [x] Schema: `tiles` in index + `tile_blobs` in BlobStore; status counters
- [x] `image.cpp`: `build_tile_pyramid(...)` → vector of tile blobs
- [x] Database / BlobStore put/get/list/min_max for tiles
- [x] Client: get/request tile + worker job type
- [x] `thumtoo-status` tile summary; tests with small fixture image
- [x] `thumtoo-prepare --tiles`
- [x] Galapix develop integration sketch (`INTEGRATION_GALAPIX.md`)

### Notes / resolved

- On-demand generates **requested scale + all coarser** in one source load.
- `kTileMaxSourcePixels` (100 MP): refuse encode above that; size probe still works.
- Galapix develop adapter remains outside this repo (see INTEGRATION_GALAPIX.md).

## Later

- [ ] Cache eviction / LRU / orphan sweep + `thumtoo-gc` / `thumtoo-status`
- [ ] Optional D-Bus daemon
- [ ] Adaptive video frame count (8–64 → still_count)
- [ ] Animated video preview level (must-have; deferred until consumers exist)


## PDF page tiles ([x] 2026-09-07)

Interactive `request_tile` and pyramid prewarm no longer stub PDF URIs.
Flow: `pdf_page_size_72dpi` → `pdf_rasterize_page` at that long edge →
`build_tile_cell_rgb` / `build_tile_pyramid_rgb` (new in image.hpp).

Known limit: layout size remains 72 dpi media box; higher-dpi native size
for readable scale-0 is a follow-up.

---

## Thumbnail generation audit + microbenchmarks (2026-09-09) — **in progress**

Goal: exhaustive audit of thumbnail / ladder / tile generation in thumtoo
(and galapix consumption), with measured numbers — not guesses.

### Deliverables
- [ ] `docs/THUMBNAIL_AUDIT.md` — file-by-file, slow vs fast ops, bugs, gaps
- [ ] Extended `thumtoo-bench` (or new `tools/microbench_*`) covering:
  - JPEG full decode vs shrink=2/4/8 vs header-only size
  - Quality comparison (PSNR/SSIM or visual samples) of shrink path
  - EXIF embedded thumb vs full `vips_thumbnail`
  - Archive extract: libarchive random member vs `unzip`/`unrar` CLI
  - Cold vs warm cache time-to-first-pixel paths
  - LQIP generation cost vs size-only probe
  - PNG/JXL/WebP vs JPEG decode costs
- [ ] Document every place that still does full-resolution work when a
  cheaper path exists
- [ ] Propose schema flag for "tile is high-quality vs fast-path" if useful

### Fast vs slow taxonomy (working)

| Operation | Expected class | Current implementation |
|-----------|----------------|------------------------|
| Image dimensions only | Fast (header) | `probe_image_*` via VIPS_ACCESS_SEQUENTIAL |
| EXIF embedded JPEG thumb | Fast | Used in `build_ladder` for JPEG when large enough |
| `vips_jpegload(..., shrink=N)` | Medium | Used in `build_tile_cell_buffer` for scale>0 JPEG |
| `vips_thumbnail` / full decode | Slow | Ladder fallback; pyramid path loads full |
| Archive TOC | Medium | libarchive sequential |
| Archive member extract | Slow | libarchive; in-process extract cache 512 MiB |
| LQIP/ThumbHash encode | Medium | Needs small RGBA raster |
| Tile JPEG encode | Medium | Parallel per scale in prepare |
| PDF/DjVu page raster | Slow | Poppler / ddjvu at requested DPI/region |

### Known design issues to verify in audit
- LQIP generated alongside size probe historically (partially fixed thumtoo-070)
- `build_tile_cell` for file path (non-buffer) may not use jpeg shrink
- Pyramid still full-loads non-JPEG
- No durable flag distinguishing fast-path vs HQ tiles
- Embedded thumbs used for ladder but not systematically for tiles/LQIP
- Galapix overview / size probe interaction with LQIP

### Progress
- [x] Read DESIGN.md, TILES.md, image.cpp probe/ladder/tile paths
- [x] Deep pass: image.cpp
- [x] Deep pass: client.cpp request_size / ensure_lqip / request_tile
- [x] Deep pass: archive.cpp extract paths (TOC + sequential extract)
- [x] Deep pass: lqip/handsum obtain path (vips_thumbnail 32)
- [x] Deep pass: pdf.cpp / djvu.cpp raster costs
- [x] Deep pass: galapix ImageOverview + ThumtooTileProvider
- [x] Microbench harness (Pillow results + C++ vips tool wired)
- [x] Initial numbers in docs/MICROBENCH_RESULTS.md
- [x] Propose JPEG shrink / LQIP / schema fixes (audit §8)
- [ ] Vips/libjpeg numbers under nix develop
- [ ] RAR / solid archive extract comparison
- [ ] Implement interactive JPEG shrink (after agreement on Option A/B)
- [ ] Decouple LQIP from size probe (after agreement)

- [x] ImageTileCache / SizeProbeSession / FIFO notes (§5.5–5.7)
- [x] Policy constants table (§7b)

- [x] Archive coalesce / warm extract skip documented
- [x] Schema + BlobStore tile path documented
- [x] ZIP stored/deflate extract numbers in MICROBENCH_RESULTS

- [x] prepare CLI / BuildStats / expand documented
- [x] Executive summary in THUMBNAIL_AUDIT.md

- [x] gc / status / format / hashing / non-thumtoo DCT contrast
- [x] Coverage checklist; audit marked complete for handoff

### Audit complete (docs)
Canonical: [docs/THUMBNAIL_AUDIT.md](docs/THUMBNAIL_AUDIT.md),
[docs/MICROBENCH_RESULTS.md](docs/MICROBENCH_RESULTS.md).

**Code follow-ups:**
- [x] Interactive JPEG shrink Option A (§8.1) — thumtoo-083
- [x] Decouple LQIP from size probe (§8.2) — thumtoo-083
- [x] Extract-cache LRU (§8.4) — thumtoo-083
- [x] Optional tile `source` column (§8.3) — thumtoo-083

**Numbers follow-ups:**
- [ ] `thumtoo-microbench-decode` under nix+vips
- [ ] Solid RAR extract comparison

Tip: **thumtoo-082**.