# TODO / agent handoff

## Status (2026-09-23)

**Tip: thumtoo-323-try-exif-external-linkage.**

Fix: `try_exif_embedded_preview_{file,buffer}` were defined inside an
anonymous namespace (internal linkage) → undefined reference when linking
biltoo against libthumtoo. Moved to `namespace thumtoo` after the anon block.

Includes **321–322**. Base still `bd9cca0` (thumtoo-320 tip).

### Apply
```bash
git pull --ff-only …/thumtoo-323.1-try-exif-external-linkage-bd9cca0.bundle HEAD
```

Next: **324**.
