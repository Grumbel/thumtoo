# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `thumtoo-349.1-systemd-pkg-config` (base `53e62cd`).

### 349.1 — silence dbus→libsystemd pkg-config spam
- `flake.nix` mkBuildInputs: add `pkgs.systemd` (dbus-1 Requires.private: libsystemd)

### Prior
- 348.1: leptonica for tesseract Requires: lept
- 347.1: purge text layers

### Apply
```bash
git pull --ff-only …/thumtoo-349.1-systemd-pkg-config-53e62cd.bundle HEAD
```
