# TODO / agent handoff

## Status (2026-09-24)

**Tip: thumtoo-337.3-xdg-thumbnails** (base `f71d183`).

### XDG thumbnails — verified
- Unit tests + MD5 vs system `md5sum`: pass
- Ready signal prefers **requested** flavor (then xx-large→normal)
- Cache path always; D-Bus Queue needs libdbus-1 + session Thumbnailer1

### Verification (2026-09-24, agent session)
- MD5 (in-tree compact RFC 1321): RFC test vectors + XDG `file://` URI digests match system `md5sum` and Python `hashlib.md5`
- Ready path: `PendingRequest` stores flavor; try requested first, then xx-large→normal fallback
- Cache path always independent of D-Bus; `xdg_thumbnail_dbus_built()` gated on `THUMTOO_HAVE_DBUS`
- Full `nix build` / `test_xdg_thumbnail` binary **not run** in this sandbox (no Nix / no vips+sqlite system deps)

### Apply
```bash
git pull --ff-only …/thumtoo-337.3-xdg-thumbnails-f71d183.bundle HEAD
```

Next: **338**.
