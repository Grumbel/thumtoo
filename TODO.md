# TODO / agent handoff

## Status (2026-09-24)

**Tip: thumtoo-337.3-xdg-thumbnails** (base `f71d183`).

### XDG thumbnails — verified
- Unit tests + MD5 vs system `md5sum`: pass
- Ready signal prefers **requested** flavor (then xx-large→normal)
- Cache path always; D-Bus Queue needs libdbus-1 + session Thumbnailer1

### Apply
```bash
git pull --ff-only …/thumtoo-337.3-xdg-thumbnails-f71d183.bundle HEAD
```

Next: **338**.
