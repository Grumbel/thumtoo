# TODO / agent handoff

## Status (2026-09-23)

**Tip: thumtoo-334-unarr-close-no-extra-toc** (base `f71d183`, includes 324–333).

### Extra unarr opens (sizes-only solid RAR)
Log showed **4× open + 3× TOC** for one album. Expected **2** (TOC + visit).

| # | Source | Fix |
|---|--------|-----|
| 1 | `prepare_paths` → `refresh_archive_toc` | keep (writes Store) |
| 2 | `ensure_archive_cursor` → `read_archive_toc` | prefer Store `list_container_members` |
| 3 | `visit_archive_members_unarr` | keep (extract) |
| 4 | `order_uris_for_sequential_extract` after phase 1 | skip when `--sizes-only`; else Store via Client |

Also **`unarr close path=…`** on `UnarrHolder` dtor.

```bash
THUMTOO_DEBUG_ARCHIVE=1 thumtoo-prepare --sizes-only --no-cache album.rar
# expect: open → TOC → close → visit start → open → … → close
```

### Apply
```bash
git pull --ff-only …/thumtoo-334.1-unarr-close-no-extra-toc-f71d183.bundle HEAD
```

Next: **335**.
