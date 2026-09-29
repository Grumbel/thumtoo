# TODO / agent handoff

## Status (2026-09-29)

**Tip:** thumtoo-006-request-tiles-batch-job (on 005 stack).

### 006
- `request_tiles` enqueues **one** EnsureTiles job with `tile_batch` (was N jobs)
- Batch handler: Store hits + sequential miss encode (file/archive/PDF) on one worker
- Avoids N-worker fan-out / re-decode under Gallery multi-cell issue

### 005
- Do not archive-coalesce interactive EnsureTiles (serialised 150-cell settle)
- ProbeSize / soft / LQIP / FocusFull still batch for one extract
