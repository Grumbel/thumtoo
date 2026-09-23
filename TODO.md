# TODO / agent handoff

## Status (2026-09-23)

**Tip: thumtoo-326-no-cache-memory-store** (base `f71d183`, includes 324–325).

### 326 — `--no-cache` / `:memory:` Store
`Store::open_memory()` + `Client::open_memory()` — three SQLite `:memory:` DBs.
`thumtoo-prepare --no-cache` for pure cold size timing (ignores `--cache`).

### 325 — Sequential size one-pass
### 324 — prepare sizes-only timing

### Apply
```bash
git pull --ff-only …/thumtoo-326.1-no-cache-memory-store-f71d183.bundle HEAD
```

Next: **327**.

---

**Prior: thumtoo-323-try-exif-external-linkage** (`f71d183`).
