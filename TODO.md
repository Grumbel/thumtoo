# TODO / agent handoff

## Status (2026-09-24)

**Tip: thumtoo-338.3-verify-xdg** (base `f71d183`).

### XDG thumbnails — verified
- Unit tests + MD5 vs system `md5sum`: pass
- Ready signal prefers **requested** flavor (then xx-large→normal)
- Cache path always; D-Bus Queue needs libdbus-1 + session Thumbnailer1

### Verification (2026-09-24, agent — three passes)
- MD5: RFC vectors + XDG `file://` digests match system `md5sum`
- **Compiled unit** (`xdg_thumbnail.cpp` + harness, `THUMTOO_HAVE_DBUS=0`): **ok**
- Ready path: `PendingRequest` stores flavor; try requested first, then xx-large→normal
- Fixed stale comment that still said "prefer large then normal"
- Note: same-URI sequential requests with different flavors — last `pend.flavor` wins
  (class is documented single-instance sequential; not concurrent multi-flavor)
- Full `nix build` / in-tree CMake test **not run** (no Nix / no vips)

### Apply
```bash
git pull --ff-only …/thumtoo-338.3-verify-xdg-f71d183.bundle HEAD
```

Next: **339**.
