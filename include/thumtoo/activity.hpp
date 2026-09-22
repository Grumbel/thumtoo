// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace thumtoo {

/**
 * Live work snapshot for hosts (docs/ACTIVITY.md in biltoo).
 * Phase 1: size probes only. Thread-safe; snapshot() copies under a short lock.
 */
enum class ActivityKind : int {
  SizeProbe = 0,
  // SoftLadder, TileCell, ArchiveMemberRead, … — later phases
};

enum class ActivityPhase : int {
  Queued = 0,
  Running = 1,
};

struct ActivityRecord {
  std::uint64_t id = 0;
  ActivityKind kind = ActivityKind::SizeProbe;
  ActivityPhase phase = ActivityPhase::Queued;
  std::string uri;
};

struct ActivitySnapshot {
  std::size_t size_probe_queued = 0;
  std::size_t size_probe_running = 0;
  /** Lifetime completions since process start (or last clear). */
  std::uint64_t size_probe_completed = 0;
  /** Up to 8 currently running size-probe URIs (newest last). */
  std::vector<std::string> running_size_probe_uris;
};

/**
 * Process-wide activity ledger (one instance owned by Client or global).
 * Jobs call note_* from any thread; hosts call snapshot() on the GUI.
 */
class ActivityLedger {
 public:
  std::uint64_t note_size_probe_queued(std::string uri);
  void note_size_probe_running(std::uint64_t id);
  void note_size_probe_finished(std::uint64_t id, bool /*ok*/);

  [[nodiscard]] ActivitySnapshot snapshot() const;

  void clear();

 private:
  mutable std::mutex mu_;
  std::uint64_t next_id_ = 1;
  std::uint64_t completed_ = 0;
  // id → record (queued or running)
  std::vector<ActivityRecord> active_;
};

/** Shared ledger used by Client workers (hosts may snapshot via Client API). */
ActivityLedger& global_activity_ledger();

}  // namespace thumtoo
