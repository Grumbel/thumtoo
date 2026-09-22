// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/activity.hpp"

#include <cstdio>

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
  ledger.note_size_probe_running(id);
  s = ledger.snapshot();
  expect(s.size_probe_running == 1, "running");
  ledger.note_size_probe_finished(id, true);
  s = ledger.snapshot();
  expect(s.size_probe_completed == 1, "completed");

  const auto aid =
      ledger.note_archive_member_running("/data/photos.zip", "img/001.jpg");
  s = ledger.snapshot();
  expect(s.archive_read_running == 1, "archive running");
  expect(!s.running_archive_labels.empty(), "archive label");
  ledger.note_archive_member_finished(aid, true);
  s = ledger.snapshot();
  expect(s.archive_read_running == 0, "archive done");
  expect(s.archive_read_completed == 1, "archive completed");

  if (fails) {
    std::fprintf(stderr, "%d failures\n", fails);
    return 1;
  }
  std::puts("ok");
  return 0;
}
