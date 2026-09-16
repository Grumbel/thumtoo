// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

namespace thumtoo {

/// Top-level Store layout (default): Store files at cache_root. On-disk
/// dual-path trees are moved under cache_root/legacy/ by migrate.
/// Opt out with THUMTOO_STORE_ROOT=0 (Store under cache_root/store/).
[[nodiscard]] bool store_root_layout_enabled();

/// Destination for relocated pre-cutover files (layout migrate only).
[[nodiscard]] std::filesystem::path legacy_db_root(
    const std::filesystem::path& cache_root);

[[nodiscard]] std::filesystem::path redesign_store_root(
    const std::filesystem::path& cache_root);

/// When STORE_ROOT is on, move classic dual-path files into the root layout.
/// Safe to call repeatedly; skips renames when destinations exist.
void migrate_dual_path_to_store_root(const std::filesystem::path& cache_root);

}  // namespace thumtoo
