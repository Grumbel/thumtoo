# TODO / agent handoff

## Status (2026-09-30)

**Tip:** thumtoo-012.1-activity-orphan-drop (on `b825e8d` + agent stack).

### 012.1
- `ActivityLedger::drop_orphans()` + `Client::reconcile_activity_if_idle()` —
  clear stuck tile_queued when queue empty and inflight==0 (host Working badge).
- Finish activity on empty-uri claim, shutdown stop path, and destructor queue drop.

### Prior
- 011.1 EPUB layout tile key

### Bundle policy
Work-line base: `b825e8d`. Full stack in each tip bundle.
