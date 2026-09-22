// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/activity.hpp"

#include <cstdio>
#include <cstdlib>

static int fails = 0;
static void expect(bool ok, const char* msg) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    ++fails;
  }
}

int main() {
  thumtoo::ActivityLedger ledger;
  const auto id = ledger.note_size_probe_queued("file:///tmp/a.jpg");
  expect(id != 0, "id");
  auto s = ledger.snapshot();
  expect(s.size_probe_queued == 1, "queued");
  expect(s.size_probe_running == 0, "not running");
  ledger.note_size_probe_running(id);
  s = ledger.snapshot();
  expect(s.size_probe_queued == 0, "queued after run");
  expect(s.size_probe_running == 1, "running");
  expect(s.running_size_probe_uris.size() == 1, "uri list");
  ledger.note_size_probe_finished(id, true);
  s = ledger.snapshot();
  expect(s.size_probe_running == 0, "done running");
  expect(s.size_probe_completed == 1, "completed");
  if (fails) {
    std::fprintf(stderr, "%d failures\n", fails);
    return 1;
  }
  std::puts("ok");
  return 0;
}
