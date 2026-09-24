# TODO / agent handoff

## Status (2026-09-24)

**Tip: thumtoo-338.2-verify-xdg** (base `f71d183`).

### XDG thumbnails — verified
- Unit tests + MD5 vs system `md5sum`: pass
- Ready signal prefers **requested** flavor (then xx-large→normal)
- Cache path always; D-Bus Queue needs libdbus-1 + session Thumbnailer1

### Verification (2026-09-24, agent — two passes)
- MD5 (in-tree compact RFC 1321): RFC vectors + XDG `file://` digests match system `md5sum`
- **Compiled unit** (`xdg_thumbnail.cpp` + test harness, `THUMTOO_HAVE_DBUS=0`): all checks **ok**
  including stem digests for `/tmp/foo.jpg` and `/tmp/hello world.png`
- Ready path: `PendingRequest` stores flavor; try requested first, then xx-large→normal
- Cache path always independent of D-Bus; `xdg_thumbnail_dbus_built()` gated correctly
- Full `nix build` / in-tree CMake `test_xdg_thumbnail` **not run** (no Nix / no vips)

### Apply
```bash
git pull --ff-only …/thumtoo-338.2-verify-xdg-f71d183.bundle HEAD
```

Next: **339**.
