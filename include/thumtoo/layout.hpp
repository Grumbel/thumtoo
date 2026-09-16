// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

namespace thumtoo {

/// THUMTOO_STORE_ROOT=1 → Store at cache_root; legacy under cache_root/legacy/.
/// Default dual-path: Store under cache_root/store/; legacy at cache_root.
[[nodiscard]] bool store_root_layout_enabled();

[[nodiscard]] std::filesystem::path legacy_db_root(
    const std::filesystem::path& cache_root);

[[nodiscard]] std::filesystem::path redesign_store_root(
    const std::filesystem::path& cache_root);

/// When STORE_ROOT is on, move classic dual-path files into the root layout.
/// Safe to call repeatedly; skips renames when destinations exist.
void migrate_dual_path_to_store_root(const std::filesystem::path& cache_root);

}  // namespace thumtoo
