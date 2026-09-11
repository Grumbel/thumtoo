// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumtoo/appearance.hpp"

#include <cstdio>
#include <filesystem>
#include <unistd.h>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static int g_failures = 0;

static void expect(bool cond, const char* msg)
{
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    ++g_failures;
  }
}

int main()
{
  const fs::path root =
      fs::temp_directory_path() / ("thumtoo-appearance-test-" + std::to_string(::getpid()));
  fs::remove_all(root);

  {
    auto store = thumtoo::AppearanceStore::open(root);
    expect(store.valid(), "open store");
    expect(fs::exists(store.db_path()), "db file exists");

    const std::string id =
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

    expect(!store.get(id).has_value(), "empty get");

    thumtoo::ContentAppearance app;
    app.content_quarter_turns = 1;
    app.content_h_flip = true;
    store.put(id, app);

    auto got = store.get(id);
    expect(got.has_value(), "get after put");
    if (got) {
      expect(got->content_quarter_turns == 1, "turns");
      expect(got->content_h_flip, "hflip");
      expect(!got->content_v_flip, "vflip");
    }

    // Bare hex also works.
    auto got2 = store.get(
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    expect(got2.has_value(), "get by bare hex");

    // Identity clears the row.
    store.put(id, thumtoo::ContentAppearance{});
    expect(!store.get(id).has_value(), "identity removes");

    // Crop row.
    thumtoo::ContentAppearance crop;
    crop.has_crop = true;
    crop.crop_x = 10;
    crop.crop_y = 20;
    crop.crop_w = 100;
    crop.crop_h = 80;
    crop.crop_source_w = 200;
    crop.crop_source_h = 160;
    store.put(id, crop);
    auto g3 = store.get(id);
    expect(g3.has_value() && g3->has_crop && g3->crop_w == 100, "crop stored");
  }

  // Re-open durable.
  {
    auto store = thumtoo::AppearanceStore::open(root);
    const std::string id =
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    auto got = store.get(id);
    expect(got.has_value() && got->has_crop, "durable reopen");
  }

  fs::remove_all(root);

  if (g_failures) {
    std::cerr << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "ok\n";
  return 0;
}
