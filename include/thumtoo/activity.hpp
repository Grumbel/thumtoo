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
 * Live work snapshot for hosts (biltoo docs/ACTIVITY.md).
 * Phase 1–2: size probes + archive member extracts.
 */
enum class ActivityKind : int {
  SizeProbe = 0,
  ArchiveMemberRead = 1,
  // SoftLadder, TileCell, … — later
};

enum class ActivityPhase : int {
  Queued = 0,
  Running = 1,
};

struct ActivityRecord {
  std::uint64_t id = 0;
  ActivityKind kind = ActivityKind::SizeProbe;
  ActivityPhase phase = ActivityPhase::Queued;
  std::string uri;           // full locator or archive path for member reads
  std::string archive_root;  // empty if not archive-derived
  std::string member_key;    // member path when applicable
};

struct ActivitySnapshot {
  std::size_t size_probe_queued = 0;
  std::size_t size_probe_running = 0;
  std::uint64_t size_probe_completed = 0;
  std::vector<std::string> running_size_probe_uris;

  std::size_t archive_read_running = 0;
  std::uint64_t archive_read_completed = 0;
  /** Up to 8 current archive extracts: "archive_basename:member". */
  std::vector<std::string> running_archive_labels;
};

class ActivityLedger {
 public:
  std::uint64_t note_size_probe_queued(std::string uri);
  void note_size_probe_running(std::uint64_t id);
  void note_size_probe_finished(std::uint64_t id, bool ok);

  /** Begin a blocking archive member extract (Running immediately). */
  std::uint64_t note_archive_member_running(std::string archive_path,
                                            std::string member);
  void note_archive_member_finished(std::uint64_t id, bool ok);

  [[nodiscard]] ActivitySnapshot snapshot() const;

  void clear();

 private:
  std::uint64_t alloc_id_locked();
  void finish_locked(std::uint64_t id, std::uint64_t* completed_counter);

  mutable std::mutex mu_;
  std::uint64_t next_id_ = 1;
  std::uint64_t size_probe_completed_ = 0;
  std::uint64_t archive_read_completed_ = 0;
  std::vector<ActivityRecord> active_;
};

ActivityLedger& global_activity_ledger();

}  // namespace thumtoo
