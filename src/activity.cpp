// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/activity.hpp"

#include <algorithm>

namespace thumtoo {

ActivityLedger& global_activity_ledger() {
  static ActivityLedger ledger;
  return ledger;
}

std::uint64_t ActivityLedger::note_size_probe_queued(std::string uri) {
  std::lock_guard lock(mu_);
  const std::uint64_t id = next_id_++;
  if (next_id_ == 0) {
    next_id_ = 1;
  }
  ActivityRecord r;
  r.id = id;
  r.kind = ActivityKind::SizeProbe;
  r.phase = ActivityPhase::Queued;
  r.uri = std::move(uri);
  active_.push_back(std::move(r));
  // Cap active list to avoid unbounded growth if finish is missed.
  constexpr std::size_t kMaxActive = 4096;
  if (active_.size() > kMaxActive) {
    active_.erase(active_.begin(),
                  active_.begin() + static_cast<std::ptrdiff_t>(active_.size() - kMaxActive));
  }
  return id;
}

void ActivityLedger::note_size_probe_running(std::uint64_t id) {
  std::lock_guard lock(mu_);
  for (auto& r : active_) {
    if (r.id == id) {
      r.phase = ActivityPhase::Running;
      return;
    }
  }
}

void ActivityLedger::note_size_probe_finished(std::uint64_t id, bool /*ok*/) {
  std::lock_guard lock(mu_);
  for (auto it = active_.begin(); it != active_.end(); ++it) {
    if (it->id == id) {
      active_.erase(it);
      ++completed_;
      return;
    }
  }
  // Finish without queue note (e.g. store hit path) — still count completion.
  ++completed_;
}

ActivitySnapshot ActivityLedger::snapshot() const {
  std::lock_guard lock(mu_);
  ActivitySnapshot s;
  s.size_probe_completed = completed_;
  s.running_size_probe_uris.reserve(8);
  for (const auto& r : active_) {
    if (r.kind != ActivityKind::SizeProbe) {
      continue;
    }
    if (r.phase == ActivityPhase::Running) {
      ++s.size_probe_running;
      if (s.running_size_probe_uris.size() < 8) {
        s.running_size_probe_uris.push_back(r.uri);
      }
    } else {
      ++s.size_probe_queued;
    }
  }
  return s;
}

void ActivityLedger::clear() {
  std::lock_guard lock(mu_);
  active_.clear();
  completed_ = 0;
}

}  // namespace thumtoo
