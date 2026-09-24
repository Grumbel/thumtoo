# TODO / agent handoff

## Status (2026-09-24)

**Tip: thumtoo-337-xdg-thumbnails** (base `f71d183`, includes 324–336).

### XDG / Freedesktop thumbnails (optional path)
Standalone module — **not** wired into Client tiles/ladder/LQIP.

| Piece | Notes |
|-------|--------|
| `include/thumtoo/xdg_thumbnail.hpp` | Cache URI/path/lookup + `XdgThumbnailer` |
| Cache | Always: MD5(`file://`) under `$XDG_CACHE_HOME/thumbnails/<flavor>/` |
| D-Bus | Optional `THUMTOO_HAVE_DBUS` (libdbus-1): Thumbnailer1 `Queue` / Ready / Error |
| CLI | `thumtoo-xdg-thumb [--request] PATH` |
| Docs | `docs/XDG_THUMBNAILS.md` |

Inspired by dirtoo `dirtoo-thumbnail` (Qt); this is Qt-free for embedding.

### Apply
```bash
git pull --ff-only …/thumtoo-337.1-xdg-thumbnails-f71d183.bundle HEAD
```

Next: **338**.

## Prior — 336
request_tile(s) always queue workers (no caller get_tile).
