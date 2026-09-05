<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# biltoo ↔ thumtoo integration

Primary consumer of the size index and display ladder. thumtoo keys are
**source URIs** only; biltoo **SessionImageId** stays session/edit identity and
must not key durable pixels or tags.

## Mapping

| biltoo need | thumtoo API |
|-------------|-------------|
| Replace provisional layout size (`imageSizeForPath` / probe) | `get_size(uri)` then `request_size(uri, cb)` |
| Soft preview (Gallery / filmstrip / Image mode) | `get_pixels(uri, max_edge)` / `request_pixels(uri, max_edge, cb)` |
| Known-good meta without I/O | `get_meta(uri)` (status, format, still_count, …) |
| Prewarm session paths | `prepare_paths` or CLI `thumtoo-prepare` |
| Archive open without re-walk | `refresh_archive_toc` / `get_archive_entries` |
| Image inside zip/cbz/… | URI `file:///…//archive:member` + normal get/request size/pixels |

Suggested `max_edge` starting points (tunable in biltoo):

| Mode | max_edge |
|------|----------|
| Filmstrip / grid cell | 128 or 256 |
| Gallery soft tile | 256 or 512 |
| Image-mode soft preview | 512 or 1024 |

Prefer **largest cached level ≤ request**; do not upscale in the client if a
smaller level is all that exists yet.

## Threading (Qt)

```text
Client::open(cache_root, Executor{[](auto fn) {
  QMetaObject::invokeMethod(qApp, std::move(fn), Qt::QueuedConnection);
}});
```

- GUI thread: only `get_*` / schedule `request_*`
- Callbacks arrive on the Qt event loop via the Executor
- Never decode or hash on the GUI thread

## URI form

- File: `file:///absolute/path.jpg` (`thumtoo::file_uri_from_path`)
- Archive member (Location form): `file:///book.zip//archive:inner/path.jpg`

biltoo path strings should be converted once at the boundary; do not pass
relative paths into the cache.

## Cache root

Default: `$XDG_CACHE_HOME/thumtoo` (or `~/.cache/thumtoo`). Tests may pass an
explicit root. biltoo should not write beside source images.

## Status handling

| `ContentStatus` | UI hint |
|-----------------|---------|
| `pending` / `incomplete` | Placeholder; keep request in flight |
| `ready` | Use size / pixels |
| `failed` | Allow retry later |
| `unsupported` | Stop retrying this locator |

## Out of scope for thumtoo

- Crop / flip / session edits → biltoo `SessionImageId`
- Desktop icon thumbnailers → Freedesktop Thumbnailer1 (dirtoo client)
- Live directory watching → dirtoo (thumtoo only stores optional snapshots)
