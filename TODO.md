# TODO / agent handoff

## Status (2026-09-24)

**Tip: thumtoo-337.2-xdg-thumbnails** (base `f71d183`).

### XDG thumbnails verified (cache path)
- Unit test: URI encode, MD5 digests, flavors, remove_cache, HAVE_DBUS flag
- MD5 cross-check vs system `md5sum` for `file:///tmp/foo.jpg`
- Compiled with `-DTHUMTOO_HAVE_DBUS=0` (no dbus-1 in agent sandbox)
- D-Bus Queue path: needs host with libdbus-1 + session Thumbnailer1

### Apply
```bash
git pull --ff-only …/thumtoo-337.2-xdg-thumbnails-f71d183.bundle HEAD
```

Next: **338**.

## Prior — 337.1
Initial XDG module + CLI + docs.
